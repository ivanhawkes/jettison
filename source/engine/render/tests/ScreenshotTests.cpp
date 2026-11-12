#include <jettison/render/Screenshot.h>

#include <doctest/doctest.h>
#include <stb_image.h>

#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace Jettison::Render;

namespace
{
// A directory of the test's own, removed as it ends.
struct ScratchDirectory
{
	std::filesystem::path path =
	    std::filesystem::temp_directory_path() / ("jettison-screenshot-" + std::to_string(::getpid()));
	~ScratchDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(path, error);
	}
};

// Three by two, every pixel different, in the order given; alpha is a byte no output should show.
Screenshot Frame(PixelOrder order)
{
	const std::vector<std::uint8_t> rgb{255, 0,  0,  0,  255, 0,  0, 0, 255,
	                                    10,  20, 30, 40, 50,  60, 1, 2, 3};
	Screenshot frame{3, 2, order, {}};
	for (std::size_t pixel = 0; pixel < 6; ++pixel)
	{
		const std::uint8_t r = rgb[pixel * 3];
		const std::uint8_t g = rgb[pixel * 3 + 1];
		const std::uint8_t b = rgb[pixel * 3 + 2];
		if (order == PixelOrder::Bgra)
			frame.pixels.insert(frame.pixels.end(), {b, g, r, 77});
		else
			frame.pixels.insert(frame.pixels.end(), {r, g, b, 77});
	}
	return frame;
}
} // namespace

TEST_CASE("a screenshot is written as the RGB of its pixels, top row first, in either order")
{
	ScratchDirectory scratch;
	for (const PixelOrder order : {PixelOrder::Rgba, PixelOrder::Bgra})
	{
		CAPTURE(static_cast<int>(order));
		// In a directory that is not there yet, which is made.
		const std::filesystem::path path = scratch.path / "shots" / "frame.png";
		REQUIRE(WriteScreenshot(path, Frame(order)));

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc *pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 0);
		REQUIRE(pixels != nullptr);
		CHECK(width == 3);
		CHECK(height == 2);
		CHECK(channels == 3);
		const std::vector<std::uint8_t> read(pixels, pixels + 18);
		stbi_image_free(pixels);
		CHECK(
		    read == std::vector<std::uint8_t>{
		                255, 0, 0, 0, 255, 0, 0, 0, 255, 10, 20, 30, 40, 50, 60, 1, 2, 3});
	}
}

TEST_CASE("a screenshot whose bytes do not fill its size is refused")
{
	ScratchDirectory scratch;
	Screenshot frame = Frame(PixelOrder::Rgba);
	frame.pixels.pop_back();
	CHECK_FALSE(WriteScreenshot(scratch.path / "short.png", frame));
	CHECK_FALSE(std::filesystem::exists(scratch.path / "short.png"));
	CHECK_FALSE(WriteScreenshot(scratch.path / "empty.png", Screenshot{}));
}
