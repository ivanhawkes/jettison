#include <jettison/celestial/StarSystem.h>

#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>

namespace Jettison::Celestial
{
namespace
{
[[noreturn]] void Refuse(const StarSystem &system, const Body &body, const std::string &what)
{
	throw Serialise::FormatError("Star system '" + system.name + "': body '" + body.name + "' " + what);
}

bool NonNegative(double value)
{
	return std::isfinite(value) && value >= 0.0;
}
} // namespace

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterEnum<BodyKind>("Celestial.BodyKind")
		    .Value<BodyKind::Star>("Star")
		    .Value<BodyKind::Planet>("Planet")
		    .Value<BodyKind::Moon>("Moon")
		    .Value<BodyKind::Station>("Station")
		    .Value<BodyKind::JumpPoint>("JumpPoint");
		RegisterType<OrbitalElements>("Celestial.OrbitalElements")
		    .Field<&OrbitalElements::semiMajorAxis>("semiMajorAxis", Attribute::Saved)
		    .Field<&OrbitalElements::eccentricity>("eccentricity", Attribute::Saved)
		    .Field<&OrbitalElements::inclination>("inclination", Attribute::Saved)
		    .Field<&OrbitalElements::longitudeOfAscendingNode>(
		        "longitudeOfAscendingNode", Attribute::Saved)
		    .Field<&OrbitalElements::argumentOfPeriapsis>("argumentOfPeriapsis", Attribute::Saved)
		    .Field<&OrbitalElements::meanAnomalyAtEpoch>("meanAnomalyAtEpoch", Attribute::Saved)
		    .Field<&OrbitalElements::period>("period", Attribute::Saved);
		RegisterType<Rotation>("Celestial.Rotation")
		    .Field<&Rotation::period>("period", Attribute::Saved)
		    .Field<&Rotation::axialTilt>("axialTilt", Attribute::Saved)
		    .Field<&Rotation::angleAtEpoch>("angleAtEpoch", Attribute::Saved);
		RegisterType<Body>("Celestial.Body")
		    .Field<&Body::name>("name", Attribute::Saved)
		    .Field<&Body::kind>("kind", Attribute::Saved)
		    .Field<&Body::parent>("parent", Attribute::Saved)
		    .Field<&Body::orbit>("orbit", Attribute::Saved)
		    .Field<&Body::rotation>("rotation", Attribute::Saved)
		    .Field<&Body::radius>("radius", Attribute::Saved)
		    .Field<&Body::gravitationalParameter>("gravitationalParameter", Attribute::Saved);
		RegisterType<StarSystem>("Celestial.StarSystem", Attribute::Moddable)
		    .Field<&StarSystem::name>("name", Attribute::Saved)
		    .Field<&StarSystem::bodies>("bodies", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

int StarSystem::Find(std::string_view bodyName) const
{
	for (std::size_t index = 0; index < bodies.size(); ++index)
		if (bodies[index].name == bodyName)
			return static_cast<int>(index);
	return -1;
}

void Resolve(StarSystem &system)
{
	if (system.bodies.empty())
		throw Serialise::FormatError("Star system '" + system.name + "' has no bodies");

	// Ordered, so the first duplicate reported is the same one on every run.
	std::map<std::string, int, std::less<>> indices;
	for (std::size_t index = 0; index < system.bodies.size(); ++index)
	{
		Body &body = system.bodies[index];
		if (body.name.empty())
			throw Serialise::FormatError(
			    "Star system '" + system.name + "': body " + std::to_string(index) + " has no name");
		if (!indices.emplace(body.name, static_cast<int>(index)).second)
			Refuse(system, body, "is listed twice");

		if (index == 0)
		{
			if (!body.parent.empty())
				Refuse(
				    system, body,
				    "is the primary, listed first, and cannot orbit '" + body.parent + "'");
			if (body.orbit.semiMajorAxis != 0.0)
				Refuse(system, body, "is the primary, at the origin, and cannot have an orbit");
			body.parentIndex = -1;
		}
		else
		{
			if (body.parent.empty())
				Refuse(system, body, "names no parent; only the first body is the primary");
			const auto parent = indices.find(body.parent);
			if (parent == indices.end() || parent->second == static_cast<int>(index))
				Refuse(system, body, "orbits '" + body.parent + "', which is not listed before it");
			body.parentIndex = parent->second;
		}

		OrbitalElements &orbit = body.orbit;
		if (!NonNegative(body.radius))
			Refuse(system, body, "has a negative radius");
		if (!NonNegative(body.gravitationalParameter))
			Refuse(system, body, "has a negative gravitational parameter");
		if (!NonNegative(orbit.semiMajorAxis))
			Refuse(system, body, "has a negative semi-major axis");
		if (!NonNegative(orbit.eccentricity) || orbit.eccentricity >= 1.0)
			Refuse(system, body, "has an eccentricity outside [0, 1): orbits on rails are ellipses");
		if (!NonNegative(orbit.period))
			Refuse(system, body, "has a negative period");
		if (!std::isfinite(body.rotation.period))
			Refuse(system, body, "has a rotation period that is not a number");

		if (orbit.semiMajorAxis > 0.0 && orbit.period == 0.0)
		{
			const Body &parent = system.bodies[static_cast<std::size_t>(body.parentIndex)];
			if (parent.gravitationalParameter <= 0.0)
				Refuse(
				    system, body,
				    "gives no period, and its parent '" + parent.name +
				        "' no gravitational parameter to derive one from");
			orbit.period = PeriodFor(orbit.semiMajorAxis, parent.gravitationalParameter);
		}
	}
}

StarSystem ReadStarSystem(std::string_view text)
{
	RegisterTypes();
	auto system = Serialise::YamlArchive::Read<StarSystem>(
	    text, Serialise::FormatVersion{kStarSystemFormatVersion});
	Resolve(system);
	return system;
}

std::string WriteStarSystem(const StarSystem &system)
{
	RegisterTypes();
	return Serialise::YamlArchive::Write(system, Serialise::FormatVersion{kStarSystemFormatVersion});
}

std::vector<SystemPosition> EvaluateSystem(const StarSystem &system, double simSeconds)
{
	std::vector<SystemPosition> positions;
	positions.reserve(system.bodies.size());
	for (const Body &body : system.bodies)
	{
		const glm::dvec3 parent = body.parentIndex < 0
		                              ? glm::dvec3{0.0}
		                              : positions[static_cast<std::size_t>(body.parentIndex)].metres;
		positions.push_back({parent + Evaluate(body.orbit, simSeconds)});
	}
	return positions;
}

SystemPosition EvaluateBody(const StarSystem &system, std::size_t body, double simSeconds)
{
	// Summed from the primary down, as EvaluateSystem sums: the same additions in the same order,
	// so a body is at the same position to the bit whichever of the two placed it.
	std::vector<std::size_t> chain;
	for (int index = static_cast<int>(body); index >= 0;
	     index = system.bodies[static_cast<std::size_t>(index)].parentIndex)
		chain.push_back(static_cast<std::size_t>(index));
	glm::dvec3 position{0.0};
	for (const std::size_t link : std::views::reverse(chain))
		position = position + Evaluate(system.bodies[link].orbit, simSeconds);
	return {position};
}

glm::dquat JumpPointFacing(const StarSystem &system, std::size_t body, double simSeconds)
{
	if (body >= system.bodies.size() || system.bodies[body].kind != BodyKind::JumpPoint)
		throw std::invalid_argument{std::format(
		    "Celestial: body {} of '{}' is not a jump point, and has no facing", body, system.name)};
	const std::vector<SystemPosition> at = EvaluateSystem(system, simSeconds);
	const glm::dvec3 &from = at[body].metres;

	std::optional<std::size_t> target;
	for (int parent = system.bodies[body].parentIndex; parent >= 0 && !target;
	     parent = system.bodies[static_cast<std::size_t>(parent)].parentIndex)
	{
		const BodyKind kind = system.bodies[static_cast<std::size_t>(parent)].kind;
		if (kind == BodyKind::Planet || kind == BodyKind::Moon)
			target = static_cast<std::size_t>(parent);
	}
	// The nearest of a kind, the first listed of two as near.
	for (const BodyKind kind : {BodyKind::Planet, BodyKind::Station})
	{
		if (target)
			break;
		double nearest = std::numeric_limits<double>::infinity();
		for (std::size_t other = 0; other < system.bodies.size(); ++other)
		{
			const double distance = glm::length(at[other].metres - from);
			if (system.bodies[other].kind == kind && distance < nearest)
			{
				nearest = distance;
				target = other;
			}
		}
	}
	if (!target || at[*target].metres == from)
		return Orientation(system.bodies[body].rotation, simSeconds);

	const glm::dvec3 ahead = glm::normalize(at[*target].metres - from);
	const glm::dvec3 up =
	    std::abs(ahead.y) > 0.999999 ? glm::dvec3{1.0, 0.0, 0.0} : glm::dvec3{0.0, 1.0, 0.0};
	return glm::quatLookAtRH(ahead, up);
}

double PresentationTime(double previousSimSeconds, double currentSimSeconds, double alpha)
{
	return previousSimSeconds + (currentSimSeconds - previousSimSeconds) * alpha;
}
} // namespace Jettison::Celestial
