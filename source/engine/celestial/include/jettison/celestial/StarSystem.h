#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/celestial/Frames.h>
#include <jettison/celestial/Orbit.h>

namespace Jettison::Celestial
{
// One star system as data: the bodies in their orbital hierarchy, the stations, the jump points.
// The jump-lane graph between systems is the galaxy (Galaxy.h).
enum class BodyKind
{
	Star,
	Planet,
	Moon,
	Station,
	JumpPoint
};

struct Body
{
	std::string name;
	BodyKind kind{BodyKind::Planet};
	// The name of the body this one orbits, listed before it; empty for the primary, which sits
	// at the system frame's origin.
	std::string parent;
	OrbitalElements orbit;
	Rotation rotation;
	double radius{0.0};
	// GM, in m^3/s^2: what a child's period is derived from when its data leaves it out. Zero
	// for a body nothing orbits.
	double gravitationalParameter{0.0};

	// Runtime: the parent's index in StarSystem::bodies, or -1 for the primary. Filled in by
	// Resolve, not written to the system file.
	int parentIndex{-1};
};

struct StarSystem
{
	std::string name;
	std::vector<Body> bodies;

	// The index of the body of that name, or -1.
	int Find(std::string_view bodyName) const;
};

// Registers BodyKind, OrbitalElements, Rotation, Body and StarSystem with Reflect. Idempotent.
void RegisterTypes();

// The system file's version, which ReadStarSystem expects and WriteStarSystem writes.
inline constexpr std::uint32_t kStarSystemFormatVersion = 1;

// Checks the hierarchy and fills in what data may leave out, in one place that the reader and a
// system built in code both go through: each parent named and listed before its child, one
// primary and it first, names unique and not empty, eccentricities in [0, 1), radii and periods
// not negative, and each period left at zero derived from the parent's gravitational parameter.
// Throws Serialise::FormatError naming the system and the body.
void Resolve(StarSystem &system);

// A system file (YAML, through Serialise::YamlArchive) read and resolved, and its writer. A
// derived period is written back as the number it became, so a written file reads as the same
// system without the parent's gravitational parameter.
StarSystem ReadStarSystem(std::string_view text);
std::string WriteStarSystem(const StarSystem &system);

// Every body's system-frame position at a simulation time, in the order of StarSystem::bodies,
// which is parents before children. The system must have been resolved.
std::vector<SystemPosition> EvaluateSystem(const StarSystem &system, double simSeconds);

// One body's system-frame position, walking up to the primary: for a presentation that shows a
// few bodies of a large system.
SystemPosition EvaluateBody(const StarSystem &system, std::size_t body, double simSeconds);

// The time presentation shows between two simulation steps. A body on rails is evaluated at the
// interpolated time, not lerped between the positions the steps put it at, so it is on its orbit
// at every frame and every body shown is shown at the same moment.
double PresentationTime(double previousSimSeconds, double currentSimSeconds, double alpha);
} // namespace Jettison::Celestial
