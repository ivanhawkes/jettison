// atmosphere.glsl compiled as C++ (render/Atmosphere.h), against the same single scattering
// integrated by brute force in double precision: the ray in a few thousand even steps, and the
// sunlight's way to each of them, and the column of air, in steps of their own, with nothing of the
// Chapman function's approximation.

#include <jettison/render/Atmosphere.h>
#include <jettison/scene/Components.h>

#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <cmath>
#include <numbers>
#include <string>

using namespace Jettison;
using namespace Jettison::Render;

namespace
{
constexpr double kEarthRadius = 6.371e6;

struct Air
{
	glm::dvec3 rayleigh;
	double rayleighHeight;
	double mie;
	double mieExtinction;
	double mieHeight;
	double anisotropy;
	double top;
};

Air AirOf(const Scene::AtmosphereComponent &component)
{
	return {glm::dvec3{component.rayleigh}, component.rayleighHeight, component.mie,
	        component.mieExtinction,        component.mieHeight,      component.mieAnisotropy,
	        kEarthRadius + component.height};
}

// The column of air of one kind from a point along a direction to the top, in metres at the surface's
// density, by the midpoint rule; infinite where the ray meets the ground.
double ColumnByBruteForce(
    glm::dvec3 from, glm::dvec3 direction, double scaleHeight, double top, int steps)
{
	const double along = glm::dot(from, direction);
	const double c = glm::dot(from, from) - top * top;
	const double leave = -along + std::sqrt(along * along - c);
	double column = 0.0;
	const double step = leave / steps;
	for (int index = 0; index < steps; ++index)
	{
		const double radius = glm::length(from + direction * ((index + 0.5) * step));
		if (radius < kEarthRadius)
			return INFINITY;
		column += std::exp((kEarthRadius - radius) / scaleHeight) * step;
	}
	return column;
}

struct Reference
{
	glm::dvec3 inscattered{0.0};
	glm::dvec3 transmittance{1.0};
};

// The ray from a camera at a point from the body's centre, through the shell to the ground or out.
Reference TraceByBruteForce(const Air &air, glm::dvec3 camera, glm::dvec3 direction, glm::dvec3 toSun)
{
	const double along = glm::dot(camera, direction);
	const double nearest = glm::length(camera - direction * along);
	if (nearest >= air.top)
		return {};
	const double half = std::sqrt(air.top * air.top - nearest * nearest);
	const double enter = std::max(-along - half, 0.0);
	double leave = -along + half;
	if (nearest < kEarthRadius)
	{
		const double ground = -along - std::sqrt(kEarthRadius * kEarthRadius - nearest * nearest);
		if (ground > 0.0)
			leave = std::min(leave, ground);
	}
	constexpr int kSteps = 3000;
	const double step = (leave - enter) / kSteps;
	const double cosine = glm::dot(direction, toSun);
	const double rayleighPhase = 3.0 / (16.0 * std::numbers::pi) * (1.0 + cosine * cosine);
	const double g = air.anisotropy;
	const double mieDenominator = 1.0 + g * g - 2.0 * g * cosine;
	const double miePhase = 3.0 / (8.0 * std::numbers::pi) * (1.0 - g * g) * (1.0 + cosine * cosine) /
	                        ((2.0 + g * g) * mieDenominator * std::sqrt(mieDenominator));
	glm::dvec2 columns{0.0};
	Reference reference;
	glm::dvec3 rayleighSum{0.0};
	glm::dvec3 mieSum{0.0};
	for (int index = 0; index < kSteps; ++index)
	{
		const glm::dvec3 point = camera + direction * (enter + (index + 0.5) * step);
		const double height = glm::length(point) - kEarthRadius;
		const double rayleighDensity = std::exp(-height / air.rayleighHeight);
		const double mieDensity = std::exp(-height / air.mieHeight);
		// To the middle of this step, from the entry.
		const glm::dvec2 toHere = columns + glm::dvec2{rayleighDensity, mieDensity} * (0.5 * step);
		columns += glm::dvec2{rayleighDensity, mieDensity} * step;
		const glm::dvec3 toCamera = glm::exp(-(air.rayleigh * toHere.x + air.mieExtinction * toHere.y));
		const double sunRayleigh = ColumnByBruteForce(point, toSun, air.rayleighHeight, air.top, 600);
		const double sunMie = ColumnByBruteForce(point, toSun, air.mieHeight, air.top, 600);
		const glm::dvec3 sunlight =
		    std::isinf(sunRayleigh)
		        ? glm::dvec3{0.0}
		        : glm::exp(-(air.rayleigh * sunRayleigh + air.mieExtinction * sunMie));
		rayleighSum += toCamera * sunlight * (rayleighDensity * step);
		mieSum += toCamera * sunlight * (mieDensity * step);
	}
	reference.inscattered = air.rayleigh * rayleighSum * rayleighPhase + air.mie * mieSum * miePhase;
	reference.transmittance = glm::exp(-(air.rayleigh * columns.x + air.mieExtinction * columns.y));
	return reference;
}

// Within a share of the reference, or within a small floor for what is all but nothing.
void CheckClose(const glm::vec3 &value, const glm::dvec3 &reference, double share, double floor)
{
	for (int channel = 0; channel < 3; ++channel)
	{
		CAPTURE(channel);
		CAPTURE(value[channel]);
		CAPTURE(reference[channel]);
		CHECK(std::abs(value[channel] - reference[channel]) <= share * reference[channel] + floor);
	}
}

glm::dvec3 Direction(double x, double y, double z)
{
	return glm::normalize(glm::dvec3{x, y, z});
}

// From a camera over the body's +Y pole, the way across the limb whose lowest point is the height
// given over the ground.
glm::dvec3 Limb(const glm::dvec3 &camera, double height)
{
	const double sine = (kEarthRadius + height) / glm::length(camera);
	return {sine, -std::sqrt(1.0 - sine * sine), 0.0};
}
} // namespace

