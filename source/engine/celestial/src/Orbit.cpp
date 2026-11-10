#include <jettison/celestial/Orbit.h>

#include <cmath>
#include <numbers>

#include <jettison/core/Assert.h>

namespace Jettison::Celestial
{
namespace
{
constexpr double kTwoPi = 2.0 * std::numbers::pi;

// Newton's method converges quadratically from the starting points below, so a handful of
// iterations reach the root; the limit is for the eccentricities near 1, where the first steps
// are short, and it is never the answer's precision.
constexpr int kKeplerIterations = 32;
constexpr double kKeplerTolerance = 1.0e-15;
} // namespace

double PeriodFor(double semiMajorAxis, double gravitationalParameter)
{
	JETTISON_ASSERT(semiMajorAxis >= 0.0 && gravitationalParameter > 0.0);
	return kTwoPi * std::sqrt(semiMajorAxis * semiMajorAxis * semiMajorAxis / gravitationalParameter);
}

double SolveKepler(double meanAnomaly, double eccentricity)
{
	JETTISON_ASSERT(eccentricity >= 0.0 && eccentricity < 1.0);
	// Into [-pi, pi]: remainder is exact, so the reduction itself costs nothing.
	const double mean = std::remainder(meanAnomaly, kTwoPi);
	// From the mean anomaly the iteration can overshoot past the root when the orbit is long
	// and thin. From pi (with the mean anomaly's sign) it cannot: E - e sin E - M is convex
	// between the root and pi, so each step lands between the root and the last guess.
	double eccentric = eccentricity < 0.8 ? mean : std::copysign(std::numbers::pi, mean);
	for (int iteration = 0; iteration < kKeplerIterations; ++iteration)
	{
		const double residual = eccentric - eccentricity * std::sin(eccentric) - mean;
		const double slope = 1.0 - eccentricity * std::cos(eccentric);
		const double step = residual / slope;
		eccentric -= step;
		if (std::abs(step) < kKeplerTolerance)
			break;
	}
	return eccentric;
}

double Phase(double simSeconds, double period)
{
	JETTISON_ASSERT(period > 0.0);
	double phase = std::fmod(simSeconds, period) / period;
	if (phase < 0.0)
		phase += 1.0;
	// A phase just below zero plus one rounds to one.
	return phase < 1.0 ? phase : 0.0;
}

OrbitState EvaluateState(const OrbitalElements &elements, double simSeconds)
{
	const double a = elements.semiMajorAxis;
	if (a == 0.0)
		return {};
	JETTISON_ASSERT(elements.period > 0.0);
	const double e = elements.eccentricity;

	const double mean = elements.meanAnomalyAtEpoch + kTwoPi * Phase(simSeconds, elements.period);
	const double eccentric = SolveKepler(mean, e);
	const double cosE = std::cos(eccentric);
	const double sinE = std::sin(eccentric);
	const double b = a * std::sqrt(1.0 - e * e);
	const double eccentricRate = kTwoPi / elements.period / (1.0 - e * cosE);

	// In the orbit's own plane, periapsis along x.
	const double px = a * (cosE - e);
	const double py = b * sinE;
	const double vx = -a * sinE * eccentricRate;
	const double vy = b * cosE * eccentricRate;

	// Perifocal to the reference frame: Rz(node) Rx(inclination) Rz(periapsis).
	const double cosNode = std::cos(elements.longitudeOfAscendingNode);
	const double sinNode = std::sin(elements.longitudeOfAscendingNode);
	const double cosInc = std::cos(elements.inclination);
	const double sinInc = std::sin(elements.inclination);
	const double cosPeri = std::cos(elements.argumentOfPeriapsis);
	const double sinPeri = std::sin(elements.argumentOfPeriapsis);
	const double xx = cosNode * cosPeri - sinNode * sinPeri * cosInc;
	const double xy = -cosNode * sinPeri - sinNode * cosPeri * cosInc;
	const double yx = sinNode * cosPeri + cosNode * sinPeri * cosInc;
	const double yy = -sinNode * sinPeri + cosNode * cosPeri * cosInc;
	const double zx = sinPeri * sinInc;
	const double zy = cosPeri * sinInc;

	// The astronomical frame's (x, y, z), north along z, is the engine's (x, -z, y): north is +Y
	// and the frame stays right-handed.
	const auto toEngine = [&](double x, double y)
	{ return glm::dvec3{xx * x + xy * y, zx * x + zy * y, -(yx * x + yy * y)}; };
	return {toEngine(px, py), toEngine(vx, vy)};
}

glm::dvec3 Evaluate(const OrbitalElements &elements, double simSeconds)
{
	return EvaluateState(elements, simSeconds).position;
}

glm::dquat Orientation(const Rotation &rotation, double simSeconds)
{
	double angle = rotation.angleAtEpoch;
	if (rotation.period != 0.0)
		angle += std::copysign(kTwoPi, rotation.period) * Phase(simSeconds, std::abs(rotation.period));
	const glm::dquat tilt = glm::angleAxis(rotation.axialTilt, glm::dvec3{1.0, 0.0, 0.0});
	const glm::dquat spin = glm::angleAxis(angle, glm::dvec3{0.0, 1.0, 0.0});
	return tilt * spin;
}
} // namespace Jettison::Celestial
