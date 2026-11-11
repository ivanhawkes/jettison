// The cluster grid and the local lights' reach, through render/Clusters.h, which is clusters.glsl
// compiled as C++: the source the cluster programs and the lighting include run. The grid is
// checked against the projection the renderer draws with, and the light tests against brute force:
// points sampled through a cluster's own pixels and depths, lit or not by the light's definition
// rather than by the test under scrutiny.

#include <jettison/render/Clusters.h>

#include <doctest/doctest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include <jettison/math/Random.h>
#include <jettison/render/Camera.h>
#include <jettison/scene/Components.h>

using namespace Jettison::Render;
using doctest::Approx;
using Jettison::Scene::LightComponent;
using Jettison::Scene::LightType;

namespace
{
// A 640 x 480 view, as the smoke test opens, through the renderer's projection.
ClusterView MakeView(float nearPlane = 0.5f, float range = 2000.0f)
{
	const glm::mat4 projection =
	    ReversedInfinitePerspective(glm::radians(50.0f), 640.0f / 480.0f, nearPlane);
	ClusterView view;
	view.viewport = {640.0f, 480.0f};
	view.projectionScale = {projection[0][0], projection[1][1]};
	view.nearPlane = nearPlane;
	view.range = range;
	return view;
}

// A point in a cluster: a pixel in its tile and a depth in its slice, both drawn at random, and the
// view-space point there by the projection's own inverse.
glm::vec3 SampleCluster(std::uint32_t cluster, const ClusterView &view, Jettison::Math::Random &random)
{
	const glm::vec2 tile = ClusterTileSize(view.viewport);
	const std::uint32_t x = cluster % kClusterTilesX;
	const std::uint32_t y = (cluster / kClusterTilesX) % kClusterTilesY;
	const std::uint32_t slice = cluster / (kClusterTilesX * kClusterTilesY);
	const glm::vec2 pixel{
	    (static_cast<float>(x) + random.NextFloat()) * tile.x,
	    (static_cast<float>(y) + random.NextFloat()) * tile.y};
	const float nearDepth = ClusterSliceDepth(slice, view.nearPlane, view.range);
	const float farDepth = ClusterSliceDepth(slice + 1, view.nearPlane, view.range);
	const float depth = nearDepth + (farDepth - nearDepth) * random.NextFloat();
	const glm::vec2 ndc = pixel / view.viewport * 2.0f - 1.0f;
	return {ndc.x * depth / view.projectionScale.x, ndc.y * depth / view.projectionScale.y, -depth};
}

// Whether a light lights a render-space point at all, by its definition: inside its range, and for a
// spot inside its outer cone.
bool Lights(const GpuLight &light, const glm::vec3 &point)
{
	const glm::vec3 toPoint = point - glm::vec3{light.positionRange};
	const float distance = glm::length(toPoint);
	if (distance >= light.positionRange.w)
		return false;
	if (light.directionType.w == 0.0f || distance == 0.0f)
		return true;
	return glm::dot(toPoint / distance, glm::vec3{light.directionType}) > light.cone.z;
}

// The assignment as the GPU lays it out, made on the CPU: each cluster's lights in order, one after
// another, the allocator's count first.
struct Assignment
{
	std::vector<glm::uvec2> grid;
	std::vector<std::uint32_t> indices;
};

Assignment Assign(
    const ClusterView &view, const glm::mat3 &rotation, const std::vector<GpuLight> &lights)
{
	Assignment assignment;
	assignment.grid.resize(kClusterCount);
	for (std::uint32_t cluster = 0; cluster < kClusterCount; ++cluster)
	{
		const ClusterBox bounds = ClusterBoundsOf(cluster, view);
		const auto offset = static_cast<std::uint32_t>(assignment.indices.size());
		for (std::uint32_t light = 0; light < lights.size(); ++light)
			if (LightReachesCluster(lights[light], rotation, bounds))
				assignment.indices.push_back(light);
		assignment.grid[cluster] = {
		    offset, static_cast<std::uint32_t>(assignment.indices.size()) - offset};
	}
	return assignment;
}

GpuLight Spot(
    const glm::vec3 &position, const glm::vec3 &direction, float range, float inner, float outer)
{
	LightComponent light;
	light.type = LightType::Spot;
	light.range = range;
	light.innerConeAngle = inner;
	light.outerConeAngle = outer;
	return PackLight(light, position, glm::normalize(direction));
}

GpuLight Point(const glm::vec3 &position, float range)
{
	LightComponent light;
	light.range = range;
	return PackLight(light, position, glm::vec3{0.0f, 0.0f, -1.0f});
}
} // namespace

