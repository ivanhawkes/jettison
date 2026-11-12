#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Jettison::Render
{
// The order of a frame's bytes as the swapchain holds them: its format is one of the 8-bit four
// channel ones, and which of red and blue comes first is the surface's choice.
enum class PixelOrder : std::uint8_t
{
	Rgba,
	Bgra
};

// A frame read back from the swapchain as the output pass left it (docs/architecture.md, "The
// frame"): tonemapped and encoded for the display, with the overlay and the dev tools over it. Four
// bytes a pixel, rows from the top, with nothing between them; the fourth byte is the swapchain's
// alpha, which the compositor ignores, and is not written.
struct Screenshot
{
	std::uint32_t width{0};
	std::uint32_t height{0};
	PixelOrder order{PixelOrder::Rgba};
	std::vector<std::uint8_t> pixels;
};

// Writes the frame as an 8-bit RGB PNG, through stb. False, having logged why, when the file
// cannot be written or the frame's size does not match its bytes. A directory in the path is
// made if it is missing.
bool WriteScreenshot(const std::filesystem::path &path, const Screenshot &frame);
} // namespace Jettison::Render
