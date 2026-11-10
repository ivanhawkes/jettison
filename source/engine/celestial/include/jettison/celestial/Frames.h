#pragma once

#include <glm/glm.hpp>

namespace Jettison::Celestial
{
// The coordinate frames of docs/architecture.md, "Coordinate frames and units". A SystemPosition
// is metres from the system's primary in double precision; a LocalPosition is metres from a
// station or planet, on axes parallel to the system frame's (a local frame translates with its
// origin and does not rotate with it).
struct SystemPosition
{
	glm::dvec3 metres{0.0};
};

struct LocalPosition
{
	glm::dvec3 metres{0.0};
};

// The floating origin: the camera's position is subtracted in double precision and only the
// difference becomes a float, so precision is always best where the player is looking. A float
// keeps 24 bits: floats a kilometre from the camera are 61 micrometres apart, and floats a hundred
// million kilometres from the primary, where converting both positions first would put them, are
// eight kilometres apart.
glm::vec3 ToRenderSpace(const SystemPosition &position, const SystemPosition &camera);
SystemPosition ToSystem(const LocalPosition &local, const SystemPosition &origin);
LocalPosition ToLocal(const SystemPosition &position, const SystemPosition &origin);
} // namespace Jettison::Celestial