TEST_CASE("Clusters: the slices are exponential in depth, from the near plane to the cluster range")
{
	const ClusterView view = MakeView();
	CHECK(ClusterSliceDepth(0, view.nearPlane, view.range) == Approx(view.nearPlane));
	CHECK(ClusterSliceDepth(kClusterSlices, view.nearPlane, view.range) == Approx(view.range));
	const float ratio = ClusterSliceDepth(1, view.nearPlane, view.range) / view.nearPlane;
	for (std::uint32_t slice = 0; slice < kClusterSlices; ++slice)
	{
		CAPTURE(slice);
		const float nearDepth = ClusterSliceDepth(slice, view.nearPlane, view.range);
		const float farDepth = ClusterSliceDepth(slice + 1, view.nearPlane, view.range);
		CHECK(farDepth / nearDepth == Approx(ratio).epsilon(1e-4));
		CHECK(ClusterSlice(std::sqrt(nearDepth * farDepth), view.nearPlane, view.range) == slice);
	}
	// Outside the grid: no cluster, so no local light.
	CHECK(ClusterSlice(0.4f, view.nearPlane, view.range) == kClusterSlices);
	CHECK(ClusterSlice(view.range, view.nearPlane, view.range) == kClusterSlices);
	CHECK(ClusterAt({10.0f, 10.0f}, 5000.0f, view) == kClusterCount);
	// The last tile reaches the edge, and a pixel on it is in the last column and row: 480 pixels in
	// 18 rows is 26 2/3 a row, rounded up.
	CHECK(ClusterTileSize(view.viewport) == glm::vec2{20.0f, 27.0f});
	CHECK(ClusterAt({639.5f, 479.5f}, 1.0f, view) % kClusterTilesX == kClusterTilesX - 1);
}

TEST_CASE("Clusters: a cluster's bounds hold every point the projection draws in its tile and slice")
{
	const ClusterView view = MakeView();
	const glm::mat4 projection =
	    ReversedInfinitePerspective(glm::radians(50.0f), 640.0f / 480.0f, 0.5f);
	Jettison::Math::Random random{17};
	for (int sample = 0; sample < 20000; ++sample)
	{
		const glm::vec2 pixel{random.Range(0.0f, 640.0f), random.Range(0.0f, 480.0f)};
		const float depth = view.nearPlane * std::pow(view.range / view.nearPlane, random.NextFloat());
		const std::uint32_t cluster = ClusterAt(pixel, depth, view);
		REQUIRE(cluster < kClusterCount);

		// Where the projection puts the point the bounds were built from: on the same pixel, at
		// the same depth, so the bounds and the fragment's lookup agree on what a cluster is.
		const glm::vec2 ndc = pixel / view.viewport * 2.0f - 1.0f;
		const glm::vec3 point{
		    ndc.x * depth / view.projectionScale.x, ndc.y * depth / view.projectionScale.y, -depth};
		const glm::vec4 clip = projection * glm::vec4{point, 1.0f};
		CHECK((clip.x / clip.w + 1.0f) * 0.5f * view.viewport.x == Approx(pixel.x).epsilon(1e-3));
		CHECK((clip.y / clip.w + 1.0f) * 0.5f * view.viewport.y == Approx(pixel.y).epsilon(1e-3));

		const ClusterBox bounds = ClusterBoundsOf(cluster, view);
		const float slack = 1e-4f * depth;
		CAPTURE(cluster);
		CHECK(glm::all(glm::greaterThanEqual(point, bounds.minimum - slack)));
		CHECK(glm::all(glm::lessThanEqual(point, bounds.maximum + slack)));
	}
}

