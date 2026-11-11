#pragma once

#include <string_view>

namespace Jettison::Scene
{
// The procedural shapes a scene file can name, which the renderer makes in code
// (render/Procedural.h). Each is unit-sized: radius one, and -1 to 1 along an axis that has an
// extent. The file names them as ShapeName spells them.
enum class Shape
{
	Sphere,
	Box,
	IcoSphere,
	CubeSphere,
	Ring,
	Cylinder
};

// "sphere", "box", "icosphere", "cubesphere", "ring" or "cylinder": the shape's name in a scene file.
std::string_view ShapeName(Shape shape);
} // namespace Jettison::Scene