TEST_CASE("the Chapman column matches the column of air integrated along the ray")
{
	constexpr double kScaleHeight = 8000.0;
	constexpr double kTop = kEarthRadius + 30.0 * kScaleHeight;
	for (const double height : {0.0, 2000.0, 20000.0, 60000.0})
		for (const double mu : {1.0, 0.7, 0.3, 0.1, 0.02, 0.0, -0.02, -0.05})
		{
			CAPTURE(height);
			CAPTURE(mu);
			const double radius = kEarthRadius + height;
			const glm::dvec3 from{0.0, radius, 0.0};
			const glm::dvec3 direction{std::sqrt(1.0 - mu * mu), mu, 0.0};
			const double reference = ColumnByBruteForce(from, direction, kScaleHeight, kTop, 200000);
			if (std::isinf(reference))
				continue;
			const float column = AtmosphereColumn(
			    static_cast<float>(radius), static_cast<float>(mu), static_cast<float>(kEarthRadius),
			    static_cast<float>(kScaleHeight));
			// Schüler's own measure is a few per cent at worst, grazing.
			CHECK(column == doctest::Approx(reference).epsilon(0.03));
		}
}

TEST_CASE("the sun's light through an atmosphere is reddened towards the horizon and gone below it")
{
	const Scene::AtmosphereComponent earth;
	const GpuAtmosphere atmosphere = PackAtmosphere(earth, glm::vec3{0.0f}, kEarthRadius);
	const glm::vec3 ground{0.0f, static_cast<float>(kEarthRadius), 0.0f};
	const glm::vec3 overhead = AtmosphereSunTransmittance(atmosphere, ground, {0.0f, 1.0f, 0.0f});
	const glm::vec3 low =
	    AtmosphereSunTransmittance(atmosphere, ground, glm::normalize(glm::vec3{1.0f, 0.05f, 0.0f}));
	// Straight down, Earth's air takes about a quarter of the blue and a twentieth of the red.
	CHECK(
	    overhead.z == doctest::Approx(std::exp(-(33.1e-6 * 8000.0 + 4.44e-6 * 1200.0))).epsilon(1e-3));
	CHECK(overhead.x > overhead.y);
	CHECK(overhead.y > overhead.z);
	CHECK(low.x < overhead.x);
	// Low in the sky the red comes through and the blue does not.
	CHECK(low.x > 4.0f * low.z);
	// Below the horizon, seen from 10 km up: the ray to it meets the ground.
	const glm::vec3 high{0.0f, static_cast<float>(kEarthRadius + 10000.0), 0.0f};
	CHECK(
	    AtmosphereSunTransmittance(atmosphere, high, glm::normalize(glm::vec3{1.0f, -0.1f, 0.0f})) ==
	    glm::vec3{0.0f});
}

