#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include <jettison/animation/Rig.h>

namespace Jettison::Resource
{
class ResourceManager;
}

namespace Jettison::Animation
{
// The rig in a glTF file's bytes: its node hierarchy, the skin on its one mesh node, and its clips.
// A binary .glb, or a .gltf whose buffers are embedded: the bytes are all a resource's decode is
// given, so a buffer in a file beside it cannot be read, and is refused. Throws for a file with no
// nodes or with other than one mesh node, naming it.
//
// The renderer reads the same file for its geometry (render's Model). Each reads what it needs, so
// the rig, which the scene's clip players sample on the CPU, does not wait on the device or depend on
// the module that has one; the cost is the file parsed twice, on the I/O worker.
Rig ReadGltfRig(std::span<const std::byte> bytes, std::string_view name);

// Registers the loader of Animation::Rig, from a model file's virtual path, with the resource
// manager. Once per manager.
void RegisterRigLoader(Resource::ResourceManager &resources);
} // namespace Jettison::Animation
