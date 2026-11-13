#include <jettison/render/ColourGrade.h>

#include <doctest/doctest.h>

#include <cmath>
#include <format>
#include <stdexcept>
#include <string>

#include <jettison/math/Random.h>
#include <jettison/render/PostProcess.h>

using namespace Jettison;
using namespace Jettison::Render;

namespace
{
// A table of size n as a .cube lists it, red fastest, each entry what grade() makes of it.
template <typename Grade> std::string Cube(std::uint32_t size, Grade grade, std::string header = {})
{
	std::string text = header + std::format("LUT_3D_SIZE {}\n", size);
	const auto last = static_cast<float>(size - 1);
	for (std::uint32_t b = 0; b < size; ++b)
		for (std::uint32_t g = 0; g < size; ++g)
			for (std::uint32_t r = 0; r < size; ++r)
			{
				const glm::vec3 value = grade(
				    glm::vec3{
				        static_cast<float>(r) / last, static_cast<float>(g) / last,
				        static_cast<float>(b) / last});
				text += std::format("{} {} {}\n", value.x, value.y, value.z);
			}
	return text;
}

std::string Refusal(const std::string &text)
{
	try
	{
		(void)ReadCubeLut(text, "grades/test.cube");
	}
	catch (const std::runtime_error &error)
	{
		return error.what();
	}
	return {};
}

// What a device's trilinear filter gives at a normalised coordinate into the table as a 3D texture,
// as Vulkan's linear filtering defines it: the texel space's coordinate less a half, between the two
// texels either side of it on each axis, clamped to the edge.
glm::vec3 FilterTexture(const ColourLut &lut, glm::vec3 coordinate)
{
	const auto size = static_cast<float>(lut.size);
	const glm::vec3 texel = coordinate * size - 0.5f;
	const glm::vec3 low = glm::floor(texel);
	const glm::vec3 fraction = texel - low;
	const auto at = [&](glm::vec3 index)
	{
		const glm::uvec3 clamped{glm::clamp(index, glm::vec3{0.0f}, glm::vec3{size - 1.0f})};
		return lut.values[clamped.x + lut.size * (clamped.y + std::size_t{lut.size} * clamped.z)];
	};
	glm::vec3 result{0.0f};
	for (std::uint32_t corner = 0; corner < 8; ++corner)
	{
		const glm::vec3 step{
		    static_cast<float>(corner & 1u), static_cast<float>((corner >> 1) & 1u),
		    static_cast<float>((corner >> 2) & 1u)};
		const glm::vec3 weight = glm::mix(1.0f - fraction, fraction, step);
		result += weight.x * weight.y * weight.z * at(low + step);
	}
	return result;
}
} // namespace

TEST_CASE("Colour grade: a .cube's table is read red fastest, its title and comments as written")
{
	const std::string text = "# A grade written by hand\r\n"
	                         "TITLE \"Swap red and blue\"\r\n"
	                         "DOMAIN_MIN 0 0 0\r\n"
	                         "DOMAIN_MAX 1 1 1\r\n"
	                         "LUT_3D_INPUT_RANGE 0.0 1.0\r\n"
	                         "LUT_3D_SIZE 2\r\n"
	                         "\r\n"
	                         "0 0 0\r\n"
	                         "0 0 1 # red\r\n"
	                         "0 1 0\r\n"
	                         "0 1 1\r\n"
	                         "1 0 0\r\n"
	                         "1 0 1\r\n"
	                         "1 1 0\r\n"
	                         "1 1 1";
	const ColourLut lut = ReadCubeLut(text, "grades/swap.cube");
	CHECK(lut.title == "Swap red and blue");
	REQUIRE(lut.size == 2);
	REQUIRE(lut.values.size() == 8);
	CHECK(lut.values[1] == glm::vec3{0.0f, 0.0f, 1.0f});
	CHECK(lut.values[4] == glm::vec3{1.0f, 0.0f, 0.0f});
	const glm::vec3 graded = SampleLut(lut, {0.25f, 0.5f, 0.75f});
	CHECK(graded.x == doctest::Approx(0.75f));
	CHECK(graded.y == doctest::Approx(0.5f));
	CHECK(graded.z == doctest::Approx(0.25f));
}