TEST_CASE(
    "a ray through an atmosphere picks up the light single scattering integrated by brute force does")
{
	const Scene::AtmosphereComponent earth;
	const Air air = AirOf(earth);
	struct Case
	{
		const char *name;
		glm::dvec3 camera;
		glm::dvec3 direction;
		glm::dvec3 toSun;
		// Measured on 2026-11-12 with 8 samples a half: within 1.7% of the reference from orbit, and
		// 7.6% from the ground under the sun, where the haze's glow about it is 1.2 km deep along a ray
		// of 100 km (1.5% at 16 samples). The camera is in orbit, so the samples are not doubled.
		double inscatteredShare{0.025};
	};
	// From orbit 400 km up, as the station sees the planet, and from the ground.
	const glm::dvec3 orbit{0.0, kEarthRadius + 400000.0, 0.0};
	const glm::dvec3 ground{0.0, kEarthRadius + 10.0, 0.0};
	const Case cases[] = {
	    {"straight down at noon", orbit, {0.0, -1.0, 0.0}, {0.0, 1.0, 0.0}},
	    {"down into the morning", orbit, {0.0, -1.0, 0.0}, Direction(1.0, 0.2, 0.0)},
	    // At the limb, the ray's lowest point 10 km and then 30 km up, lit from the side and from
	    // behind the camera; and just over the ground, against the sun.
	    {"the limb lit from the side", orbit, Limb(orbit, 10000.0), Direction(0.0, 0.0, 1.0)},
	    {"the limb higher, lit from behind", orbit, Limb(orbit, 30000.0), Direction(-1.0, 0.3, 0.2)},
	    {"the limb against the sun", orbit, Limb(orbit, 2000.0), Direction(1.0, 0.0, 0.0)},
	    {"the ground's sky at noon", ground, Direction(0.3, 1.0, 0.0), {0.0, 1.0, 0.0}, 0.09},
	    {"the ground's sky at sunset", ground, Direction(0.0, 0.2, 1.0), Direction(1.0, 0.03, 0.0)},
	};
	for (const Case &ray : cases)
	{
		INFO(std::string{ray.name});
		// The body's centre in render space, where the camera is at the origin.
		const GpuAtmosphere atmosphere =
		    PackAtmosphere(earth, glm::vec3{-ray.camera}, static_cast<float>(kEarthRadius));
		const glm::dvec3 direction = glm::normalize(ray.direction);
		const glm::dvec3 toSun = glm::normalize(ray.toSun);
		const AtmosphereSample traced =
		    TraceAtmosphere(atmosphere, glm::vec3{direction}, 1.0e12f, glm::vec3{toSun});
		const Reference reference = TraceByBruteForce(air, ray.camera, direction, toSun);
		CheckClose(traced.transmittance, reference.transmittance, 0.03, 1e-4);
		CheckClose(traced.inscattered, reference.inscattered, ray.inscatteredShare, 1e-6);
	}
}

