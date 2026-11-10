#include <jettison/celestial/Frames.h>
#include <jettison/celestial/Orbit.h>

#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>

using namespace Jettison::Celestial;

namespace
{
constexpr double kPi = std::numbers::pi;
// Earth's GM, for orbits whose speed can be checked against vis-viva.
constexpr double kMu = 3.986004418e14;

OrbitalElements LowOrbit()
{
	OrbitalElements elements;
	elements.semiMajorAxis = 6.791e6;
	elements.period = PeriodFor(elements.semiMajorAxis, kMu);
	return elements;
}
} // namespace

TEST_CASE("Frames: render space is the difference taken in double, then made a float")
{
	static_assert(sizeof(SystemPosition::metres.x) == sizeof(double));
	// A hundred million kilometres out, where floats are eight kilometres apart and doubles 15
	// micrometres: the difference keeps what the doubles kept.
	const SystemPosition camera{{1.0e11, 2.0e10, -3.0e10}};
	const glm::dvec3 offset{12.345, -0.001, 250.0};
	const SystemPosition station{camera.metres + offset};
	const glm::vec3 render = ToRenderSpace(station, camera);
	CHECK(glm::length(glm::dvec3{render} - offset) < 2e-5);
	// Converting first loses them.
	CHECK(glm::vec3{station.metres} - glm::vec3{camera.metres} != render);

	const SystemPosition origin{{1.496e11, 0.0, 0.0}};
	const LocalPosition local{{100.0, 200.0, 300.0}};
	CHECK(ToLocal(ToSystem(local, origin), origin).metres == local.metres);
}

TEST_CASE("Orbit: Kepler's equation is solved for every eccentricity below one")
{
	for (const double e : {0.0, 0.1, 0.5, 0.8, 0.9, 0.99, 0.999})
		for (int step = -40; step <= 40; ++step)
		{
			const double mean = step * kPi / 16.0;
			const double eccentric = SolveKepler(mean, e);
			const double reduced = std::remainder(mean, 2.0 * kPi);
			CAPTURE(e);
			CAPTURE(mean);
			CHECK(std::abs(eccentric - e * std::sin(eccentric) - reduced) < 1e-14);
		}
}

TEST_CASE("Orbit: a circular orbit starts on +X, runs prograde towards -Z, and keeps its radius")
{
	const OrbitalElements orbit = LowOrbit();
	// About ninety-two and a half minutes, as a station 420 km above an Earth runs.
	CHECK(orbit.period == doctest::Approx(5569.6).epsilon(1e-4));

	const OrbitState start = EvaluateState(orbit, 0.0);
	CHECK(start.position.x == doctest::Approx(orbit.semiMajorAxis));
	CHECK(std::abs(start.position.y) < 1e-6);
	CHECK(std::abs(start.position.z) < 1e-6);
	// Anticlockwise seen from +Y, the reference plane's north.
	CHECK(start.velocity.z < 0.0);
	const double speed = 2.0 * kPi * orbit.semiMajorAxis / orbit.period;
	CHECK(glm::length(start.velocity) == doctest::Approx(speed));

	const glm::dvec3 quarter = Evaluate(orbit, orbit.period / 4.0);
	CHECK(std::abs(quarter.x) < 1e-6 * orbit.semiMajorAxis);
	CHECK(quarter.z == doctest::Approx(-orbit.semiMajorAxis));
	for (int sample = 0; sample < 16; ++sample)
		CHECK(
		    glm::length(Evaluate(orbit, sample * orbit.period / 16.0)) ==
		    doctest::Approx(orbit.semiMajorAxis).epsilon(1e-12));
}

TEST_CASE("Orbit: an ellipse is nearest at periapsis, farthest half a period on, and obeys vis-viva")
{
	OrbitalElements orbit;
	orbit.semiMajorAxis = 2.4e7;
	orbit.eccentricity = 0.7;
	orbit.argumentOfPeriapsis = 0.6;
	orbit.longitudeOfAscendingNode = 1.1;
	orbit.inclination = 0.3;
	orbit.period = PeriodFor(orbit.semiMajorAxis, kMu);

	CHECK(glm::length(Evaluate(orbit, 0.0)) == doctest::Approx(orbit.semiMajorAxis * 0.3));
	CHECK(
	    glm::length(Evaluate(orbit, orbit.period / 2.0)) == doctest::Approx(orbit.semiMajorAxis * 1.7));
	for (int sample = 0; sample < 32; ++sample)
	{
		const OrbitState state = EvaluateState(orbit, sample * orbit.period / 32.0);
		const double r = glm::length(state.position);
		const double v2 = glm::dot(state.velocity, state.velocity);
		CHECK(v2 == doctest::Approx(kMu * (2.0 / r - 1.0 / orbit.semiMajorAxis)).epsilon(1e-10));
	}
}

