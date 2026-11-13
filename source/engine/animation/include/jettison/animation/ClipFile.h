#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/animation/Clip.h>

namespace Jettison::Resource
{
class ResourceManager;
}

namespace Jettison::Animation
{
// Clips of keyframed tracks on named nodes, for a mechanical rig made of scene entities: a clamp's
// post, arm and jaw. The scene binds each name to an entity (Scene::ClipPlayerComponent), so one
// file moves every rig built to the same names. A file at animations/<name>.clip.yaml:
//
//   version: 1
//   clips:
//     - name: close
//       tracks:
//         - node: arm
//           path: rotation          # translation, rotation or scale, the node's local one
//           interpolation: linear   # linear (the default), step or cubic
//           times: [0.4, 1.0]
//           values: [[0, 0, 1.5708], [0, 0, 0]]
//       events:
//         - {time: 1.0, name: clamp.closed}
//
// A rotation is in radians about X, then Y, then Z, as the scene's transform turns, and is slerped
// between keys by the shorter way, so keys are less than half a turn apart. A cubic track gives
// three values a key, in-tangent, value and out-tangent, as glTF does, and is translation or scale
// alone: a tangent to a rotation is not a turn about the axes.
struct ClipFile
{
	// The names the tracks are on, in the order first named; a track's node indexes them.
	std::vector<std::string> nodes;
	std::vector<Clip> clips;
};

inline constexpr std::uint32_t kClipFileVersion = 1;

// The file's clips, checked: each named once, each track's node named, its path given once in the
// clip, its times finite, not negative and rising, a value for each (three for a cubic track), each
// event named and its time in the clip. Throws Serialise::FormatError naming the file.
ClipFile ReadClipFile(std::string_view text, std::string_view name);

// The node's index in the file, or the count of its nodes for one it does not name.
std::size_t NodeIndex(const ClipFile &file, std::string_view node);

// Registers the loader of Animation::ClipFile, from its virtual path, with the resource manager.
// The bytes are read on the I/O worker and the YAML parsed on the main thread, since the archive
// reads the reflection tables the main thread registers into. Once per manager.
void RegisterClipFileLoader(Resource::ResourceManager &resources);
} // namespace Jettison::Animation
