#pragma once

#include <glm/glm.hpp>

namespace Jettison::Celestial
{
// The coordinate frames of docs/architecture.md, "Coordinate frames and units". A SystemPosition
// is metres from the system's primary in double precision; a LocalPosition is metres from a
// station or planet. ToRenderSpace subtracts the camera's position before converting to float,
// which is the floating origin: precision is always best where the player is looking.
// Filled in phase 4.
struct SystemPosition
{
	glm::dvec3 metres{0.0};
};

struct LocalPosition
{
	glm::dvec3 metres{0.0};
};

glm::vec3 ToRenderSpace(const SystemPosition &position, const SystemPosition &camera);
SystemPosition ToSystem(const LocalPosition &local, const SystemPosition &origin);
LocalPosition ToLocal(const SystemPosition &position, const SystemPosition &origin);
} // namespace Jettison::Celestial
