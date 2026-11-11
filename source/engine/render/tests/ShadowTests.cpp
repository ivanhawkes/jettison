// The sun's cascade fit (render/Shadows.h) against what the lighting include does with it: the
// selection and the map lookup are shadows.glsl compiled as C++, so these check the fit against the
// source every lit program runs. The camera is placed where the orbit scene's is, an astronomical
// unit from the system's origin, so the floating origin is part of what is tested.

#include <doctest/doctest.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>

#include <jettison/render/Camera.h>
#include <jettison/render/Shadows.h>

using namespace Jettison::Render;

namespace
{
constexpr float kAspect = 16.0f / 9.0f;
const ShadowSettings kSettings{};

// Where Ostra Highport is, about: an astronomical unit out, plus a low orbit.
const glm::dvec3 kStation{1.496e11, 2.1e6, -6.4e6};
const glm::dvec3 kTowardsSun = glm::normalize(glm::dvec3{-0.8, 0.35, 0.45});

CameraView MakeCamera(const glm::dvec3 &frameOrigin, const glm::dvec3 &offset, double yaw, double pitch)
{
	CameraView camera;
	camera.frameOrigin = frameOrigin;
	camera.position = frameOrigin + offset;
	camera.orientation = OrientationFromAngles(pitch, 0.0, yaw);
	camera.verticalFov = glm::radians(50.0f);
	camera.nearPlane = 0.5f;
	return camera;
}

// A point of the view at a view depth, across its cross-section by the fractions (-1 to 1 each),
// in render space.
glm::vec3 ViewPoint(const CameraView &camera, double depth, double across, double up)
{
	const double tanHalf = std::tan(double{camera.verticalFov} * 0.5);
	const glm::dvec3 view{across * depth * tanHalf * kAspect, up * depth * tanHalf, -depth};
	return glm::vec3{camera.orientation * view};
}

// Where a system position falls in a cascade's map, in texels, through the render-space position
// the GPU would be given.
glm::dvec2 TexelOf(const ShadowCascade &cascade, const CameraView &camera, const glm::dvec3 &system)
{
	const glm::vec3 coordinate =
	    ShadowMapCoordinate(cascade.matrix, ToRenderSpace(system, camera.position));
	return glm::dvec2{coordinate.x, coordinate.y} * double(kSettings.resolution);
}

// How far apart two texel positions' places within their texels are, wrapping, in texels.
double FractionApart(double a, double b)
{
	const double apart = std::abs((a - std::floor(a)) - (b - std::floor(b)));
	return std::min(apart, 1.0 - apart);
}

// Points fixed in the frame, spread over a station's size.
constexpr std::array<glm::dvec3, 5> kStationPoints{{
    {0.0, 0.0, 0.0},
    {44.0, 0.8, 0.0},
    {-56.0, 0.15, 7.0},
    {0.0, 6.0, -34.0},
    {13.3, -11.7, 12.0},
}};
} // namespace

TEST_CASE("Shadows: the splits run from the near plane to the shadow distance")
{
	const auto splits = ShadowSplits(0.5f, kSettings);
	CHECK(splits.back() == kSettings.distance);
	float previous = 0.5f;
	for (const float split : splits)
	{
		CHECK(split > previous);
		previous = split;
	}
	// Between even and logarithmic: the first cascade is far nearer than a quarter of the distance.
	CHECK(splits[0] < kSettings.distance / 4.0f);

	// A distance short of the near plane still leaves the slices some depth.
	ShadowSettings tight = kSettings;
	tight.distance = 0.1f;
	CHECK(ShadowSplits(0.5f, tight).back() == doctest::Approx(1.0f));
}

