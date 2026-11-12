#include <jettison/render/PostProcess.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

#include <jettison/math/Random.h>
#include <jettison/render/Camera.h>
#include <jettison/scene/Components.h>

using namespace Jettison;
using namespace Jettison::Render;

TEST_CASE("Post: a pixel's view position is the point the projection put there")
{
	const glm::vec2 viewport{640.0f, 480.0f};
	const float nearPlane = 0.5f;
	const glm::mat4 projection =
	    ReversedInfinitePerspective(glm::radians(60.0f), 640.0f / 480.0f, nearPlane);
	const glm::vec2 scale{projection[0][0], projection[1][1]};
	for (const glm::vec3 point :
	     {glm::vec3{0.0f, 0.0f, -3.0f}, glm::vec3{4.0f, -2.0f, -20.0f},
	      glm::vec3{-150.0f, 90.0f, -900.0f}})
	{
		const glm::vec4 clip = projection * glm::vec4{point, 1.0f};
		const glm::vec3 ndc = glm::vec3{clip} / clip.w;
		// The pixel whose centre the point lands on, and its depth there.
		const glm::vec2 pixel = (glm::vec2{ndc} * 0.5f + 0.5f) * viewport - 0.5f;
		const glm::vec3 back = Post::ViewPositionAt(pixel, ndc.z, viewport, scale, nearPlane);
		CHECK(back.x == doctest::Approx(point.x).epsilon(1e-4));
		CHECK(back.y == doctest::Approx(point.y).epsilon(1e-4));
		CHECK(back.z == doctest::Approx(point.z).epsilon(1e-4));
	}
	// A metre at ten metres, on 480 lines at 60 degrees: 480 / 2 / tan(30 degrees) / 10 pixels.
	CHECK(
	    Post::ScreenRadius(1.0f, 10.0f, 480.0f, scale.y) ==
	    doctest::Approx(240.0f / std::tan(glm::radians(30.0f)) / 10.0f));
}

namespace
{
// The access of a point on the floor (y = 0, facing up) with a wall along x = wall rising from it,
// sampled as the pass samples it: the spiral's points over the screen land on whichever surface is
// nearer the camera, here looked at from above, so a sample past the wall's foot is on the wall.
float FloorAccess(float x, float wall, float radius)
{
	const glm::vec3 p{x, 0.0f, -10.0f};
	const glm::vec3 n{0.0f, 1.0f, 0.0f};
	float sum = 0.0f;
	for (int index = 0; index < Post::AoSamples(); ++index)
	{
		const glm::vec3 sample = Post::AoSpiralSample(index, 0.3f);
		glm::vec3 q = p + glm::vec3{sample.x, 0.0f, sample.y} * (sample.z * radius);
		// Past the foot of the wall, the surface the camera sees is the wall's face, as high as the
		// sample is far past the foot.
		if (q.x > wall)
			q = {wall, q.x - wall, q.z};
		sum += Post::ObscuranceSample(p, n, q, radius);
	}
	return Post::AmbientAccess(sum, float(Post::AoSamples()), radius, 1.0f);
}
} // namespace

TEST_CASE("Post: a flat surface is not occluded, and a corner more the nearer it is")
{
	const float radius = 2.0f;
	// The wall far off: the floor alone, every sample in its plane.
	CHECK(FloorAccess(0.0f, 100.0f, radius) == doctest::Approx(1.0f));
	const float far = FloorAccess(0.0f, 1.0f, radius);
	const float near = FloorAccess(0.0f, 0.4f, radius);
	const float foot = FloorAccess(0.0f, 0.05f, radius);
	CHECK(far < 1.0f);
	CHECK(near < far);
	CHECK(foot < near);
	CHECK(foot >= 0.0f);
	// A point below the floor's plane, the floor's own quantised depth, is under the bias.
	CHECK(
	    Post::ObscuranceSample(
	        {0.0f, 0.0f, -10.0f}, {0.0f, 1.0f, 0.0f}, {0.3f, 0.01f, -10.0f}, radius) == 0.0f);
	// Nothing past the radius counts.
	CHECK(
	    Post::ObscuranceSample(
	        {0.0f, 0.0f, -10.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 2.5f, -10.0f}, radius) == 0.0f);
}

