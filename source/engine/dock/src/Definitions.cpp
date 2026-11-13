#include <jettison/dock/Definitions.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>

#include <jettison/dock/Roads.h>
#include <jettison/serialise/Archive.h>

namespace Jettison::Dock
{
namespace
{
// How far a pad's face is probed into and out of the structure it lies on, and how much two shapes
// may touch without overlapping: a pad lies flush on its deck and beside the next.
constexpr double kProbe = 0.05;
constexpr double kTouching = 0.01;
// How near to a unit length, and to square, a pad's normal and heading have to be.
constexpr double kUnitTolerance = 1.0e-3;

[[noreturn]] void Refuse(std::string_view what, std::string_view name, const std::string &problem)
{
	throw Serialise::FormatError{std::format("{} '{}' {}", what, name, problem)};
}

bool Finite(const glm::dvec3 &vector)
{
	return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

// A box turned to its axes, by its centre and half its size along each: a pad's footprint, the
// column of space above it, or a deck's box, square to the station's axes.
struct Oriented
{
	glm::dvec3 centre{0.0};
	std::array<glm::dvec3, 3> axes{glm::dvec3{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
	glm::dvec3 half{0.0};
};

Oriented Square(const Box &box, double shrink)
{
	Oriented oriented;
	oriented.centre = box.centre;
	oriented.half = glm::max(box.size * 0.5 - shrink, glm::dvec3{0.0});
	return oriented;
}

// The pad's axes, as PadOrientation has them: across it, its normal, and back along its heading.
std::array<glm::dvec3, 3> PadAxes(const PadLayout &pad)
{
	const glm::dvec3 back = -pad.heading;
	return {glm::cross(pad.normal, back), pad.normal, back};
}

// The space from `from` to `to` above the pad along its normal, over its footprint, less the
// shrink on every side.
Oriented Above(const PadLayout &pad, double from, double to, double shrink)
{
	const glm::vec2 size = PadSize(pad.padClass);
	Oriented oriented;
	oriented.axes = PadAxes(pad);
	oriented.centre = pad.position + pad.normal * ((from + to) * 0.5);
	oriented.half =
	    glm::max(glm::dvec3{size.x * 0.5, (to - from) * 0.5, size.y * 0.5} - shrink, glm::dvec3{0.0});
	return oriented;
}

// Whether two turned boxes overlap: the separating axis test over their faces' normals and the
// crosses of their edges, fifteen axes.
bool Overlap(const Oriented &a, const Oriented &b)
{
	const glm::dvec3 between = b.centre - a.centre;
	const auto separates = [&](const glm::dvec3 &axis)
	{
		const double length = glm::length(axis);
		if (length < 1.0e-9)
			return false;
		const glm::dvec3 unit = axis / length;
		double reach = 0.0;
		for (std::size_t index = 0; index < 3; ++index)
			reach +=
			    a.half[static_cast<glm::length_t>(index)] * std::abs(glm::dot(a.axes[index], unit)) +
			    b.half[static_cast<glm::length_t>(index)] * std::abs(glm::dot(b.axes[index], unit));
		return std::abs(glm::dot(between, unit)) > reach;
	};
	for (std::size_t index = 0; index < 3; ++index)
		if (separates(a.axes[index]) || separates(b.axes[index]))
			return false;
	for (const glm::dvec3 &first : a.axes)
		for (const glm::dvec3 &second : b.axes)
			if (separates(glm::cross(first, second)))
				return false;
	return true;
}

bool Inside(const Box &box, const glm::dvec3 &point)
{
	const glm::dvec3 offset = glm::abs(point - box.centre);
	const glm::dvec3 half = box.size * 0.5;
	return offset.x <= half.x && offset.y <= half.y && offset.z <= half.z;
}

// The decks and piers together.
std::vector<Box> Structure(const StationLayout &layout)
{
	std::vector<Box> boxes = layout.decks;
	boxes.insert(boxes.end(), layout.piers.begin(), layout.piers.end());
	return boxes;
}

std::string PadName(std::size_t index)
{
	return std::format("pad {}", index + 1);
}
} // namespace

glm::vec3 LargestHull(PadClass padClass)
{
	switch (padClass)
	{
		case PadClass::S: return {25.0f, 20.0f, 8.0f};
		case PadClass::M: return {60.0f, 30.0f, 15.0f};
		case PadClass::L: return {140.0f, 50.0f, 30.0f};
		case PadClass::XL: return {300.0f, 80.0f, 60.0f};
	}
	return {};
}

glm::vec2 PadSize(PadClass padClass)
{
	switch (padClass)
	{
		case PadClass::S: return {35.0f, 40.0f};
		case PadClass::M: return {50.0f, 90.0f};
		case PadClass::L: return {80.0f, 180.0f};
		case PadClass::XL: return {120.0f, 360.0f};
	}
	return {};
}

void CheckHull(std::string_view name, const HullDefinition &hull)
{
	constexpr std::string_view kWhat = "Hull";
	const glm::vec3 size{hull.length, hull.beam, hull.height};
	for (glm::length_t index = 0; index < 3; ++index)
		if (!(size[index] > 0.0f) || !std::isfinite(size[index]))
			Refuse(kWhat, name, "has a length, beam or height that is not a positive size");
	const glm::vec3 largest = LargestHull(hull.pad);
	if (hull.length > largest.x || hull.beam > largest.y || hull.height > largest.z)
		Refuse(
		    kWhat, name,
		    std::format(
		        "is {} x {} x {} m, larger than its pad's largest hull, {} x {} x {} m", hull.length,
		        hull.beam, hull.height, largest.x, largest.y, largest.z));
	const glm::vec3 half = glm::vec3{hull.beam, hull.height, hull.length} * 0.5f;
	if (!(glm::all(glm::lessThanEqual(glm::abs(hull.hatch), half + 0.01f))))
		Refuse(kWhat, name, "has its cargo hatch outside its hull");
	// A drive's time rises with the distance, and no faster than it.
	const Ai::CruiseDrive &cruise = hull.cruise;
	if (!(cruise.seconds > 0.0f) || !std::isfinite(cruise.seconds) || !(cruise.reference > 0.0) ||
	    !std::isfinite(cruise.reference) || !(cruise.exponent > 0.0f) || !(cruise.exponent <= 1.0f))
		Refuse(
		    kWhat, name,
		    "has a cruise drive whose seconds or reference is not positive, or whose exponent is "
		    "not above 0 and at most 1");
}

double Radius(const StationLayout &layout)
{
	double radius = 0.0;
	for (const Box &box : Structure(layout))
		radius = std::max(radius, glm::length(glm::abs(box.centre) + box.size * 0.5));
	return radius;
}

void CheckLayout(std::string_view name, const StationLayout &layout)
{
	constexpr std::string_view kWhat = "Station layout";
	const std::vector<Box> boxes = Structure(layout);
	if (boxes.empty())
		Refuse(kWhat, name, "has no decks or piers");
	for (const Box &box : boxes)
		if (!Finite(box.centre) || !Finite(box.size) || box.size.x <= 0.0 || box.size.y <= 0.0 ||
		    box.size.z <= 0.0)
			Refuse(kWhat, name, "has a deck or a pier that is not a box of positive size");
	if (!(layout.reservationSeconds > 0.0f) || !std::isfinite(layout.reservationSeconds))
		Refuse(kWhat, name, "has a reservation that is not a positive number of seconds");
	const double radius = Radius(layout);

	for (std::size_t index = 0; index < layout.pads.size(); ++index)
	{
		const PadLayout &pad = layout.pads[index];
		const std::string padName = PadName(index);
		if (!Finite(pad.position) || !Finite(pad.normal) || !Finite(pad.heading) ||
		    std::abs(glm::length(pad.normal) - 1.0) > kUnitTolerance ||
		    std::abs(glm::length(pad.heading) - 1.0) > kUnitTolerance)
			Refuse(kWhat, name, padName + " has a normal or a heading that is not of unit length");
		if (std::abs(glm::dot(pad.normal, pad.heading)) > kUnitTolerance)
			Refuse(kWhat, name, padName + " has a heading that is not square to its normal");
		if (!pad.ship.empty() && !pad.display)
			Refuse(
			    kWhat, name,
			    padName + " shows a ship, which only a display pad does: a ship lands on any other");

		// All of it on the structure: its middle, its corners and the middles of its sides each
		// just under its face in a deck or a pier, and its middle not inside one.
		const std::array<glm::dvec3, 3> axes = PadAxes(pad);
		const glm::vec2 size = PadSize(pad.padClass);
		for (const double across : {-0.5, 0.0, 0.5})
			for (const double along : {-0.5, 0.0, 0.5})
			{
				const glm::dvec3 under = pad.position + axes[0] * (across * (size.x - kTouching)) +
				                         axes[2] * (along * (size.y - kTouching)) - pad.normal * kProbe;
				if (std::ranges::none_of(boxes, [&](const Box &box) { return Inside(box, under); }))
					Refuse(kWhat, name, padName + " is not all on a deck or a pier");
			}
		if (std::ranges::any_of(
		        boxes, [&](const Box &box) { return Inside(box, pad.position + pad.normal * kProbe); }))
			Refuse(kWhat, name, padName + " is under a deck or a pier, not on one");

		// Clear above, from just over its face to beyond anything of the station.
		const Oriented column = Above(pad, kProbe, kProbe + 2.0 * radius, kTouching);
		if (std::ranges::any_of(
		        boxes, [&](const Box &box) { return Overlap(column, Square(box, kTouching)); }))
			Refuse(kWhat, name, padName + " has the station over it");

		const Oriented footprint = Above(pad, -kProbe, kProbe, kTouching);
		for (std::size_t other = 0; other < index; ++other)
			if (Overlap(footprint, Above(layout.pads[other], -kProbe, kProbe, kTouching)))
				Refuse(kWhat, name, std::format("{} overlaps {}", padName, PadName(other)));
	}
	for (const Bay &bay : layout.bays)
		if (!Finite(bay.position) || std::abs(glm::length(bay.normal) - 1.0) > kUnitTolerance)
			Refuse(kWhat, name, "has a cargo bay whose door faces no way");

	const DeckTraffic &traffic = layout.traffic;
	if (traffic.buggies == 0 || traffic.cratesPerTrip <= 0 || !(traffic.speed > 0.0f) ||
	    !std::isfinite(traffic.speed))
		Refuse(
		    kWhat, name,
		    "has deck traffic of no buggies, no crates a trip or a speed that is not a positive one");
	if (layout.bays.empty())
		return;
	const RoadGraph roads = Roads(layout);
	for (std::size_t index = 0; index < layout.pads.size(); ++index)
		if (!layout.pads[index].display && !roads.pads[index].reached)
			Refuse(kWhat, name, PadName(index) + " is not reached by road from a cargo bay");
}

Physics::ShapeDefinition DeckShape(const StationLayout &layout)
{
	Physics::ShapeDefinition shape;
	shape.kind = Physics::ShapeKind::Mesh;
	for (const Box &box : Structure(layout))
	{
		const auto base = static_cast<std::uint32_t>(shape.vertices.size());
		// Corner i has bit 0 for +X, bit 1 for +Y and bit 2 for +Z.
		for (std::uint32_t corner = 0; corner < 8; ++corner)
		{
			const glm::dvec3 sign{
			    (corner & 1u) ? 0.5 : -0.5, (corner & 2u) ? 0.5 : -0.5, (corner & 4u) ? 0.5 : -0.5};
			shape.vertices.emplace_back(box.centre + sign * box.size);
		}
		// Each face's corners round it, then wound to face out.
		for (std::uint32_t axis = 0; axis < 3; ++axis)
			for (const std::uint32_t side : {0u, 1u})
			{
				const std::uint32_t u = 1u << ((axis + 1) % 3);
				const std::uint32_t v = 1u << ((axis + 2) % 3);
				const std::uint32_t at = side << axis;
				std::array<std::uint32_t, 4> quad{at, at | u, at | u | v, at | v};
				glm::dvec3 outward{0.0};
				outward[static_cast<glm::length_t>(axis)] = side ? 1.0 : -1.0;
				const auto vertex = [&](std::uint32_t corner)
				{ return glm::dvec3{shape.vertices[base + corner]}; };
				const glm::dvec3 normal =
				    glm::cross(vertex(quad[1]) - vertex(quad[0]), vertex(quad[2]) - vertex(quad[0]));
				if (glm::dot(normal, outward) < 0.0)
					std::swap(quad[1], quad[3]);
				for (const std::uint32_t corner :
				     {quad[0], quad[1], quad[2], quad[0], quad[2], quad[3]})
					shape.indices.push_back(base + corner);
			}
	}
	return shape;
}

glm::dquat PadOrientation(const PadLayout &pad)
{
	const std::array<glm::dvec3, 3> axes = PadAxes(pad);
	return glm::normalize(glm::quat_cast(glm::dmat3{axes[0], axes[1], axes[2]}));
}
} // namespace Jettison::Dock