TEST_CASE("Clusters: a light that lights any point of a cluster is assigned to it")
{
	const ClusterView view = MakeView(0.5f, 400.0f);
	// A turned view, so the test sees the lights move from render space into view space.
	const glm::mat3 rotation{
	    glm::rotate(glm::mat4{1.0f}, 0.7f, glm::normalize(glm::vec3{0.3f, 1.0f, -0.2f}))};
	const glm::mat3 toRender = glm::transpose(rotation);
	Jettison::Math::Random random{29};
	int lit = 0;
	int reached = 0;
	for (int trial = 0; trial < 400; ++trial)
	{
		const auto cluster =
		    static_cast<std::uint32_t>(random.Range(0, static_cast<std::int32_t>(kClusterCount) - 1));
		const ClusterBox bounds = ClusterBoundsOf(cluster, view);
		const glm::vec3 inside = SampleCluster(cluster, view, random);
		// Lights near the cluster, often enough just reaching it, and a spot aimed roughly at it.
		const float range = random.Range(0.5f, 2.0f) * glm::length(bounds.maximum - bounds.minimum);
		const glm::vec3 offset{
		    random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f)};
		const glm::vec3 position = toRender * (inside + offset * range);
		const glm::vec3 aim =
		    toRender * (inside - (inside + offset * range)) +
		    glm::vec3{random.Range(-0.5f, 0.5f), random.Range(-0.5f, 0.5f), random.Range(-0.5f, 0.5f)} *
		        range;
		const GpuLight light = trial % 2 == 0
		                           ? Point(position, range)
		                           : Spot(position, aim, range, 0.1f, random.Range(0.15f, 1.5f));
		const bool reaches = LightReachesCluster(light, rotation, bounds);
		reached += reaches ? 1 : 0;

		bool lightsCluster = false;
		for (int sample = 0; sample < 400 && !lightsCluster; ++sample)
			lightsCluster = Lights(light, toRender * SampleCluster(cluster, view, random));
		lit += lightsCluster ? 1 : 0;
		CAPTURE(trial);
		CAPTURE(cluster);
		if (lightsCluster)
			CHECK(reaches);
	}
	// The trials are set up to straddle the edge: some light their cluster and some do not.
	CHECK(lit > 50);
	CHECK(reached < 400);
}

TEST_CASE("Clusters: a light far from a cluster, or facing away from it, is not assigned to it")
{
	const ClusterView view = MakeView();
	const glm::mat3 identity{1.0f};
	// The cluster in the middle of the view, about ten metres out.
	const std::uint32_t cluster = ClusterAt({320.0f, 240.0f}, 10.0f, view);
	const ClusterBox bounds = ClusterBoundsOf(cluster, view);
	const glm::vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
	CHECK(LightReachesCluster(Point(center, 0.1f), identity, bounds));
	CHECK_FALSE(
	    LightReachesCluster(Point(center + glm::vec3{0.0f, 20.0f, 0.0f}, 5.0f), identity, bounds));
	// A spot five metres to the side: aimed at the cluster it reaches it, aimed away it does not.
	const glm::vec3 beside = center + glm::vec3{5.0f, 0.0f, 0.0f};
	CHECK(LightReachesCluster(Spot(beside, center - beside, 8.0f, 0.1f, 0.4f), identity, bounds));
	CHECK_FALSE(LightReachesCluster(Spot(beside, beside - center, 8.0f, 0.1f, 0.4f), identity, bounds));
	CHECK_FALSE(LightReachesCluster(
	    Spot(beside, glm::vec3{0.0f, 1.0f, 0.0f}, 8.0f, 0.1f, 0.4f), identity, bounds));
}

TEST_CASE("Clusters: the falloff is the inverse square near a light, and reaches zero at its range")
{
	constexpr float kRange = 20.0f;
	for (const float distance : {0.5f, 1.0f, 2.0f})
		CHECK(
		    RangeAttenuation(distance * distance, kRange) ==
		    Approx(1.0f / (distance * distance)).epsilon(1e-3));
	CHECK(RangeAttenuation(kRange * kRange, kRange) == 0.0f);
	CHECK(RangeAttenuation(4.0f * kRange * kRange, kRange) == 0.0f);
	// Smooth into zero, and never rising with distance.
	float previous = RangeAttenuation(0.0f, kRange);
	for (int step = 1; step <= 200; ++step)
	{
		const float distance = kRange * static_cast<float>(step) / 200.0f;
		const float attenuation = RangeAttenuation(distance * distance, kRange);
		CHECK(attenuation <= previous);
		previous = attenuation;
	}
	CHECK(RangeAttenuation(0.99f * 0.99f * kRange * kRange, kRange) < 1e-5f);
	// Inside the bulb the inverse square stops climbing, so a surface at the light stays finite.
	CHECK(RangeAttenuation(0.0f, kRange) == Approx(400.0f));
	CHECK(RangeAttenuation(1e-6f, kRange) == RangeAttenuation(0.0f, kRange));
}

