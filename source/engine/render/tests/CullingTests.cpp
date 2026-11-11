// What the frame leaves out (render/Culling.h), against brute force: a sphere the frustum test drops
// has no point inside the projection's clip volume, one a planet's horizon hides has every point
// behind the planet along the line of sight, and a caster a cascade leaves out has no point in the
// prism its map shadows. Each is also checked to leave out what it should, so a test that kept
// everything would fail too.

#include <doctest/doctest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include <jettison/math/Random.h>
#include <jettison/render/Camera.h>
#include <jettison/render/Culling.h>
#include <jettison/render/Shadows.h>

using namespace Jettison::Render;

namespace
{
constexpr double kAspect = 16.0 / 9.0;

// Points spread evenly over a sphere's surface (a Fibonacci lattice), and its centre.
std::vector<glm::dvec3> SurfacePoints(const BoundingSphere &sphere, int count = 400)
{
	std::vector<glm::dvec3> points{sphere.center};
	const double golden = std::numbers::pi * (3.0 - std::sqrt(5.0));
	for (int index = 0; index < count; ++index)
	{
		const double y = 1.0 - 2.0 * (index + 0.5) / count;
		const double across = std::sqrt(1.0 - y * y);
		const double angle = golden * index;
		points.push_back(
		    sphere.center +
		    sphere.radius * glm::dvec3{std::cos(angle) * across, y, std::sin(angle) * across});
	}
	return points;
}

// And through its volume, on concentric shells.
std::vector<glm::dvec3> VolumePoints(const BoundingSphere &sphere)
{
	std::vector<glm::dvec3> points;
	for (const double shell : {0.25, 0.5, 0.75, 1.0})
		for (const glm::dvec3 &point : SurfacePoints({sphere.center, sphere.radius * shell}, 200))
			points.push_back(point);
	return points;
}

CameraView RandomCamera(Jettison::Math::Random &random)
{
	CameraView camera;
	camera.orientation = OrientationFromAngles(
	    random.NextDouble() * 2.0 - 1.0, random.NextDouble() - 0.5, random.NextDouble() * 6.28);
	camera.verticalFov = glm::radians(random.Range(30.0f, 70.0f));
	camera.nearPlane = random.Range(0.1f, 1.0f);
	return camera;
}

bool InsideClipVolume(const CameraView &camera, const glm::dvec3 &point)
{
	const glm::dmat4 projection{
	    ReversedInfinitePerspective(camera.verticalFov, static_cast<float>(kAspect), camera.nearPlane)};
	const glm::dmat4 view{glm::mat4_cast(glm::conjugate(camera.orientation))};
	const glm::dvec4 clip = projection * view * glm::dvec4{point, 1.0};
	// Reversed: depth is 1 on the near plane and falls towards 0 at infinity.
	return clip.w > 0.0 && std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && clip.z <= clip.w;
}

// Whether the line of sight from the camera to a point meets the sphere before the point does.
bool HiddenBy(const BoundingSphere &occluder, const glm::dvec3 &point)
{
	const double length = glm::length(point);
	const glm::dvec3 direction = point / length;
	const double along = glm::dot(occluder.center, direction);
	const double missSquared = glm::dot(occluder.center, occluder.center) - along * along;
	const double radiusSquared = occluder.radius * occluder.radius;
	if (missSquared > radiusSquared)
		return false;
	const double entry = along - std::sqrt(radiusSquared - missSquared);
	return entry > 0.0 && entry < length;
}
} // namespace

TEST_CASE("Culling: a sphere the frustum leaves out has no point inside the view")
{
	Jettison::Math::Random random{5};
	int culled = 0;
	int kept = 0;
	for (int trial = 0; trial < 2000; ++trial)
	{
		const CameraView camera = RandomCamera(random);
		const Frustum frustum = ViewFrustum(camera, kAspect);
		const BoundingSphere sphere{
		    glm::dvec3{
		        random.Range(-60.0f, 60.0f), random.Range(-60.0f, 60.0f), random.Range(-60.0f, 60.0f)},
		    double{random.Range(0.1f, 15.0f)}};
		if (Intersects(frustum, sphere))
		{
			++kept;
			continue;
		}
		++culled;
		for (const glm::dvec3 &point : VolumePoints(sphere))
		{
			CAPTURE(trial);
			REQUIRE_FALSE(InsideClipVolume(camera, point));
		}
	}
	// The view covers a fraction of the space around the camera, so most spheres are left out.
	CHECK(culled > 1000);
	CHECK(kept > 100);
}