TEST_CASE("Post: every pixel of a 4 x 4 turns its spiral its own way, and the pattern repeats")
{
	std::set<float> angles;
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
		{
			angles.insert(Post::AoPixelAngle(x, y));
			CHECK(Post::AoPixelAngle(x + 4, y + 8) == Post::AoPixelAngle(x, y));
		}
	CHECK(angles.size() == 16);
	// The spiral's samples reach out from the middle to the radius.
	CHECK(Post::AoSpiralSample(0, 0.0f).z < Post::AoSpiralSample(Post::AoSamples() - 1, 0.0f).z);
	CHECK(Post::AoSpiralSample(Post::AoSamples() - 1, 0.0f).z < 1.0f);
}

TEST_CASE("Post: the blur keeps to a surface, and specular occlusion leaves the open alone")
{
	CHECK(Post::AoBlurWeight(10.0f, 10.0f) == 1.0f);
	CHECK(Post::AoBlurWeight(10.0f, 10.2f) > 0.5f);
	CHECK(Post::AoBlurWeight(10.0f, 11.0f) == 0.0f);
	CHECK(Post::SpecularAccess(0.7f, 1.0f, 0.5f) == doctest::Approx(1.0f));
	CHECK(Post::SpecularAccess(0.7f, 0.0f, 1.0f) == doctest::Approx(0.0f).epsilon(0.01));
	// A mirror square on slips past what darkens a rough surface.
	CHECK(Post::SpecularAccess(1.0f, 0.5f, 0.0f) > Post::SpecularAccess(1.0f, 0.5f, 1.0f));
}

TEST_CASE("Post: the bloom filters keep a flat image, and the first one tames a lone bright pixel")
{
	const glm::vec3 grey{0.4f, 0.5f, 0.6f};
	glm::vec3 down[13];
	glm::vec3 up[9];
	std::fill(std::begin(down), std::end(down), grey);
	std::fill(std::begin(up), std::end(up), grey);
	for (const bool karis : {false, true})
	{
		const glm::vec3 kept = Post::BloomDownsample(down, karis);
		CHECK(kept.x == doctest::Approx(grey.x));
		CHECK(kept.z == doctest::Approx(grey.z));
	}
	CHECK(Post::BloomUpsample(up).y == doctest::Approx(grey.y));
	// The top level of a flat frame holds every level's copy of it, and applying it keeps the frame.
	const glm::vec3 top = grey * float(Post::BloomLevels());
	CHECK(Post::ApplyBloom(grey, top, 0.3f).x == doctest::Approx(grey.x));

	// A star in the middle tap, a thousand times its neighbours.
	down[4] = glm::vec3{400.0f};
	const glm::vec3 plain = Post::BloomDownsample(down, false);
	const glm::vec3 tamed = Post::BloomDownsample(down, true);
	CHECK(tamed.x < plain.x * 0.1f);
	CHECK(tamed.x > grey.x);
}