TEST_CASE(
    "Clusters: a spot is full inside its inner cone and dark outside its outer, a point light "
    "everywhere")
{
	const GpuLight spot = Spot({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 10.0f, 0.2f, 0.5f);
	const auto at = [](float angle) { return glm::vec3{std::sin(angle), 0.0f, -std::cos(angle)}; };
	CHECK(SpotAttenuation(spot, at(0.0f)) == Approx(1.0f));
	CHECK(SpotAttenuation(spot, at(0.19f)) == Approx(1.0f));
	CHECK(SpotAttenuation(spot, at(0.51f)) == 0.0f);
	const float between = SpotAttenuation(spot, at(0.35f));
	CHECK(between > 0.0f);
	CHECK(between < 1.0f);
	CHECK(spot.cone.z == Approx(std::cos(0.5f)));
	CHECK(spot.directionType.w == 1.0f);

	const GpuLight point = Point({0.0f, 0.0f, 0.0f}, 10.0f);
	CHECK(point.directionType.w == 0.0f);
	for (const float angle : {0.0f, 1.5f, 3.0f})
		CHECK(SpotAttenuation(point, at(angle)) == 1.0f);
}

TEST_CASE("Clusters: the check passes an assignment that agrees, and names one that does not")
{
	const ClusterView view = MakeView(0.5f, 500.0f);
	const glm::mat3 rotation{glm::rotate(glm::mat4{1.0f}, 0.3f, glm::vec3{0.0f, 1.0f, 0.0f})};
	const std::vector<GpuLight> lights{
	    Point({0.0f, 0.0f, -10.0f}, 6.0f),
	    Spot({3.0f, 2.0f, -20.0f}, {0.0f, -1.0f, 0.2f}, 15.0f, 0.2f, 0.6f),
	    Point({-40.0f, 5.0f, -120.0f}, 30.0f)};
	Assignment assignment = Assign(view, rotation, lights);
	const auto reserved = static_cast<std::uint32_t>(assignment.indices.size());
	const ClusterCheck agrees =
	    CheckClusterAssignment(view, rotation, lights, assignment.grid, assignment.indices, reserved);
	CHECK(agrees.Agrees());
	CHECK(agrees.litClusters > 10);
	CHECK(agrees.entries == reserved);

	// A cluster that lost its light.
	std::uint32_t lit = 0;
	while (assignment.grid[lit].y == 0)
		++lit;
	Assignment missing = assignment;
	--missing.grid[lit].y;
	const ClusterCheck dropped =
	    CheckClusterAssignment(view, rotation, lights, missing.grid, missing.indices, reserved);
	CHECK_FALSE(dropped.Agrees());
	CHECK(dropped.mismatch.find("does not list light") != std::string::npos);

	// One that lists a light that cannot reach it: the first lit cluster is a few metres out, and the
	// last light's range ends ninety metres short of it. Its entries move to the end of the list,
	// with that light after them.
	Assignment wrong = assignment;
	const glm::uvec2 entry = wrong.grid[lit];
	const auto moved = static_cast<std::uint32_t>(wrong.indices.size());
	for (std::uint32_t index = entry.x; index < entry.x + entry.y; ++index)
		wrong.indices.push_back(wrong.indices[index]);
	wrong.indices.push_back(2);
	wrong.grid[lit] = {moved, entry.y + 1};
	const ClusterCheck extra = CheckClusterAssignment(
	    view, rotation, lights, wrong.grid, wrong.indices,
	    static_cast<std::uint32_t>(wrong.indices.size()));
	CHECK_FALSE(extra.Agrees());
	CHECK(extra.mismatch.find("lists light 2, which does not reach it") != std::string::npos);

	// And an index list that ran out.
	const ClusterCheck overflowed = CheckClusterAssignment(
	    view, rotation, lights, assignment.grid, assignment.indices, reserved + 1);
	CHECK(overflowed.overflowed);
	CHECK_FALSE(overflowed.Agrees());
}
