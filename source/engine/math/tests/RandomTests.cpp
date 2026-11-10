#include <jettison/math/Random.h>

#include <doctest/doctest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>

using namespace Jettison::Math;

// The values pcg32-demo prints for its default seed, 42 on stream 54, published with the
// reference implementation (pcg-c-basic) and checked by other ports against it. If this fails,
// the generator is no longer PCG32.
TEST_CASE("Random: the PCG32 reference implementation's published output")
{
	Random random{42, 54};
	constexpr std::array<std::uint32_t, 6> expected{0xa15c02b7, 0x7b47f409, 0xba1d3330,
	                                                0x83d2f293, 0xbfa4784b, 0xcbed606e};
	for (const std::uint32_t value : expected)
		CHECK(random.NextU32() == value);
}

// The distributions are the engine's own, so there is no published output for them: these were
// recorded from this implementation on 2026-11-10, and are compared bit for bit. A failure here
// means a save or a replay recorded before the change would now play out differently.
TEST_CASE("Random: the distributions' recorded output")
{
	Random random{2026};
	CHECK(random.NextU64() == 0xbbc743b4e9f40ea7ULL);
	CHECK(random.NextU64() == 0x2828adaf608c60dcULL);
	CHECK(random.NextU64() == 0x664f25dbd2508253ULL);
	CHECK(random.NextU64() == 0x6f29e58a4f6346d7ULL);

	for (const std::uint32_t bits : {0x3f7bc968U, 0x3df6e760U, 0x3f6204e8U, 0x3d84bb78U})
		CHECK(std::bit_cast<std::uint32_t>(random.NextFloat()) == bits);
	for (const std::uint64_t bits :
	     {0x3fdc2731b3bb3e96ULL, 0x3fc74a2e0e79d8e0ULL, 0x3fefc101169d8ff1ULL, 0x3fa8dc517b1e5080ULL})
		CHECK(std::bit_cast<std::uint64_t>(random.NextDouble()) == bits);
	for (const std::int32_t value : {3, 2, 4, 4, 3, 6, 6, 5})
		CHECK(random.Range(1, 6) == value);
	for (const std::uint32_t bits : {0x3e97d528U, 0xbf79b3bcU, 0x3dcb1500U, 0x3ee1a0a4U})
		CHECK(std::bit_cast<std::uint32_t>(random.Range(-1.0f, 1.0f)) == bits);
	CHECK(random.State() == RandomState{0x36cd274214cc26b0ULL, 1});

	Random wide{7};
	constexpr auto lowest = std::numeric_limits<std::int32_t>::min();
	constexpr auto highest = std::numeric_limits<std::int32_t>::max();
	for (const std::int32_t value : {-231132847, 2143014202, -1554810161, -909489089})
		CHECK(wide.Range(lowest, highest) == value);
	for (const std::int32_t value : {651485, -633993, -138168, 15115})
		CHECK(wide.Range(-1000000, 1000000) == value);
}

TEST_CASE("Random: a seed and stream decide the sequence")
{
	Random a{99};
	Random b{99};
	Random otherStream{99, 1};
	Random otherSeed{100};
	bool streamDiffers = false;
	bool seedDiffers = false;
	for (int draw = 0; draw < 16; ++draw)
	{
		const std::uint32_t value = a.NextU32();
		CHECK(b.NextU32() == value);
		streamDiffers = streamDiffers || otherStream.NextU32() != value;
		seedDiffers = seedDiffers || otherSeed.NextU32() != value;
	}
	CHECK(streamDiffers);
	CHECK(seedDiffers);
}

TEST_CASE("Random: a restored state carries on where the saved one was")
{
	Random original{5, 3};
	for (int draw = 0; draw < 10; ++draw)
		original.NextU32();
	Random restored = Random::FromState(original.State());
	CHECK(restored.State() == original.State());
	for (int draw = 0; draw < 10; ++draw)
		CHECK(restored.NextU64() == original.NextU64());
}

TEST_CASE("Random: unit floats stay in [0, 1)")
{
	Random random{11};
	for (int draw = 0; draw < 10000; ++draw)
	{
		const float single = random.NextFloat();
		CHECK(single >= 0.0f);
		CHECK(single < 1.0f);
		const double precise = random.NextDouble();
		CHECK(precise >= 0.0);
		CHECK(precise < 1.0);
		const float ranged = random.Range(-2.0f, 3.0f);
		CHECK(ranged >= -2.0f);
		CHECK(ranged <= 3.0f);
	}
}

TEST_CASE("Random: integer ranges reach both ends, and nothing outside them")
{
	Random random{13};
	CHECK(random.Range(7, 7) == 7);

	bool sawMin = false;
	bool sawMax = false;
	for (int draw = 0; draw < 10000; ++draw)
	{
		const std::int32_t value = random.Range(-3, 3);
		CHECK(value >= -3);
		CHECK(value <= 3);
		sawMin = sawMin || value == -3;
		sawMax = sawMax || value == 3;
	}
	CHECK(sawMin);
	CHECK(sawMax);
}

// A die rolled 60,000 times: each face's count has a standard deviation of about 91, so five
// percent either side of 10,000 is over five deviations. A biased range would sit well outside.
TEST_CASE("Random: integer ranges are uniform")
{
	Random random{17};
	std::array<int, 6> counts{};
	for (int draw = 0; draw < 60000; ++draw)
		++counts[static_cast<std::size_t>(random.Range(0, 5))];
	for (const int count : counts)
	{
		CHECK(count > 9500);
		CHECK(count < 10500);
	}
}
