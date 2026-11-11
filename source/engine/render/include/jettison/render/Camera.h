#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Jettison::Render
{
// Where a camera is and where it looks: a system-frame position in double precision and the
// camera-to-system rotation. The camera looks down its local -Z with +Y up and +X to the right,
// as the scene's right-handed, Y-up convention has it (CameraComponent). The vertical field of
// view is in radians.
struct CameraView
{
	glm::dvec3 position{0.0};
	glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
	float verticalFov{0.785398163f};
	float nearPlane{0.1f};
	// The origin of the local frame the camera moves in (docs/architecture.md, "Coordinate frames
	// and units"), in the system frame: the station or body an orbit camera circles, or the system's
	// origin. The sun's shadow maps keep their texel grid still in this frame (render/Shadows.h).
	glm::dvec3 frameOrigin{0.0};
};

// The scene file's camera angles as a rotation: pitch about X, then yaw about Y, the view
// direction (-sin yaw cos pitch, sin pitch, -cos yaw cos pitch), and roll about that direction.
// The same convention the renderer has read the scene with since the camera was added.
glm::dquat OrientationFromAngles(double pitch, double roll, double yaw);

// Reversed-Z with an infinite far plane, for Vulkan's clip space (y down, depth from 0 to 1): a
// point on the near plane has depth 1, and depth falls towards 0 as the distance grows without
// bound, as near / distance. A float depth buffer keeps its precision where depth is near 0,
// which is where reversing puts the far distances, so a cockpit at half a metre and a planet at
// forty thousand kilometres share one buffer (docs/architecture.md, "Coordinate frames and
// units"). Depth is cleared to 0 and tested with GREATER.
glm::mat4 ReversedInfinitePerspective(float verticalFov, float aspect, float nearPlane);

// The view matrix in render space: the camera is at the origin there, so the view is the inverse
// of its rotation and nothing else.
glm::mat4 RenderViewMatrix(const CameraView &camera);

// A world transform made camera-relative: the camera's position is taken off the translation in
// double precision, and only then does the matrix become a float. The floating origin, as
// Celestial::ToRenderSpace is for a point.
glm::mat4 ToRenderSpace(const glm::dmat4 &world, const glm::dvec3 &camera);
glm::vec3 ToRenderSpace(const glm::dvec3 &position, const glm::dvec3 &camera);
} // namespace Jettison::Render
