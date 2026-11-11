#include <jettison/scene/Shape.h>

namespace Jettison::Scene
{
std::string_view ShapeName(Shape shape)
{
	switch (shape)
	{
		case Shape::Sphere: return "sphere";
		case Shape::Box: return "box";
		case Shape::IcoSphere: return "icosphere";
		case Shape::CubeSphere: return "cubesphere";
		case Shape::Ring: return "ring";
		case Shape::Cylinder: return "cylinder";
	}
	return "sphere";
}
} // namespace Jettison::Scene
