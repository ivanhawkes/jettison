#include <jettison/render/DebugDraw.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using namespace Jettison::Render;

TEST_CASE("Debug draw: shapes become segments, made camera-relative when the frame is built")
{
	DebugDraw draw;
	const glm::dvec3 far{1.496e11, 0.0, 0.0};
	draw.Line(far, far + glm::dvec3{1.0, 0.0, 0.0}, {1.0f, 0.0f, 0.0f, 1.0f});
	draw.Box(far, {1.0, 2.0, 3.0}, {1.0, 0.0, 0.0, 0.0}, {1.0f, 1.0f, 1.0f, 1.0f});
	draw.Axes(far, {1.0, 0.0, 0.0, 0.0}, 5.0, DebugDepth::Overlay);
	draw.Circle(far, {0.0, 1.0, 0.0}, 10.0, {1.0f, 1.0f, 0.0f, 1.0f}, 16);
	CHECK(draw.SegmentCount(DebugDepth::Tested) == 1 + 12 + 16);
	CHECK(draw.SegmentCount(DebugDepth::Overlay) == 3);

	std::vector<LineVertex> vertices;
	const std::uint32_t tested = draw.Build(far, vertices);
	CHECK(tested == 2 * 29);
	REQUIRE(vertices.size() == 2 * 32);
	// The line a metre long a hundred and fifty million kilometres out is a metre long.
	CHECK(vertices[0].position == glm::vec3{0.0f});
	CHECK(vertices[1].position == glm::vec3{1.0f, 0.0f, 0.0f});
	// Every circle point is ten metres from the centre.
	for (std::uint32_t index = 2 + 24; index < tested; ++index)
		CHECK(glm::length(vertices[index].position) == doctest::Approx(10.0f));
	// The overlay comes last: the axes' X arm, in red.
	CHECK(vertices[tested + 1].position == glm::vec3{5.0f, 0.0f, 0.0f});
	CHECK(vertices[tested].color.r == 1.0f);

	draw.Clear();
	CHECK(draw.Empty());
}
