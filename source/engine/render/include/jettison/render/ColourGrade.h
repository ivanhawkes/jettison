#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Resource
{
class ResourceManager;
}

namespace Jettison::Render
{
// A colour grade (docs/architecture.md, "Colour grading"): a 3D lookup table from a colour on the
// display to the colour it is graded to, both encoded sRGB, applied by the output pass after the
// tonemap, under the fade and the UI. It is the space a grading tool works in on a screenshot, so a
// frame graded here is that frame's screenshot put through the same table: grade a capture in
// Resolve or Photoshop, export the table as a .cube, and the scene names it
// (Scene::ColourGradeComponent).
//
// The entries are red-fastest, as a .cube lists them: entry r + g n + b n^2 is what the colour (r, g,
// b) / (n - 1) grades to.
struct ColourLut
{
	std::uint32_t size{0};
	std::vector<glm::vec3> values;
	std::string title;
};

// The sizes of table a grade may have: a .cube's smallest, and the largest a grading tool writes.
inline constexpr std::uint32_t kMinLutSize = 2;
inline constexpr std::uint32_t kMaxLutSize = 65;

// A .cube file's 3D table (Adobe's Cube LUT Specification 1.0, as Resolve writes it): a TITLE, its
// LUT_3D_SIZE, the entries, three numbers a line, and comments from a #. A DOMAIN_MIN or
// DOMAIN_MAX, or a LUT_3D_INPUT_RANGE, other than 0 to 1 is refused, as are a 1D table, a size out of
// the range above, a count of entries other than the size cubed, an entry that is not finite, and
// anything else that is not a number. Throws std::runtime_error naming the file and the line.
ColourLut ReadCubeLut(std::string_view text, std::string_view name);

// The table that grades every colour to itself.
ColourLut IdentityLut(std::uint32_t size);

// The table's grade of an encoded colour, clamped to 0 to 1, by trilinear interpolation between its
// eight nearest entries: what the output pass's sampling of it gives, to within the device's
// filtering.
glm::vec3 SampleLut(const ColourLut &lut, glm::vec3 colour);

// Registers the loader of Render::ColourLut, from a .cube's virtual path, with the resource manager:
// the text is read and parsed on the I/O worker, since nothing in it reads the reflection tables.
void RegisterColourLutLoader(Resource::ResourceManager &resources);
} // namespace Jettison::Render
