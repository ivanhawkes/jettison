#include "vulkan/DeviceCapabilities.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace Jettison::Render;

namespace
{
// A device with every optional path's needs but the cluster extension, as the gate's lavapipe is.
DeviceCapabilities Lavapipe()
{
	DeviceCapabilities capabilities;
	capabilities.apiVersion = (1u << 22) | (4u << 12);
	capabilities.multiDrawIndirect = true;
	capabilities.drawIndirectCount = true;
	capabilities.bufferDeviceAddress = true;
	capabilities.meshShader = true;
	capabilities.taskShader = true;
	capabilities.rayQuery = true;
	capabilities.accelerationStructure = true;
	return capabilities;
}


bool Mentions(const std::vector<std::string> &lines, const std::string &text)
{
	for (const std::string &line : lines)
		if (line.find(text) != std::string::npos)
			return true;
	return false;
}
} // namespace


TEST_CASE("Device paths: a device with the features has meshlets and ray tracing, and no Mega Geometry")
{
	const DevicePaths paths = ChoosePaths(Lavapipe(), {});
	CHECK(paths.meshlets.enabled);
	CHECK(paths.rayTracing.enabled);
	CHECK_FALSE(paths.megaGeometry.enabled);
	CHECK(paths.megaGeometry.why.find("clusterAccelerationStructure") != std::string::npos);
}


TEST_CASE("Device paths: a path says what the device lacks")
{
	DeviceCapabilities capabilities = Lavapipe();
	capabilities.taskShader = false;
	capabilities.drawIndirectCount = false;
	capabilities.rayQuery = false;
	const DevicePaths paths = ChoosePaths(capabilities, {});
	CHECK_FALSE(paths.meshlets.enabled);
	CHECK(paths.meshlets.why == "the device lacks taskShader, drawIndirectCount");
	CHECK_FALSE(paths.rayTracing.enabled);
	CHECK(paths.rayTracing.why == "the device lacks rayQuery");
}


TEST_CASE("Device paths: a setting turns a path off that the device offers, and says so")
{
	DeviceCapabilities capabilities = Lavapipe();
	capabilities.clusterAccelerationStructure = true;
	CHECK(ChoosePaths(capabilities, {}).megaGeometry.enabled);

	const DevicePaths paths = ChoosePaths(capabilities, {.meshlets = false, .rayTracing = false});
	CHECK_FALSE(paths.meshlets.enabled);
	CHECK(paths.meshlets.why.find("--no-meshlets") != std::string::npos);
	CHECK_FALSE(paths.rayTracing.enabled);
	CHECK(paths.rayTracing.why.find("--no-ray-tracing") != std::string::npos);

	// The cluster structures are built over the ray tracing path's.
	CHECK_FALSE(paths.megaGeometry.enabled);
	CHECK(paths.megaGeometry.why.find("the ray tracing path") != std::string::npos);

	const DevicePaths mega = ChoosePaths(capabilities, {.megaGeometry = false});
	CHECK(mega.rayTracing.enabled);
	CHECK_FALSE(mega.megaGeometry.enabled);
	CHECK(mega.megaGeometry.why.find("--no-mega-geometry") != std::string::npos);
}


TEST_CASE("Device capabilities: the log carries the version, the limits and each path")
{
	DeviceCapabilities capabilities = Lavapipe();
	capabilities.maxMeshOutputVertices = 256;
	capabilities.maxMeshOutputPrimitives = 256;
	const std::vector<std::string> lines =
	    DescribeCapabilities(capabilities, ChoosePaths(capabilities, {.rayTracing = false}));
	CHECK(Mentions(lines, "Vulkan 1.4.0"));
	CHECK(Mentions(lines, "at most 256 vertices and 256 primitives"));
	CHECK(Mentions(lines, "Path meshlets: on"));
	CHECK(Mentions(lines, "Path ray tracing: off, turned off"));
	CHECK(Mentions(lines, "Path mega geometry: off"));
}