TEST_CASE("Orbit: the velocity is the position's derivative")
{
	OrbitalElements orbit = LowOrbit();
	orbit.eccentricity = 0.2;
	orbit.inclination = 0.9;
	orbit.longitudeOfAscendingNode = 2.0;
	orbit.argumentOfPeriapsis = 0.4;
	for (const double t : {0.0, 700.0, 2900.0, 5000.0})
	{
		const double h = 0.01;
		const glm::dvec3 difference = (Evaluate(orbit, t + h) - Evaluate(orbit, t - h)) / (2.0 * h);
		const glm::dvec3 velocity = EvaluateState(orbit, t).velocity;
		CHECK(glm::length(difference - velocity) < 1e-3);
	}
}

TEST_CASE("Orbit: inclination tilts the plane about the ascending node, towards +Y")
{
	OrbitalElements orbit = LowOrbit();
	orbit.inclination = kPi / 2.0;
	const glm::dvec3 quarter = Evaluate(orbit, orbit.period / 4.0);
	CHECK(quarter.y == doctest::Approx(orbit.semiMajorAxis));
	CHECK(std::abs(quarter.z) < 1e-6 * orbit.semiMajorAxis);

	// The node a quarter turn round: the plane now holds X and Y.
	orbit.longitudeOfAscendingNode = kPi / 2.0;
	const glm::dvec3 node = Evaluate(orbit, 0.0);
	CHECK(node.z == doctest::Approx(-orbit.semiMajorAxis));
}

TEST_CASE("Orbit: the phase of a long clock keeps its precision, and a period later is the same place")
{
	const OrbitalElements orbit = LowOrbit();
	// A century of simulation seconds.
	const double century = 100.0 * 365.25 * 86400.0;
	const double turns = std::floor(century / orbit.period);
	const double t = turns * orbit.period;
	CHECK(glm::length(Evaluate(orbit, t) - Evaluate(orbit, 0.0)) < 1e-2);
	CHECK(Phase(-1.0, 10.0) == doctest::Approx(0.9));
	CHECK(Phase(-1e-300, 10.0) == 0.0);
	CHECK(Phase(25.0, 10.0) == doctest::Approx(0.5));
}

TEST_CASE("Orbit: a body with no orbit is at its parent")
{
	CHECK(Evaluate(OrbitalElements{}, 1234.0) == glm::dvec3{0.0});
}

TEST_CASE("Rotation: tilt, then spin about the tilted axis, a whole turn per period")
{
	Rotation rotation;
	rotation.period = 86164.0;
	rotation.axialTilt = 0.4091;
	const glm::dvec3 axis = Orientation(rotation, 1000.0) * glm::dvec3{0.0, 1.0, 0.0};
	CHECK(axis.y == doctest::Approx(std::cos(rotation.axialTilt)));
	CHECK(axis.z == doctest::Approx(std::sin(rotation.axialTilt)));

	const glm::dvec3 meridian = Orientation(rotation, 0.0) * glm::dvec3{1.0, 0.0, 0.0};
	const glm::dvec3 later = Orientation(rotation, rotation.period) * glm::dvec3{1.0, 0.0, 0.0};
	CHECK(glm::length(meridian - later) < 1e-12);

	// A quarter day: +X turns towards -Z, anticlockwise from +Y.
	Rotation upright;
	upright.period = 100.0;
	const glm::dvec3 quarter = Orientation(upright, 25.0) * glm::dvec3{1.0, 0.0, 0.0};
	CHECK(quarter.z == doctest::Approx(-1.0));
	upright.period = -100.0;
	CHECK((Orientation(upright, 25.0) * glm::dvec3{1.0, 0.0, 0.0}).z == doctest::Approx(1.0));
}

TEST_CASE("Orbit: a position is the same bits in every build")
{
	// The orbit and the time are arbitrary; the bits were recorded from a Debug build and are
	// checked in Release and under the sanitisers, which is what the module's -ffp-contract=off
	// is for.
	OrbitalElements orbit;
	orbit.semiMajorAxis = 3.844e8;
	orbit.eccentricity = 0.0549;
	orbit.inclination = 0.0898;
	orbit.longitudeOfAscendingNode = 2.1831;
	orbit.argumentOfPeriapsis = 5.5528;
	orbit.meanAnomalyAtEpoch = 2.3554;
	orbit.period = 2360591.5;
	const glm::dvec3 position = Evaluate(orbit, 123456789.25);
	CHECK(std::bit_cast<std::uint64_t>(position.x) == 4733883752160754496u);
	CHECK(std::bit_cast<std::uint64_t>(position.y) == 13934674603917960173u);
	CHECK(std::bit_cast<std::uint64_t>(position.z) == 4732722547826920066u);
}
