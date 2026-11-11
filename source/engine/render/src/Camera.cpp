#include <jettison/render/Camera.h>

#include <cmath>

namespace Jettison::Render
{
glm::dquat OrientationFromAngles(double pitch, double roll, double yaw)
{
	const glm::dvec3 forward{
	    -std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
	const glm::dvec3 worldUp{0.0, 1.0, 0.0};
	const glm::dvec3 right = glm::normalize(glm::cross(forward, worldUp));
	const glm::dvec3 up = glm::cross(right, forward);
	// Columns: the camera's +X, +Y and +Z (which is backwards) in the system frame.
	const glm::dquat level = glm::quat_cast(glm::dmat3{right, up, -forward});
	return glm::angleAxis(roll, forward) * level;
}

glm::mat4 ReversedInfinitePerspective(float verticalFov, float aspect, float nearPlane)
{
	const float focal = 1.0f / std::tan(verticalFov * 0.5f);
	glm::mat4 projection{0.0f};
	projection[0][0] = focal / aspect;
	// Vulkan's clip space has y down.
	projection[1][1] = -focal;
	// clip.z = near and clip.w = -z, so depth = near / -z: 1 on the near plane, 0 at infinity.
	projection[3][2] = nearPlane;
	projection[2][3] = -1.0f;
	return projection;
}

Math::DRay ScreenRay(const CameraView &camera, const glm::dvec2 &point, const glm::dvec2 &windowSize)
{
	// The screen spans the vertical field of view top to bottom, and the same scale across.
	const double extent = std::tan(static_cast<double>(camera.verticalFov) * 0.5);
	const double x = (2.0 * point.x / windowSize.x - 1.0) * extent * windowSize.x / windowSize.y;
	const double y = (1.0 - 2.0 * point.y / windowSize.y) * extent;
	return {camera.position, glm::normalize(camera.orientation * glm::dvec3{x, y, -1.0})};
}

glm::mat4 RenderViewMatrix(const CameraView &camera)
{
	return glm::mat4_cast(glm::quat{glm::conjugate(camera.orientation)});
}

glm::mat4 ToRenderSpace(const glm::dmat4 &world, const glm::dvec3 &camera)
{
	glm::dmat4 relative = world;
	relative[3] = glm::dvec4{glm::dvec3{world[3]} - camera, world[3].w};
	return glm::mat4{relative};
}

glm::vec3 ToRenderSpace(const glm::dvec3 &position, const glm::dvec3 &camera)
{
	return glm::vec3{position - camera};
}
} // namespace Jettison::Render