TEST_CASE("a ray stopped short by a surface picks up only the air in front of it")
{
	const Scene::AtmosphereComponent earth;
	const glm::dvec3 camera{0.0, kEarthRadius + 400000.0, 0.0};
	const GpuAtmosphere atmosphere =
	    PackAtmosphere(earth, glm::vec3{-camera}, static_cast<float>(kEarthRadius));
	const glm::vec3 down{0.0f, -1.0f, 0.0f};
	const glm::vec3 sun{0.0f, 1.0f, 0.0f};
	// Above the shell, a surface 300 km below the camera has no air in front of it.
	const AtmosphereSample above = TraceAtmosphere(atmosphere, down, 300000.0f, sun);
	CHECK(above.inscattered == glm::vec3{0.0f});
	CHECK(above.transmittance == glm::vec3{1.0f});
	// One that is 1 km above the ground has nearly all the air in front of it that the ground does.
	const AtmosphereSample ground = TraceAtmosphere(atmosphere, down, 1.0e12f, sun);
	const AtmosphereSample short_ = TraceAtmosphere(atmosphere, down, 399000.0f, sun);
	CHECK(short_.inscattered.z < ground.inscattered.z);
	CHECK(short_.inscattered.z > 0.8f * ground.inscattered.z);
	CHECK(short_.transmittance.z > ground.transmittance.z);
	// A body with no atmosphere passes everything.
	GpuAtmosphere none = atmosphere;
	none.shell.y = 0.0f;
	CHECK(TraceAtmosphere(none, down, 1.0e12f, sun).transmittance == glm::vec3{1.0f});
}

namespace
{
// A Saturn-sized body at the origin, its rings in the XZ plane from 1.24 to 2.27 of its radius.
constexpr float kGiantRadius = 6.0e7f;

GpuRings GiantRings(std::uint32_t seed = 3)
{
	Scene::RingsComponent rings;
	rings.innerRadius = 1.24 * kGiantRadius;
	rings.outerRadius = 2.27 * kGiantRadius;
	rings.seed = seed;
	return PackRings(rings, {0.0f, 1.0f, 0.0f});
}
} // namespace

TEST_CASE("rings cover nothing outside their radii, and no more than their opacity within them")
{
	const GpuRings rings = GiantRings();
	CHECK(RingCoverage(rings, 1.2f * kGiantRadius, 0.0f) == 0.0f);
	CHECK(RingCoverage(rings, 2.3f * kGiantRadius, 0.0f) == 0.0f);
	float densest = 0.0f;
	float thinnest = 1.0f;
	float total = 0.0f;
	int samples = 0;
	for (float t = 0.08f; t < 0.97f; t += 0.001f)
	{
		const float radius = (1.24f + t * (2.27f - 1.24f)) * kGiantRadius;
		const float coverage = RingCoverage(rings, radius, 0.0f);
		CHECK(coverage >= 0.0f);
		CHECK(coverage <= 0.8f);
		densest = std::max(densest, coverage);
		thinnest = std::min(thinnest, coverage);
		total += coverage;
		++samples;
	}
	// Banded: some of them near the densest, some all but empty, the gap among them.
	CHECK(densest > 0.6f);
	CHECK(thinnest < 0.1f);
	CHECK(total / static_cast<float>(samples) > 0.2f);

	// Another seed draws other bands; and from afar, where a pixel spans a tenth of the width, every
	// octave but the first is its mean, so the bands are wide.
	const GpuRings other = GiantRings(11);
	int differing = 0;
	float roughness = 0.0f;
	float smoothness = 0.0f;
	for (float t = 0.1f; t < 0.5f; t += 0.002f)
	{
		const float radius = (1.24f + t * (2.27f - 1.24f)) * kGiantRadius;
		const float step = 0.002f * (2.27f - 1.24f) * kGiantRadius;
		if (std::abs(RingCoverage(rings, radius, 0.0f) - RingCoverage(other, radius, 0.0f)) > 0.05f)
			++differing;
		roughness +=
		    std::abs(RingCoverage(rings, radius + step, 0.0f) - RingCoverage(rings, radius, 0.0f));
		smoothness +=
		    std::abs(RingCoverage(rings, radius + step, 0.1f) - RingCoverage(rings, radius, 0.1f));
	}
	CHECK(differing > 50);
	CHECK(smoothness < 0.5f * roughness);
}

