#include <jettison/render/Screenshot.h>

// STB.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstddef>
#include <system_error>

#include <jettison/core/Log.h>

namespace Jettison::Render
{
namespace
{
constexpr Core::Category kRender{"render"};
}

bool WriteScreenshot(const std::filesystem::path &path, const Screenshot &frame)
{
	const std::size_t pixelCount = std::size_t{frame.width} * frame.height;
	if (pixelCount == 0 || frame.pixels.size() != pixelCount * 4)
	{
		Core::Log(
		    Core::Severity::Error, kRender, "screenshot {}: {} bytes for a frame of {} x {}",
		    path.string(), frame.pixels.size(), frame.width, frame.height);
		return false;
	}
	if (path.has_parent_path())
	{
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		if (error)
		{
			Core::Log(
			    Core::Severity::Error, kRender, "screenshot {}: {}", path.string(), error.message());
			return false;
		}
	}

	const bool bgra = frame.order == PixelOrder::Bgra;
	std::vector<std::uint8_t> rgb(pixelCount * 3);
	for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
	{
		const std::uint8_t *source = &frame.pixels[pixel * 4];
		rgb[pixel * 3 + 0] = source[bgra ? 2 : 0];
		rgb[pixel * 3 + 1] = source[1];
		rgb[pixel * 3 + 2] = source[bgra ? 0 : 2];
	}
	const int written = stbi_write_png(
	    path.string().c_str(), static_cast<int>(frame.width), static_cast<int>(frame.height), 3,
	    rgb.data(), static_cast<int>(frame.width * 3));
	if (written == 0)
	{
		Core::Log(Core::Severity::Error, kRender, "screenshot {}: could not be written", path.string());
		return false;
	}
	Core::Log(
	    Core::Severity::Info, kRender, "screenshot {}: {} x {}", path.string(), frame.width,
	    frame.height);
	return true;
}
} // namespace Jettison::Render
