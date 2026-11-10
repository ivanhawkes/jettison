#include <jettison/math/Noise.h>

#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <utility>

using namespace Jettison::Math;

namespace
{
std::uint32_t Bits(float value)
{
	return std::bit_cast<std::uint32_t>(value);
}
} // namespace

// Recorded from FastNoiseLite v1.1.1 through this wrapper on 2026-11-10, compared bit for bit.
// Generated content is reproduced from its seed, so a library update that moves these moves
// every generated world: it is a decision, not a version bump.
TEST_CASE("Noise: recorded output")
{
	const Noise noise{1337};
	CHECK(Bits(noise.Sample(glm::vec2{0.25f, 0.5f})) == 0x3f3a6846U);
	CHECK(Bits(noise.Sample(glm::vec2{10.5f, -3.25f})) == 0x3f0ea528U);
	CHECK(Bits(noise.Sample(glm::vec3{0.25f, 0.5f, 0.75f})) == 0x3eaa22dcU);
	CHECK(Bits(noise.Sample(glm::vec3{-7.5f, 2.0f, 100.125f})) == 0x3d860017U);
}

TEST_CASE("Noise: in [-1, 1], and different for a different seed")
{
	const Noise noise{1};
	const Noise other{2};
	bool differs = false;
	for (float x = -8.0f; x <= 8.0f; x += 0.37f)
	{
		for (float y = -8.0f; y <= 8.0f; y += 0.41f)
		{
			const float value = noise.Sample(glm::vec3{x, y, 0.5f});
			CHECK(value >= -1.0f);
			CHECK(value <= 1.0f);
			differs = differs || other.Sample(glm::vec3{x, y, 0.5f}) != value;
		}
	}
	CHECK(differs);
}

TEST_CASE("Noise: a copy, and a moved-to value, sample as the original did")
{
	const Noise original{42};
	const glm::vec2 point{3.5f, -1.25f};
	Noise copy = original;
	CHECK(copy.Seed() == 42);
	CHECK(copy.Sample(point) == original.Sample(point));

	Noise assigned{7};
	assigned = copy;
	CHECK(assigned.Sample(point) == original.Sample(point));

	Noise moved = std::move(copy);
	CHECK(moved.Sample(point) == original.Sample(point));
}