TEST_CASE("rings shade the body where they are between it and the sun, and the body shades them")
{
	const GpuRings rings = GiantRings();
	const glm::vec3 centre{0.0f};
	// A ray from above the plane straight down meets it at its own distance and radius.
	const RingCrossing crossing =
	    IntersectRings(rings, centre, {1.5f * kGiantRadius, 1.0e7f, 0.0f}, {0.0f, -1.0f, 0.0f});
	CHECK(crossing.distance == doctest::Approx(1.0e7f));
	CHECK(crossing.radius == doctest::Approx(1.5f * kGiantRadius));
	CHECK(IntersectRings(rings, centre, {0.0f, 1.0e7f, 0.0f}, {0.0f, 1.0f, 0.0f}).distance < 0.0f);

	// The sun 30 degrees above the plane: a point on the body's southern hemisphere under the rings
	// is in their shadow, one on the northern is not, and neither is one whose way to the sun
	// passes inside the inner edge.
	const glm::vec3 toSun = glm::normalize(glm::vec3{1.0f, 0.5773503f, 0.0f});
	float darkest = 1.0f;
	for (float latitude = -0.9f; latitude < -0.1f; latitude += 0.01f)
	{
		const glm::vec3 point = kGiantRadius * glm::vec3{std::cos(latitude), std::sin(latitude), 0.0f};
		darkest = std::min(darkest, RingSunlight(rings, centre, point, toSun));
	}
	CHECK(darkest < 0.5f);
	CHECK(RingSunlight(rings, centre, kGiantRadius * glm::vec3{0.6f, 0.8f, 0.0f}, toSun) == 1.0f);
	CHECK(RingSunlight(rings, centre, kGiantRadius * glm::vec3{1.0f, 0.0f, 0.0f}, toSun) == 1.0f);

	// The body's shadow on the rings: behind it from the sun, dark; beside it, lit; and its edge a
	// penumbra as wide as the sun is from there.
	const glm::vec4 body{centre, kGiantRadius};
	const glm::vec3 sideways{1.0f, 0.0f, 0.0f};
	const float sun = 0.0047f;
	CHECK(SphereSunlight(body, {-1.5f * kGiantRadius, 0.0f, 0.0f}, sideways, sun) == 0.0f);
	CHECK(SphereSunlight(body, {0.0f, 0.0f, 1.5f * kGiantRadius}, sideways, sun) == 1.0f);
	const float edge = SphereSunlight(body, {-1.5f * kGiantRadius, 0.0f, kGiantRadius}, sideways, sun);
	CHECK(edge == doctest::Approx(0.5f).epsilon(0.05));
}

TEST_CASE("the rings' shadow is softened by how much of them a pixel's way to the sun sweeps")
{
	const GpuRings rings = GiantRings();
	const glm::vec3 centre{0.0f};
	// The sun three degrees above the plane: a point a little south of the equator looks across the
	// rings at a grazing angle, and its neighbours a pixel away look at other bands.
	const glm::vec3 toSun = glm::normalize(glm::vec3{1.0f, 0.0524f, 0.0f});
	float sharpSpread = 0.0f;
	float softSpread = 0.0f;
	float previousSharp = -1.0f;
	float previousSoft = -1.0f;
	for (float latitude = -0.06f; latitude < -0.02f; latitude += 0.0005f)
	{
		const glm::vec3 point = kGiantRadius * glm::vec3{std::cos(latitude), std::sin(latitude), 0.0f};
		const float sharp = RingSunlight(rings, centre, point, toSun);
		// A pixel half a per cent of the radius across, as Corva is from 300,000 km at 960 by 540.
		const float soft = RingSunlight(rings, centre, point, toSun, 0.005f * kGiantRadius);
		if (previousSharp >= 0.0f)
		{
			sharpSpread += std::abs(sharp - previousSharp);
			softSpread += std::abs(soft - previousSoft);
		}
		previousSharp = sharp;
		previousSoft = soft;
	}
	CHECK(softSpread < 0.5f * sharpSpread);
}
