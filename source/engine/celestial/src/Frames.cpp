#include <jettison/celestial/Frames.h>

namespace Jettison::Celestial
{
glm::vec3 ToRenderSpace(const SystemPosition &position, const SystemPosition &camera)
{
	return glm::vec3{position.metres - camera.metres};
}

SystemPosition ToSystem(const LocalPosition &local, const SystemPosition &origin)
{
	return {origin.metres + local.metres};
}

LocalPosition ToLocal(const SystemPosition &position, const SystemPosition &origin)
{
	return {position.metres - origin.metres};
}
} // namespace Jettison::Celestial
