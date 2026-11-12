// When the environment's capture is rebaked for the bodies about the camera (render/vulkan/
// Environment.h), through the backend's private header.

#include "vulkan/Environment.h"

#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <cmath>

using namespace Jettison::Render;

namespace
{
CaptureLight PlanetAt(const glm::vec3 &centre, float radius)
{
	CaptureLight light;
	light.sunDirection = {0.0f, 1.0f, 0.0f, 1.0f};
	GpuCaptureBody body;
	body.body.atmosphere.centreRadius = glm::vec4{centre, radius};
	light.bodies.push_back(body);
	return light;
}
} // namespace

TEST_CASE("the capture is rebaked when a body or the sun has moved by more than a texel of it")
{
	// Ostra from Highport, 420 km up: the planet's centre 6,791 km away, 70 degrees across.
	const CaptureLight baked = PlanetAt({0.0f, -6.791e6f, 0.0f}, 6.371e6f);
	CHECK_FALSE(CaptureMoved(baked, baked));

	// Half a degree round the orbit is under the hundredth of a radian a texel allows; a degree is
	// over it.
	const auto turned = [](float angle)
	{ return PlanetAt(6.791e6f * glm::vec3{std::sin(angle), -std::cos(angle), 0.0f}, 6.371e6f); };
	CHECK_FALSE(CaptureMoved(baked, turned(0.0087f)));
	CHECK(CaptureMoved(baked, turned(0.0175f)));

	// Nearer by 1% is no bigger on the sky by 2%; nearer by 3% is.
	CHECK_FALSE(CaptureMoved(baked, PlanetAt({0.0f, -6.791e6f * 0.99f, 0.0f}, 6.371e6f)));
	CHECK(CaptureMoved(baked, PlanetAt({0.0f, -6.791e6f * 0.97f, 0.0f}, 6.371e6f)));

	// The sun turned by a degree, a body come or gone, or the sun gone.
	CaptureLight sunTurned = baked;
	sunTurned.sunDirection = {std::sin(0.0175f), std::cos(0.0175f), 0.0f, 1.0f};
	CHECK(CaptureMoved(baked, sunTurned));
	CaptureLight more = baked;
	more.bodies.push_back(baked.bodies.front());
	CHECK(CaptureMoved(baked, more));
	CaptureLight dark = baked;
	dark.sunDirection = glm::vec4{0.0f};
	CHECK(CaptureMoved(baked, dark));
}