TEST_CASE("Culling: a sphere whose centre is in view is kept, whatever its size")
{
	Jettison::Math::Random random{6};
	for (int trial = 0; trial < 500; ++trial)
	{
		const CameraView camera = RandomCamera(random);
		const glm::dvec3 forward = camera.orientation * glm::dvec3{0.0, 0.0, -1.0};
		const BoundingSphere sphere{
		    forward * double{random.Range(1.0f, 1.0e7f)}, double{random.Range(0.0f, 1.0f)}};
		CHECK(Intersects(ViewFrustum(camera, kAspect), sphere));
	}
	// Behind the camera, and clear of the near plane, nothing is.
	const CameraView camera;
	CHECK_FALSE(Intersects(ViewFrustum(camera, kAspect), {{0.0, 0.0, 5.0}, 1.0}));
	CHECK(Intersects(ViewFrustum(camera, kAspect), {{0.0, 0.0, 5.0}, 6.0}));
}

TEST_CASE("Culling: what a planet's horizon hides is behind the planet along every line of sight")
{
	Jettison::Math::Random random{7};
	int hidden = 0;
	for (int trial = 0; trial < 3000; ++trial)
	{
		// An Earth-sized planet some way below, and spheres around and beyond it.
		const double radius = 6.371e6;
		const glm::dvec3 axis = glm::normalize(
		    glm::dvec3{
		        random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f)});
		const HorizonOccluder planet{{axis * (radius + double{random.Range(1.0e3f, 2.0e6f)}), radius}};
		const glm::dvec3 offset{
		    random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f)};
		const BoundingSphere sphere{
		    planet.sphere.center * double{random.Range(0.5f, 2.5f)} + offset * 8.0e6,
		    double{random.Range(1.0f, 2.0e6f)}};
		if (!Occludes(planet, sphere))
			continue;
		++hidden;
		for (const glm::dvec3 &point : SurfacePoints(sphere))
		{
			CAPTURE(trial);
			REQUIRE(HiddenBy(planet.sphere, point));
		}
	}
	CHECK(hidden > 200);
}

TEST_CASE("Culling: a station over the planet is hidden from the far side, and not from above")
{
	const double radius = 6.371e6;
	// The camera 400 km up; a station as high on the far side of the planet, and one beside the camera.
	const HorizonOccluder planet{{{0.0, -(radius + 4.0e5), 0.0}, radius}};
	const BoundingSphere farSide{{0.0, -2.0 * (radius + 4.0e5), 0.0}, 100.0};
	const BoundingSphere beside{{200.0, 0.0, 0.0}, 100.0};
	// How far the horizon is from the camera.
	const double horizon = std::sqrt((radius + 4.0e5) * (radius + 4.0e5) - radius * radius);
	CHECK(Occludes(planet, farSide));
	CHECK_FALSE(Occludes(planet, beside));
	// The planet's own bounds are never hidden by itself, and a camera inside it hides nothing.
	CHECK_FALSE(Occludes(planet, {planet.sphere.center, radius * 1.001}));
	CHECK_FALSE(Occludes({{{0.0, 0.0, 0.0}, 10.0}}, farSide));
	// A sphere straddling the horizon's plane is not wholly behind it.
	const glm::dvec3 tangent = glm::normalize(glm::dvec3{radius, -horizon, 0.0});
	CHECK_FALSE(Occludes(planet, {tangent * horizon * 1.5, 1.0e5}));
}