TEST_CASE("Post: the jump flood finds each pixel's nearest seed, as a search of every seed does")
{
	constexpr int kWidth = 96;
	constexpr int kHeight = 64;
	const glm::uvec2 none{Post::NoSeed()};
	const auto cell = [](int x, int y)
	{ return static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x); };
	std::vector<glm::uvec2> grid(std::size_t{kWidth} * kHeight, none);
	std::vector<glm::uvec2> seeds;
	Math::Random random{7, 0};
	// Two blobs and a scatter, as outlined shapes and their stray pixels would be.
	for (int y = 0; y < kHeight; ++y)
		for (int x = 0; x < kWidth; ++x)
		{
			const bool blob = glm::distance(glm::vec2(x, y), glm::vec2(30, 30)) < 8.0f ||
			                  (x > 60 && x < 70 && y > 10 && y < 50);
			if (blob || random.NextDouble() < 0.002)
			{
				grid[cell(x, y)] = {x, y};
				seeds.emplace_back(x, y);
			}
		}
	for (int step = 0; step < Post::FloodSteps(); ++step)
	{
		const int stride = Post::FloodStride(step);
		std::vector<glm::uvec2> next(grid.size(), none);
		for (int y = 0; y < kHeight; ++y)
			for (int x = 0; x < kWidth; ++x)
			{
				glm::uvec2 best = none;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int sx = x + dx * stride;
						const int sy = y + dy * stride;
						if (sx < 0 || sy < 0 || sx >= kWidth || sy >= kHeight)
							continue;
						best = Post::NearerSeed(glm::uvec2(x, y), best, grid[cell(sx, sy)]);
					}
				next[cell(x, y)] = best;
			}
		grid = std::move(next);
	}
	CHECK(Post::FloodStride(0) == 16);
	CHECK(Post::FloodStride(Post::FloodSteps() - 1) == 1);

	int exact = 0;
	int reached = 0;
	float worst = 0.0f;
	for (int y = 0; y < kHeight; ++y)
		for (int x = 0; x < kWidth; ++x)
		{
			float nearest = std::numeric_limits<float>::max();
			for (const glm::uvec2 seed : seeds)
				nearest = std::min(nearest, glm::distance(glm::vec2(x, y), glm::vec2(seed)));
			if (nearest > 24.0f)
				continue;
			++reached;
			const glm::uvec2 found = grid[cell(x, y)];
			REQUIRE(found.x != Post::NoSeed());
			const float distance = glm::distance(glm::vec2(x, y), glm::vec2(found));
			worst = std::max(worst, distance - nearest);
			if (distance - nearest < 1e-4f)
				++exact;
		}
	// Jump flooding can miss the nearest by a little where two seeds' regions meet; within a glow's
	// widest reach it misses by under a pixel, and almost never at all.
	CHECK(worst < 1.0f);
	CHECK(exact > reached * 99 / 100);
}

TEST_CASE("Post: a glow fades to nothing at its width")
{
	CHECK(Post::GlowFalloff(0.0f, 12.0f) == 1.0f);
	CHECK(Post::GlowFalloff(6.0f, 12.0f) == doctest::Approx(0.25f));
	CHECK(Post::GlowFalloff(12.0f, 12.0f) == 0.0f);
	CHECK(Post::GlowFalloff(30.0f, 12.0f) == 0.0f);
}

TEST_CASE("Post: stencil bits, object IDs and the bloom levels")
{
	CHECK(StencilBits(nullptr, false) == 0);
	CHECK(StencilBits(nullptr, true) == kStencilOutlined);
	const Scene::MaskComponent mask{true, true, 21};
	CHECK(StencilBits(&mask, false) == (kStencilCockpit | kStencilUnprocessed | (21u << 3)));

	// As the prepass writes it, a float, and as the outline passes read it back.
	const std::uint32_t id = ObjectId(1234, 7);
	CHECK((id & 0xffffu) == 1235);
	CHECK(Post::ObjectOutline(static_cast<float>(id)) == 8);
	CHECK(
	    Post::ObjectOutline(static_cast<float>(ObjectId(kMaxObjectIds - 1, kMaxOutlines - 1))) == 255);
	CHECK(Post::ObjectOutline(static_cast<float>(ObjectId(3, kMaxOutlines))) == 0);
	CHECK((ObjectId(kMaxObjectIds, kMaxOutlines) & 0xffffu) == 0);

	const auto extents = BloomExtents({640, 480});
	REQUIRE(extents.size() == std::size_t(Post::BloomLevels()));
	CHECK(extents.front() == glm::uvec2{320, 240});
	CHECK(extents.back() == glm::uvec2{20, 15});
	CHECK(BloomExtents({40, 3}).back() == glm::uvec2{1, 1});
}
