#include <jettison/render/Camera.h>

#include <doctest/doctest.h>

#include <cmath>

using namespace Jettison::Render;

namespace
{
// Where a render-space point lands: normalised device coordinates and depth.
glm::vec3 Project(const glm::mat4 &projection, const glm::vec3 &point)
{
	const glm::vec4 clip = projection * glm::vec4{point, 1.0f};
	return glm::vec3{clip} / clip.w;
}
} // namespace

TEST_CASE("Camera: reversed-Z puts the near plane at 1 and infinity at 0, monotonically")
{
	const glm::mat4 projection = ReversedInfinitePerspective(glm::radians(60.0f), 16.0f / 9.0f, 0.5f);
	CHECK(Project(projection, {0.0f, 0.0f, -0.5f}).z == doctest::Approx(1.0f));
	CHECK(Project(projection, {0.0f, 0.0f, -1.0f}).z == doctest::Approx(0.5f));
	// Forty thousand kilometres: still in front, still above zero, still in order.
	const float planet = Project(projection, {0.0f, 0.0f, -4.0e7f}).z;
	const float beyond = Project(projection, {0.0f, 0.0f, -4.1e7f}).z;
	CHECK(planet > 0.0f);
	CHECK(beyond < planet);
	CHECK(beyond > 0.0f);
	// Vulkan's y is down: a point above the axis lands in the top half.
	CHECK(Project(projection, {0.0f, 1.0f, -10.0f}).y < 0.0f);
	// The edge of the vertical field of view is the edge of the screen.
	CHECK(
	    Project(projection, {0.0f, std::tan(glm::radians(30.0f)) * 10.0f, -10.0f}).y ==
	    doctest::Approx(-1.0f));
}

TEST_CASE("Camera: the scene's angles give the view direction the renderer has always used")
{
	const double pitch = -0.615479709;
	const double yaw = 0.785398163;
	const glm::dquat orientation = OrientationFromAngles(pitch, 0.0, yaw);
	const glm::dvec3 forward = orientation * glm::dvec3{0.0, 0.0, -1.0};
	CHECK(forward.x == doctest::Approx(-std::sin(yaw) * std::cos(pitch)));
	CHECK(forward.y == doctest::Approx(std::sin(pitch)));
	CHECK(forward.z == doctest::Approx(-std::cos(yaw) * std::cos(pitch)));
	// Level: the camera's right has no height.
	CHECK(std::abs((orientation * glm::dvec3{1.0, 0.0, 0.0}).y) < 1e-12);

	// Roll turns the up vector about the view direction.
	const glm::dquat rolled = OrientationFromAngles(0.0, glm::radians(90.0), 0.0);
	const glm::dvec3 up = rolled * glm::dvec3{0.0, 1.0, 0.0};
	CHECK(std::abs(up.y) < 1e-12);
	CHECK(std::abs(std::abs(up.x) - 1.0) < 1e-12);

	// The view matrix sends what the camera looks at down -Z.
	const glm::vec3 ahead = RenderViewMatrix({{}, orientation}) * glm::vec4{glm::vec3{forward}, 0.0f};
	CHECK(ahead.z == doctest::Approx(-1.0f));
}

TEST_CASE("Camera: render space takes the camera off in double precision first")
{
	// A station a hundred and fifty million kilometres out, a millimetre's detail on it.
	const glm::dvec3 camera{1.496e11, 1.0e3, -2.0e10};
	glm::dmat4 world{1.0};
	world[3] = glm::dvec4{camera + glm::dvec3{0.001, 12.5, -40.0}, 1.0};
	const glm::mat4 relative = ToRenderSpace(world, camera);
	CHECK(relative[3].x == doctest::Approx(0.001f).epsilon(1e-2));
	CHECK(relative[3].y == 12.5f);
	CHECK(relative[3].z == -40.0f);
	CHECK(relative[3].w == 1.0f);
	CHECK(ToRenderSpace(camera + glm::dvec3{3.0, 0.0, 0.0}, camera) == glm::vec3{3.0f, 0.0f, 0.0f});
}

TEST_CASE("Camera: a screen ray passes through what is drawn at that point on the screen")
{
	CameraView camera;
	camera.position = {1.0e11, -3.0e10, 42.0};
	camera.orientation = OrientationFromAngles(0.3, 0.1, -1.2);
	camera.verticalFov = glm::radians(50.0f);
	const glm::dvec2 window{640.0, 480.0};

	// The centre looks where the camera does.
	const Jettison::Math::DRay centre = ScreenRay(camera, window * 0.5, window);
	CHECK(centre.origin == camera.position);
	CHECK(
	    glm::dot(centre.direction, camera.orientation * glm::dvec3{0.0, 0.0, -1.0}) ==
	    doctest::Approx(1.0));

	// A point 300 m off in the view, projected as the renderer projects it, lies on the ray through
	// the pixel it lands on.
	const glm::dvec3 point = camera.position + camera.orientation * glm::dvec3{-40.0, 25.0, -300.0};
	const glm::mat4 projection =
	    ReversedInfinitePerspective(camera.verticalFov, static_cast<float>(window.x / window.y), 0.1f);
	const glm::vec3 ndc =
	    Project(projection * RenderViewMatrix(camera), ToRenderSpace(point, camera.position));
	// Vulkan's y is down, as a window's is.
	const glm::dvec2 pixel{(ndc.x + 1.0) * 0.5 * window.x, (ndc.y + 1.0) * 0.5 * window.y};
	const Jettison::Math::DRay ray = ScreenRay(camera, pixel, window);
	CHECK(glm::length(ray.direction) == doctest::Approx(1.0));
	const glm::dvec3 towards = glm::normalize(point - camera.position);
	CHECK(glm::dot(ray.direction, towards) == doctest::Approx(1.0).epsilon(1e-9));
}