TEST_CASE("Colour grade: what a .cube cannot be is refused, naming the file and the line")
{
	const auto identity = [](glm::vec3 colour) { return colour; };
	CHECK(Refusal(Cube(4, identity)).empty());
	CHECK(Refusal("LUT_1D_SIZE 16\n").find("line 1: it is a 1D table") != std::string::npos);
	CHECK(Refusal("TITLE \"nothing\"\n").find("there is no LUT_3D_SIZE") != std::string::npos);
	for (const char *size : {"1", "66", "2.5", "two"})
		CHECK(
		    Refusal(std::format("LUT_3D_SIZE {}\n", size)).find("a whole number from 2 to 65") !=
		    std::string::npos);
	CHECK(
	    Refusal(Cube(2, identity, "LUT_3D_SIZE 2\n")).find("LUT_3D_SIZE is given once") !=
	    std::string::npos);
	CHECK(Refusal("0 0 0\nLUT_3D_SIZE 2\n").find("before LUT_3D_SIZE") != std::string::npos);
	CHECK(Refusal(Cube(2, identity) + "1 1 1\n").find("more entries than the 8") != std::string::npos);
	std::string short_ = Cube(2, identity);
	short_.resize(short_.rfind("1 1 1"));
	CHECK(Refusal(short_).find("7 entries, where a table of size 2 has 8") != std::string::npos);
	CHECK(
	    Refusal("LUT_3D_SIZE 2\n0 0\n").find("line 2: an entry takes three numbers") !=
	    std::string::npos);
	CHECK(
	    Refusal("LUT_3D_SIZE 2\n0 nan 0\n").find("'nan', which is not a finite number") !=
	    std::string::npos);
	CHECK(Refusal("DOMAIN_MAX 1 1 2\n").find("domain is 0 to 1") != std::string::npos);
	CHECK(Refusal("LUT_3D_INPUT_RANGE 0 4\n").find("input range is 0 to 1") != std::string::npos);
	CHECK(Refusal("LUT_FORMAT 3\n").find("'LUT_FORMAT' is not a keyword") != std::string::npos);
	CHECK(Refusal("TITLE untitled\n").find("TITLE takes a quoted title") != std::string::npos);
	CHECK(Refusal("LUT_1D_SIZE 16\n").find("Colour grade 'grades/test.cube'") != std::string::npos);
}

TEST_CASE("Colour grade: the identity leaves a colour as it was, and a grade's ends are clamped")
{
	const ColourLut identity = IdentityLut(17);
	REQUIRE(identity.values.size() == 17u * 17u * 17u);
	Math::Random random{0x11c0u};
	for (int sample = 0; sample < 200; ++sample)
	{
		const glm::vec3 colour{random.NextFloat(), random.NextFloat(), random.NextFloat()};
		const glm::vec3 graded = SampleLut(identity, colour);
		for (int channel = 0; channel < 3; ++channel)
			CHECK(graded[channel] == doctest::Approx(colour[channel]).epsilon(1e-6));
	}
	CHECK(SampleLut(identity, {-0.5f, 1.5f, 1.0f}) == glm::vec3{0.0f, 1.0f, 1.0f});

	// A grade that is linear in the colour is interpolated exactly at any size.
	const ColourLut invert =
	    ReadCubeLut(Cube(5, [](glm::vec3 colour) { return 1.0f - colour; }), "grades/invert.cube");
	const glm::vec3 inverted = SampleLut(invert, {0.1f, 0.6f, 0.93f});
	CHECK(inverted.x == doctest::Approx(0.9f));
	CHECK(inverted.y == doctest::Approx(0.4f));
	CHECK(inverted.z == doctest::Approx(0.07f));
}

TEST_CASE("Colour grade: the output pass's lookup, through a trilinear filter, is the table's grade")
{
	// A grade with no straight lines in it, so a coordinate a texel out would show.
	const auto curve = [](glm::vec3 colour)
	{
		return glm::vec3{
		    colour.x * colour.x, std::sqrt(colour.y), 0.5f + 0.5f * std::sin(6.0f * colour.z)};
	};
	for (const std::uint32_t size : {2u, 17u, 33u})
	{
		const ColourLut lut = ReadCubeLut(Cube(size, curve), "grades/curve.cube");
		Math::Random random{size};
		for (int sample = 0; sample < 200; ++sample)
		{
			const glm::vec3 colour{random.NextFloat(), random.NextFloat(), random.NextFloat()};
			const glm::vec3 sampled =
			    FilterTexture(lut, Post::LutCoordinate(colour, static_cast<float>(size)));
			const glm::vec3 expected = SampleLut(lut, colour);
			for (int channel = 0; channel < 3; ++channel)
				CHECK(sampled[channel] == doctest::Approx(expected[channel]).epsilon(1e-5));
		}
		// The ends of the range are the centres of the first and last texels.
		CHECK(
		    Post::LutCoordinate(glm::vec3{0.0f}, static_cast<float>(size)).x ==
		    doctest::Approx(0.5f / static_cast<float>(size)));
		CHECK(
		    Post::LutCoordinate(glm::vec3{1.0f}, static_cast<float>(size)).x ==
		    doctest::Approx(1.0f - 0.5f / static_cast<float>(size)));
	}
}
