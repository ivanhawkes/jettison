#pragma once

#include <jettison/geometry/GltfFile.h>

namespace Jettison::Animation
{
// A glTF model file, cooked or not, read and parsed once for both its rig (RigLoader.h) and the
// renderer's geometry, each made from it as a Resource::DerivedLoader. Its loader is registered here
// because the rig's reader is here and render depends on this module, never the other way. Its
// resource is not kept (Loader::keep): it is dropped once the rig and the geometry asked for with it
// are made, so the file is not held in memory beside them.
using ModelFile = Geometry::GltfFile;
} // namespace Jettison::Animation
