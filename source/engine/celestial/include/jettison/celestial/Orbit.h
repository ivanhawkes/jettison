#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Jettison::Celestial
{
// Keplerian elements of a body on rails. The orbit is evaluated at simulation time, never
// integrated, so a body is where the clock says it is in every build and after every save.
// Angles in radians, distances in metres, time in simulation seconds.
//
// Elliptic orbits only: the eccentricity is in [0, 1). The reference plane is the system frame's
// XZ plane and its north is +Y, so the astronomical convention (reference plane XY, north +Z) is
// turned onto the engine's Y-up axes: a prograde orbit runs anticlockwise seen from +Y. The
// ascending node's longitude is measured from +X towards -Z, which is the direction a prograde
// body at the node moves in, and the mean anomaly is the body's at simulation time 0.
struct OrbitalElements
{
	double semiMajorAxis{0.0};
	double eccentricity{0.0};
	double inclination{0.0};
	double longitudeOfAscendingNode{0.0};
	double argumentOfPeriapsis{0.0};
	double meanAnomalyAtEpoch{0.0};
	// Seconds per revolution. Zero in data means "from the parent's gravitational parameter",
	// which StarSystem's Resolve fills in; Evaluate needs it positive for a non-zero orbit.
	double period{0.0};
};

// A body's position and velocity relative to its parent.
struct OrbitState
{
	glm::dvec3 position{0.0};
	glm::dvec3 velocity{0.0};
};

// The period of an orbit of that semi-major axis about a parent of that gravitational parameter
// (GM, in m^3/s^2): Kepler's third law.
double PeriodFor(double semiMajorAxis, double gravitationalParameter);

// The eccentric anomaly for a mean anomaly, by Newton's method from a fixed starting point and
// with a fixed iteration limit, so the answer is the same sequence of operations in every build.
// Converges to within an ulp or two of the root for every eccentricity below 1.
double SolveKepler(double meanAnomaly, double eccentricity);

// The position relative to the parent body at a simulation time. A zero semi-major axis is a
// body at its parent's position.
glm::dvec3 Evaluate(const OrbitalElements &elements, double simSeconds);
OrbitState EvaluateState(const OrbitalElements &elements, double simSeconds);

// A body's spin: rotationPeriod seconds per sidereal day about its axis (negative for
// retrograde), the axis tilted axialTilt radians from +Y about +X, at rotationAtEpoch radians at
// simulation time 0. A zero period does not rotate.
struct Rotation
{
	double period{0.0};
	double axialTilt{0.0};
	double angleAtEpoch{0.0};
};

// The body-to-system rotation at a simulation time: tilt, then spin about the tilted axis.
glm::dquat Orientation(const Rotation &rotation, double simSeconds);

// The phase of something periodic, in [0, 1). fmod is exact in floating point, so a phase taken
// this way loses nothing to the size of the time, where multiplying the time by an angular rate
// first rounds away a growing share of a revolution as the clock runs.
double Phase(double simSeconds, double period);
} // namespace Jettison::Celestial
