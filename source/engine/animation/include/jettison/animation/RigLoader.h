#pragma once

#include <jettison/animation/ModelFile.h>
#include <jettison/animation/Rig.h>

namespace Jettison::Resource
{
class ResourceManager;
}

namespace Jettison::Animation
{
// The rig in a parsed glTF file: its node hierarchy, the skin on its one mesh node, and its clips.
// Throws for a file with no nodes or with other than one mesh node, naming it. Read on the CPU, so
// the rig, which the scene's clip players sample, does not wait on the device or depend on the module
// that has one.
Rig ReadGltfRig(const ModelFile &file);

// Registers the loaders of a model file's parse (ModelFile) and of Animation::Rig, made from it, by
// the model file's virtual path, with the resource manager. Once per manager, and before a renderer
// loads a model, since its geometry is made from the same parse.
void RegisterRigLoader(Resource::ResourceManager &resources);
} // namespace Jettison::Animation
