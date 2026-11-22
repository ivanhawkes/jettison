#include <jettison/dock/Definitions.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>

#include <jettison/serialise/Archive.h>

#include <yaml-cpp/yaml.h>

namespace Jettison::Dock
{
namespace
{
// How far a berth's face is probed into and out of the structure it lies on, and how much two shapes
// may touch without overlapping: a berth lies flush on its deck and beside the next.
constexpr double kProbe = 0.05;
constexpr double kTouching = 0.01;
// How near to a unit length, and to square, a berth's normal and heading have to be.
constexpr double kUnitTolerance = 1.0e-3;


[[noreturn]] void Refuse(std::string_view what, std::string_view name, const std::string &problem)
{
	throw Serialise::FormatError{std::format("{} '{}' {}", what, name, problem)};
}


bool Finite(const glm::dvec3 &vector)
{
	return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

// A box turned to its axes, by its centre and half its size along each: a berth's footprint, the
// column of space above it, or a deck's box, square to the platform's axes.
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


// The berth's axes, as BerthOrientation has them: across it, its normal, and back along its heading.
std::array<glm::dvec3, 3> BerthAxes(const BerthLayout &berth)
{
	const glm::dvec3 back = -berth.heading;
	return {glm::cross(berth.normal, back), berth.normal, back};
}


// The space from `from` to `to` above the berth along its normal, over its footprint, less the
// shrink on every side.
Oriented Above(const BerthLayout &berth, glm::vec2 size, double from, double to, double shrink)
{
	Oriented oriented;
	oriented.axes = BerthAxes(berth);
	oriented.centre = berth.position + berth.normal * ((from + to) * 0.5);
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


std::vector<Box> Structure(const PlatformLayout &layout)
{
	std::vector<Box> boxes = layout.decks;
	boxes.insert(boxes.end(), layout.piers.begin(), layout.piers.end());
	return boxes;
}


std::string BerthName(std::size_t index)
{
	return std::format("berth {}", index + 1);
}


// A key renamed, if the node has it: a patch leaves out what it does not change.
void RenameKey(YAML::Node &node, const char *from, const char *to)
{
	if (!node.IsMap() || !node[from])
		return;
	node[to] = node[from];
	node.remove(from);
}
} // namespace


const BerthClassDefinition &BerthClassOf(
    const BerthClasses &classes, std::string_view berthClass, std::string_view namedBy)
{
	// No class is a default: the classes are the game's, so a file has to name one.
	if (berthClass.empty())
		throw Serialise::FormatError{std::format("{} names no berth class", namedBy)};
	const auto found = classes.find(berthClass);
	if (found == classes.end())
		throw Serialise::FormatError{
		    std::format("{} names berth class '{}', which is not defined", namedBy, berthClass)};
	return found->second;
}


void CheckBerthClass(std::string_view name, const BerthClassDefinition &berthClass)
{
	constexpr std::string_view kWhat = "Berth class";
	const glm::vec3 &vehicle = berthClass.largestVehicle;
	const glm::vec2 &berth = berthClass.berthSize;
	for (const float size : {vehicle.x, vehicle.y, vehicle.z, berth.x, berth.y})
		if (!(size > 0.0f) || !std::isfinite(size))
			Refuse(kWhat, name, "has a largest vehicle or a berth that is not of positive size");
	if (vehicle.y > berth.x || vehicle.x > berth.y)
		Refuse(
		    kWhat, name,
		    std::format(
		        "has a largest vehicle of {} x {} m, length by beam, that its {} x {} m berth, width "
		        "by "
		        "length, does not hold",
		        vehicle.x, vehicle.y, berth.x, berth.y));
}


const Serialise::YamlMigrations &BerthClassMigrations()
{
	static const Serialise::YamlMigrations migrations = []
	{
		Serialise::YamlMigrations chain{"Dock.BerthClassDefinition", {kBerthClassVersion}};
		chain.Add(
		    1,
		    [](YAML::Node &root)
		    {
			    RenameKey(root, "largestHull", "largestVehicle");
			    RenameKey(root, "padSize", "berthSize");
		    });
		return chain;
	}();
	return migrations;
}


const Serialise::YamlMigrations &VehicleMigrations()
{
	static const Serialise::YamlMigrations migrations = []
	{
		Serialise::YamlMigrations chain{"Dock.VehicleDefinition", {kVehicleVersion}};
		chain.Add(1, [](YAML::Node &root) { root.remove("cruise"); });
		// The loading deck's hatch went with the deck, retired with the space sample (slice 2).
		chain.Add(
		    2,
		    [](YAML::Node &root)
		    {
			    RenameKey(root, "pad", "berth");
			    root.remove("hatch");
		    });
		return chain;
	}();
	return migrations;
}


const Serialise::YamlMigrations &PlatformMigrations()
{
	static const Serialise::YamlMigrations migrations = []
	{
		Serialise::YamlMigrations chain{"Dock.PlatformLayout", {kPlatformVersion}};
		chain.Add(
		    1,
		    [](YAML::Node &root)
		    {
			    RenameKey(root, "pads", "berths");
			    // The loading deck's bays and traffic, retired with the space sample (slice 2).
			    root.remove("bays");
			    root.remove("traffic");
			    if (YAML::Node berths = root["berths"]; berths.IsSequence())
				    for (YAML::Node berth : berths)
					    RenameKey(berth, "ship", "vehicle");
		    });
		return chain;
	}();
	return migrations;
}


void CheckVehicle(std::string_view name, const VehicleDefinition &vehicle, const BerthClasses &classes)
{
	constexpr std::string_view kWhat = "Vehicle";
	const glm::vec3 size{vehicle.length, vehicle.beam, vehicle.height};
	for (glm::length_t index = 0; index < 3; ++index)
		if (!(size[index] > 0.0f) || !std::isfinite(size[index]))
			Refuse(kWhat, name, "has a length, beam or height that is not a positive size");
	const glm::vec3 largest =
	    BerthClassOf(classes, vehicle.berthClass, std::format("{} '{}'", kWhat, name)).largestVehicle;
	if (vehicle.length > largest.x || vehicle.beam > largest.y || vehicle.height > largest.z)
		Refuse(
		    kWhat, name,
		    std::format(
		        "is {} x {} x {} m, larger than its berth's largest vehicle, {} x {} x {} m",
		        vehicle.length, vehicle.beam, vehicle.height, largest.x, largest.y, largest.z));
}


bool Fits(const BerthClasses &classes, const VehicleDefinition &vehicle, std::string_view berthClass)
{
	const auto berth = classes.find(berthClass);
	const auto own = classes.find(vehicle.berthClass);
	if (berth == classes.end() || own == classes.end())
		return false;
	// The rank orders the classes; the size is what holds the vehicle. Ranks are the game's data, and
	// nothing makes a class ranked above another the larger in every dimension.
	const glm::vec3 &largest = berth->second.largestVehicle;
	return berth->second.rank >= own->second.rank && vehicle.length <= largest.x &&
	       vehicle.beam <= largest.y && vehicle.height <= largest.z;
}


double Radius(const PlatformLayout &layout)
{
	double radius = 0.0;
	for (const Box &box : Structure(layout))
		radius = std::max(radius, glm::length(glm::abs(box.centre) + box.size * 0.5));
	return radius;
}


void CheckLayout(std::string_view name, const PlatformLayout &layout, const BerthClasses &classes)
{
	constexpr std::string_view kWhat = "Platform layout";
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

	for (std::size_t index = 0; index < layout.berths.size(); ++index)
	{
		const BerthLayout &berth = layout.berths[index];
		const std::string berthName = BerthName(index);
		if (!Finite(berth.position) || !Finite(berth.normal) || !Finite(berth.heading) ||
		    std::abs(glm::length(berth.normal) - 1.0) > kUnitTolerance ||
		    std::abs(glm::length(berth.heading) - 1.0) > kUnitTolerance)
			Refuse(kWhat, name, berthName + " has a normal or a heading that is not of unit length");
		if (std::abs(glm::dot(berth.normal, berth.heading)) > kUnitTolerance)
			Refuse(kWhat, name, berthName + " has a heading that is not square to its normal");
		if (!berth.vehicle.empty() && !berth.display)
			Refuse(
			    kWhat, name,
			    berthName +
			        " shows a vehicle, which only a display berth does: a vehicle lands on any other");

		// All of it on the structure: its middle, its corners and the middles of its sides each
		// just under its face in a deck or a pier, and its middle not inside one.
		const std::array<glm::dvec3, 3> axes = BerthAxes(berth);
		const glm::vec2 size =
		    BerthClassOf(classes, berth.berthClass, std::format("{} '{}': {}", kWhat, name, berthName))
		        .berthSize;
		for (const double across : {-0.5, 0.0, 0.5})
			for (const double along : {-0.5, 0.0, 0.5})
			{
				const glm::dvec3 under = berth.position + axes[0] * (across * (size.x - kTouching)) +
				                         axes[2] * (along * (size.y - kTouching)) -
				                         berth.normal * kProbe;
				if (std::ranges::none_of(boxes, [&](const Box &box) { return Inside(box, under); }))
					Refuse(kWhat, name, berthName + " is not all on a deck or a pier");
			}
		if (std::ranges::any_of(
		        boxes,
		        [&](const Box &box) { return Inside(box, berth.position + berth.normal * kProbe); }))
			Refuse(kWhat, name, berthName + " is under a deck or a pier, not on one");

		// Clear above, from just over its face to beyond anything of the platform.
		const Oriented column = Above(berth, size, kProbe, kProbe + 2.0 * radius, kTouching);
		if (std::ranges::any_of(
		        boxes, [&](const Box &box) { return Overlap(column, Square(box, kTouching)); }))
			Refuse(kWhat, name, berthName + " has the platform over it");

		const Oriented footprint = Above(berth, size, -kProbe, kProbe, kTouching);
		for (std::size_t other = 0; other < index; ++other)
			if (Overlap(
			        footprint,
			        Above(
			            layout.berths[other], classes.at(layout.berths[other].berthClass).berthSize,
			            -kProbe, kProbe, kTouching)))
				Refuse(kWhat, name, std::format("{} overlaps {}", berthName, BerthName(other)));
	}
}


Physics::ShapeDefinition DeckShape(const PlatformLayout &layout)
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


glm::dquat BerthOrientation(const BerthLayout &berth)
{
	const std::array<glm::dvec3, 3> axes = BerthAxes(berth);
	return glm::normalize(glm::quat_cast(glm::dmat3{axes[0], axes[1], axes[2]}));
}
} // namespace Jettison::Dock
