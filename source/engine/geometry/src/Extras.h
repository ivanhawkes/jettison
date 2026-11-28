#pragma once

#include <string_view>

namespace Jettison::Geometry
{
// The cooked GLB's own extras, written by the cook (Cook.cpp) and read by GltfFile, so both spell them
// from here. A mesh's: {"jettison": {"levels": [{"primitives": 2, "error": 0}, ...]}}, its levels
// finest first, each the next run of that many primitives. The default scene's:
// {"jettison": {"cookKey": "..."}}. fastgltf 1.0.1 writes no asset or primitive extras, which is why
// they sit on the mesh and the scene.
inline constexpr std::string_view kExtrasObject = "jettison";

inline constexpr std::string_view kLevelsName = "levels";

inline constexpr std::string_view kPrimitivesName = "primitives";

inline constexpr std::string_view kErrorName = "error";

inline constexpr std::string_view kCookKeyName = "cookKey";
} // namespace Jettison::Geometry
