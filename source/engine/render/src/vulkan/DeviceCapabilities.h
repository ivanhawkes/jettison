#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The handle, declared as Vulkan declares it (VK_DEFINE_HANDLE), so this header carries no Vulkan
// header and the rule below is tested without a device.
struct VkPhysicalDevice_T;

namespace Jettison::Render
{
// What the device offers beyond what the engine requires of every device (DeviceContext::Unsuitable),
// read once when the device is chosen, with no Vulkan type in it. Each optional path asks this
// table and nothing else (docs/dense-geometry-and-ray-tracing.md, "Device capabilities and paths").
// A feature that needs an extension the device does not list is false, and so are its limits zero.
struct DeviceCapabilities
{
	// VkPhysicalDeviceProperties::apiVersion, VK_MAKE_API_VERSION's encoding.
	std::uint32_t apiVersion{0};

	// What Vulkan 1.4 makes required (VkPhysicalDeviceVulkan14Features), read so the log shows the
	// device keeps the promise.
	bool pushDescriptor{false};
	bool maintenance5{false};
	bool maintenance6{false};
	bool dynamicRenderingLocalRead{false};
	bool hostImageCopy{false};

	// Optional in every version: the core features, and the 1.2 ones, the paths build on.
	bool multiDrawIndirect{false};
	bool drawIndirectCount{false};
	bool bufferDeviceAddress{false};
	bool descriptorIndexing{false};
	bool samplerFilterMinmax{false};

	// 64-bit atomics: the storage buffer form (1.2), and the image form (VK_EXT_shader_image_atomic_int64).
	bool shaderBufferInt64Atomics{false};
	bool shaderImageInt64Atomics{false};

	// VK_EXT_mesh_shader, and the limits the meshlet size and the mesh shader's group are chosen from.
	bool meshShader{false};
	bool taskShader{false};
	std::uint32_t maxMeshOutputVertices{0};
	std::uint32_t maxMeshOutputPrimitives{0};
	std::uint32_t maxMeshWorkGroupInvocations{0};
	std::uint32_t maxPreferredMeshWorkGroupInvocations{0};
	std::uint32_t maxTaskWorkGroupInvocations{0};
	std::uint32_t maxPreferredTaskWorkGroupInvocations{0};

	// VK_KHR_ray_query and VK_KHR_acceleration_structure, and the structure's limits.
	bool rayQuery{false};
	bool accelerationStructure{false};
	std::uint64_t maxGeometryCount{0};
	std::uint64_t maxInstanceCount{0};
	std::uint64_t maxPrimitiveCount{0};

	// VK_NV_cluster_acceleration_structure (Mega Geometry), and the size of a cluster it builds.
	bool clusterAccelerationStructure{false};
	std::uint32_t maxVerticesPerCluster{0};
	std::uint32_t maxTrianglesPerCluster{0};
};


// Asks the device what it offers. The device is at Vulkan 1.4, as the chosen one is.
DeviceCapabilities ReadCapabilities(VkPhysicalDevice_T *device);


// Which optional paths the player allows (--no-meshlets, --no-ray-tracing, --no-mega-geometry, and
// the settings of the same names): false turns a path off whatever the device offers.
struct PathSettings
{
	bool meshlets{true};
	bool rayTracing{true};
	bool megaGeometry{true};
};


// Whether a path runs, and when it does not, why: what the device lacks, or that the player turned
// it off.
struct PathState
{
	bool enabled{false};
	std::string why;
};


struct DevicePaths
{
	// Mesh and task shaders drawing clusters of triangles; the indexed path without.
	PathState meshlets;

	// Ray queries over acceleration structures; the screen-space and cascaded effects without.
	PathState rayTracing;

	// Cluster acceleration structures, built on the ray tracing path's structures; plain
	// bottom-level structures without.
	PathState megaGeometry;
};


// Each path's state, from what the device offers and what the player allows.
DevicePaths ChoosePaths(const DeviceCapabilities &capabilities, const PathSettings &settings);


// The table and the paths as the lines the log carries at start-up, one fact or group to a line.
std::vector<std::string> DescribeCapabilities(
    const DeviceCapabilities &capabilities, const DevicePaths &paths);
} // namespace Jettison::Render
