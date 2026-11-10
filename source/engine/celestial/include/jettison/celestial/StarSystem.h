#pragma once

#include <string>
#include <vector>

#include <jettison/celestial/Orbit.h>

namespace Jettison::Celestial
{
// One star system as data: the bodies in their orbital hierarchy, the stations, the jump points.
// Loaded from a system description in phase 4; the jump-lane graph between systems is the galaxy
// (phase 6) and lives beside this.
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
	int parent{-1}; // Index into StarSystem::bodies, or -1 for the primary.
	OrbitalElements orbit;
	double radius{0.0};
	double rotationPeriod{0.0};
};

struct StarSystem
{
	std::string name;
	std::vector<Body> bodies;
};

// Every body's system-frame position at a simulation time, parents before children.
std::vector<SystemPosition> EvaluateSystem(const StarSystem &system, double simSeconds);
} // namespace Jettison::Celestial
