#pragma once

#include <glm/glm.hpp>

#include <jettison/celestial/Frames.h>

namespace Jettison::Celestial
{
// Keplerian elements of a body on rails. The orbit is evaluated at simulation time, never
// integrated, so a body is where the clock says it is in every build and after every save.
// Angles in radians, distances in metres, time in simulation seconds. Filled in phase 4.
struct OrbitalElements
{
	double semiMajorAxis{0.0};
	double eccentricity{0.0};
	double inclination{0.0};
	double longitudeOfAscendingNode{0.0};
	double argumentOfPeriapsis{0.0};
	double meanAnomalyAtEpoch{0.0};
	double period{0.0};
};

// The position relative to the parent body at a simulation time.
glm::dvec3 Evaluate(const OrbitalElements &elements, double simSeconds);
} // namespace Jettison::Celestial