TEST_CASE("Culling: a caster a cascade leaves out has no point in the prism its map shadows")
{
	CameraView camera;
	camera.position = {1.496e11, 2.1e6, -6.4e6};
	camera.frameOrigin = camera.position;
	camera.orientation = OrientationFromAngles(-0.2, 0.0, 0.7);
	camera.verticalFov = glm::radians(50.0f);
	camera.nearPlane = 0.5f;
	const glm::dvec3 towardsSun = glm::normalize(glm::dvec3{-0.8, 0.35, 0.45});
	const SunShadows shadows =
	    FitSunShadows(camera, static_cast<float>(kAspect), towardsSun, ShadowSettings{});

	Jettison::Math::Random random{8};
	int culled = 0;
	int kept = 0;
	for (const ShadowCascade &cascade : shadows.cascades)
		for (int trial = 0; trial < 1500; ++trial)
		{
			const double reach = 4.0 * cascade.radius;
			const BoundingSphere sphere{
			    glm::dvec3{
			        random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f)} *
			        reach,
			    double{random.Range(0.01f, 0.5f)} * cascade.radius};
			if (CastsInto(cascade, sphere))
			{
				++kept;
				continue;
			}
			++culled;
			for (const glm::dvec3 &point : VolumePoints(sphere))
			{
				const glm::dvec4 map = glm::dmat4{cascade.matrix} * glm::dvec4{point, 1.0};
				CAPTURE(trial);
				REQUIRE_FALSE((std::abs(map.x) <= 1.0 && std::abs(map.y) <= 1.0 && map.z >= 0.0));
			}
		}
	CHECK(culled > 1000);
	CHECK(kept > 200);

	// A planet beside a cascade, out of the sun's way, is left out of it; the same planet between the
	// sun and the cascade is in it, as is a caster however far sunwards, and not one beyond its far
	// side.
	const double radius = 6.371e6;
	const glm::dvec3 across = glm::normalize(glm::cross(towardsSun, glm::dvec3{0.0, 1.0, 0.0}));
	for (const ShadowCascade &cascade : shadows.cascades)
	{
		const glm::dvec3 center{
		    glm::inverse(glm::dmat4{cascade.matrix}) * glm::dvec4{0.0, 0.0, 0.5, 1.0}};
		CHECK_FALSE(CastsInto(
		    cascade, {center - towardsSun * (radius * 0.5) + across * (radius + 4.0e5), radius}));
		CHECK(CastsInto(cascade, {center + towardsSun * (radius + 4.0e5), radius}));
		CHECK(CastsInto(cascade, {center + towardsSun * 1.0e9, 1.0}));
		CHECK_FALSE(CastsInto(cascade, {center - towardsSun * 1.0e4, 1.0}));
	}
}

TEST_CASE("Culling: a level of detail is the coarsest whose projected error is under the threshold")
{
	CameraView camera;
	camera.verticalFov = glm::radians(60.0f);
	camera.nearPlane = 0.5f;
	const LodSelection selection = MakeLodSelection(camera, 1080.0, 1.0);
	// 1080 pixels over twice tan(30 degrees).
	CHECK(selection.pixelsPerRadian == doctest::Approx(1080.0 / (2.0 * std::tan(glm::radians(30.0)))));

	const std::vector<ModelLod> lods{{0, 1, 0.0f}, {1, 1, 0.001f}, {2, 1, 0.004f}, {3, 1, 0.016f}};
	// An error of e metres at distance d covers e * pixelsPerRadian / d pixels: the level whose error
	// times the scale is under d / pixelsPerRadian.
	const double scale = 10.0;
	for (const double distance : {1.0, 10.0, 50.0, 100.0, 500.0, 5000.0})
	{
		const BoundingSphere sphere{{0.0, 0.0, -(distance + 10.0)}, 10.0};
		const std::uint32_t chosen = SelectLod(lods, scale, sphere, selection);
		CAPTURE(distance);
		CHECK(ProjectedError(lods[chosen].error * scale, sphere, selection) <= 1.0 + 1e-9);
		if (chosen + 1 < lods.size())
			CHECK(ProjectedError(lods[chosen + 1].error * scale, sphere, selection) > 1.0);
	}
	// From inside the bounds, the distance is the near plane's, and the finest level is drawn.
	CHECK(SelectLod(lods, scale, {{0.0, 0.0, 0.0}, 10.0}, selection) == 0u);
	// Far enough off, the coarsest.
	CHECK(SelectLod(lods, scale, {{0.0, 0.0, -1.0e6}, 10.0}, selection) == 3u);
}

TEST_CASE("Culling: bounds through a transform keep every vertex inside")
{
	const ModelBounds bounds{{0.5f, -0.25f, 0.0f}, 2.0f};
	glm::dmat4 transform{1.0};
	transform = glm::translate(transform, glm::dvec3{1.0e7, -3.0, 4.0});
	transform = glm::rotate(transform, 0.7, glm::normalize(glm::dvec3{1.0, 2.0, 3.0}));
	transform = glm::scale(transform, glm::dvec3{30.0, 6.0, 6.0});
	const BoundingSphere sphere = TransformBounds(bounds, transform);
	CHECK(sphere.radius == doctest::Approx(60.0));
	CHECK(MaxScale(transform) == doctest::Approx(30.0));
	CHECK(MinScale(transform) == doctest::Approx(6.0));
	for (const glm::dvec3 &local : SurfacePoints({glm::dvec3{bounds.center}, double{bounds.radius}}))
	{
		const glm::dvec3 world{transform * glm::dvec4{local, 1.0}};
		CHECK(glm::length(world - sphere.center) <= sphere.radius * (1.0 + 1e-9));
	}
}