TEST_CASE("Shadows: each cascade's map holds every point that selects it, with the border to spare")
{
	for (const double yaw : {0.0, 0.8, 2.9})
		for (const double pitch : {-1.2, -0.1, 0.7})
		{
			CAPTURE(yaw);
			CAPTURE(pitch);
			const CameraView camera = MakeCamera(kStation, {120.0, 40.0, -90.0}, yaw, pitch);
			const SunShadows shadows = FitSunShadows(camera, kAspect, kTowardsSun, kSettings);
			const glm::vec4 splits = shadows.Splits();
			// The snap moves the map by up to half a texel, and the border is the filter's and the
			// normal offset's.
			const double margin = (kShadowBorderTexels - 0.5 - 0.01) / kSettings.resolution;
			for (std::uint32_t index = 0; index < kShadowCascades; ++index)
			{
				CAPTURE(index);
				const ShadowCascade &cascade = shadows.cascades[index];
				const double start = ShadowCascadeStart(index, splits, camera.nearPlane);
				for (const double share : {0.0, 0.3, 0.7, 0.999})
					for (const double across : {-1.0, 0.0, 1.0})
						for (const double up : {-1.0, 0.5, 1.0})
						{
							const double depth = start + (cascade.split - start) * share;
							const glm::vec3 point = ViewPoint(camera, depth, across, up);
							const glm::vec3 coordinate = ShadowMapCoordinate(cascade.matrix, point);
							CHECK(coordinate.x >= margin);
							CHECK(coordinate.x <= 1.0 - margin);
							CHECK(coordinate.y >= margin);
							CHECK(coordinate.y <= 1.0 - margin);
							CHECK(coordinate.z > 0.0f);
							CHECK(coordinate.z < 1.0f);
						}
			}
		}
}

TEST_CASE("Shadows: a depth selects its cascade, and a band's depths are inside the next one")
{
	const CameraView camera = MakeCamera(kStation, {0.0, 0.0, 160.0}, 0.0, 0.0);
	const glm::vec4 splits = FitSunShadows(camera, kAspect, kTowardsSun, kSettings).Splits();
	CHECK(ShadowCascadeAt(camera.nearPlane, splits) == 0);
	CHECK(ShadowCascadeAt(kSettings.distance, splits) == kShadowCascades);
	for (std::uint32_t index = 0; index < kShadowCascades; ++index)
	{
		CAPTURE(index);
		const float split = splits[static_cast<int>(index)];
		CHECK(ShadowCascadeAt(split * 0.999f, splits) == index);
		CHECK(ShadowCascadeBlend(split * 0.85f, split) == 0.0f);
		CHECK(ShadowCascadeBlend(split * 0.95f, split) == doctest::Approx(0.5f));
		CHECK(ShadowCascadeBlend(split, split) == 1.0f);
		// A fragment in the band also samples the next cascade, which has to have been fitted from
		// the band's start.
		if (index + 1 < kShadowCascades)
			CHECK(ShadowCascadeStart(index + 1, splits, camera.nearPlane) <= split * 0.9f);
	}
}

TEST_CASE("Shadows: a map is reversed, the sun's side at 1")
{
	const CameraView camera = MakeCamera(kStation, {0.0, 0.0, 160.0}, 0.0, 0.0);
	const ShadowCascade cascade = FitSunShadows(camera, kAspect, kTowardsSun, kSettings).cascades[2];
	const glm::vec3 point = ViewPoint(camera, 100.0, 0.0, 0.0);
	const glm::vec3 sunwards = point + glm::vec3{kTowardsSun} * 10.0f;
	CHECK(
	    ShadowMapCoordinate(cascade.matrix, sunwards).z > ShadowMapCoordinate(cascade.matrix, point).z);
	// Moving along the sun's rays moves nothing across the map.
	CHECK(
	    ShadowMapCoordinate(cascade.matrix, sunwards).x ==
	    doctest::Approx(ShadowMapCoordinate(cascade.matrix, point).x).epsilon(1e-5));
	// Far enough towards the sun, a caster is past the map's near side; the pass clamps it to 1.
	const glm::vec3 overhead = point + glm::vec3{kTowardsSun} * 2000.0f;
	CHECK(ShadowMapCoordinate(cascade.matrix, overhead).z > 1.0f);
}

TEST_CASE("Shadows: turning the camera keeps every cascade's texel size")
{
	const SunShadows ahead = FitSunShadows(
	    MakeCamera(kStation, {0.0, 0.0, 160.0}, 0.0, 0.0), kAspect, kTowardsSun, kSettings);
	const SunShadows turned = FitSunShadows(
	    MakeCamera(kStation, {0.0, 0.0, 160.0}, 1.9, -0.6), kAspect, kTowardsSun, kSettings);
	for (std::uint32_t index = 0; index < kShadowCascades; ++index)
	{
		CAPTURE(index);
		CHECK(turned.cascades[index].texelSize == ahead.cascades[index].texelSize);
		CHECK(turned.cascades[index].radius == ahead.cascades[index].radius);
	}
}

TEST_CASE("Shadows: a point still in the camera's frame stays on the same place in its texel")
{
	// The camera orbits the station and turns, and the station itself moves at orbital speed through
	// the system, a frame at a time; what is fixed to the station has to land at the same place
	// within its texel every time, or its shadow's edges crawl.
	const glm::dvec3 velocity{-7.4e3, 1.1e3, 29.8e3};
	constexpr double kFrame = 1.0 / 60.0;
	const glm::dvec3 offsets[] = {{0.0, 0.0, 160.0}, {37.3, 12.9, 151.1}, {-90.2, 3.3, 20.7}};
	const double yaws[] = {0.0, 0.31, 2.2};
	const double pitches[] = {0.0, -0.05, 0.4};

	const CameraView first = MakeCamera(kStation, offsets[0], yaws[0], pitches[0]);
	const SunShadows reference = FitSunShadows(first, kAspect, kTowardsSun, kSettings);
	for (int frame = 1; frame < 3; ++frame)
	{
		CAPTURE(frame);
		const glm::dvec3 station = kStation + velocity * (kFrame * frame);
		const CameraView camera = MakeCamera(station, offsets[frame], yaws[frame], pitches[frame]);
		const SunShadows shadows = FitSunShadows(camera, kAspect, kTowardsSun, kSettings);
		for (std::uint32_t index = 0; index < kShadowCascades; ++index)
			for (const glm::dvec3 &point : kStationPoints)
			{
				CAPTURE(index);
				const glm::dvec2 before = TexelOf(reference.cascades[index], first, kStation + point);
				const glm::dvec2 after = TexelOf(shadows.cascades[index], camera, station + point);
				// A thousandth of a texel: the float the GPU is given, and nothing else.
				CHECK(FractionApart(before.x, after.x) < 1.0e-3);
				CHECK(FractionApart(before.y, after.y) < 1.0e-3);
			}
	}
}

TEST_CASE("Shadows: a grid fixed to the system would slide under a moving station")
{
	// The case the frame origin is there for: the same motion, measured from the system's origin.
	const glm::dvec3 step = glm::dvec3{-7.4e3, 1.1e3, 29.8e3} / 60.0;
	CameraView first = MakeCamera(kStation, {0.0, 0.0, 160.0}, 0.0, 0.0);
	CameraView next = MakeCamera(kStation + step, {0.0, 0.0, 160.0}, 0.0, 0.0);
	first.frameOrigin = glm::dvec3{0.0};
	next.frameOrigin = glm::dvec3{0.0};
	const ShadowCascade before = FitSunShadows(first, kAspect, kTowardsSun, kSettings).cascades[3];
	const ShadowCascade after = FitSunShadows(next, kAspect, kTowardsSun, kSettings).cascades[3];
	double slid = 0.0;
	for (const glm::dvec3 &point : kStationPoints)
	{
		const glm::dvec2 a = TexelOf(before, first, kStation + point);
		const glm::dvec2 b = TexelOf(after, next, kStation + step + point);
		slid = std::max({slid, FractionApart(a.x, b.x), FractionApart(a.y, b.y)});
	}
	CHECK(slid > 0.05);
}
