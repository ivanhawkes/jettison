#include "vulkan/DeviceCapabilities.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Jettison::Render
{
namespace
{
// A device's extension, as a name it lists.
bool Lists(std::span<const VkExtensionProperties> extensions, std::string_view name)
{
	return std::ranges::any_of(
	    extensions,
	    [name](const VkExtensionProperties &extension) { return name == extension.extensionName; });
}


// A requirement and the name it is called by in the reason.
using Requirement = std::pair<std::string_view, bool>;

// The names of the requirements not met, joined, or empty when all are.
std::string Lacking(std::initializer_list<Requirement> requirements)
{
	std::string names;
	for (const auto &[name, met] : requirements)
		if (!met)
			names += (names.empty() ? "" : ", ") + std::string{name};
	return names;
}


PathState Decide(bool allowed, std::string_view switches, const std::string &lacking)
{
	if (!allowed)
		return {false, std::format("turned off ({})", switches)};
	if (!lacking.empty())
		return {false, std::format("the device lacks {}", lacking)};
	return {true, {}};
}


std::string_view YesNo(bool value)
{
	return value ? "yes" : "no";
}


// Vulkan's encoding of a version, read for the log.
std::string VersionText(std::uint32_t version)
{
	return std::format(
	    "{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
	    VK_API_VERSION_PATCH(version));
}


std::string PathText(std::string_view name, const PathState &state)
{
	return std::format("{}: {}", name, state.enabled ? "on" : "off, " + state.why);
}
} // namespace


DeviceCapabilities ReadCapabilities(VkPhysicalDevice_T *device)
{
	uint32_t extensionCount = 0;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
	std::vector<VkExtensionProperties> extensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

	// A structure is chained only for an extension the device lists: asking for one it does not is
	// invalid, and the validation layer says so.
	VkPhysicalDeviceVulkan14Features features14{};
	features14.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES;
	VkPhysicalDeviceVulkan12Features features12{};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
	meshFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
	VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
	rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
	VkPhysicalDeviceAccelerationStructureFeaturesKHR structureFeatures{};
	structureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT imageAtomicFeatures{};
	imageAtomicFeatures.sType =
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;
	VkPhysicalDeviceClusterAccelerationStructureFeaturesNV clusterFeatures{};
	clusterFeatures.sType =
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV;

	VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties{};
	meshProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
	VkPhysicalDeviceAccelerationStructurePropertiesKHR structureProperties{};
	structureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
	VkPhysicalDeviceClusterAccelerationStructurePropertiesNV clusterProperties{};
	clusterProperties.sType =
	    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV;

	VkPhysicalDeviceFeatures2 features{};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	VkPhysicalDeviceProperties2 properties{};
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	void **featuresTail = &features.pNext;
	void **propertiesTail = &properties.pNext;
	const auto chain = [](void **&tail, auto &structure)
	{
		*tail = &structure;
		tail = &structure.pNext;
	};
	chain(featuresTail, features14);
	chain(featuresTail, features12);
	if (Lists(extensions, VK_EXT_MESH_SHADER_EXTENSION_NAME))
	{
		chain(featuresTail, meshFeatures);
		chain(propertiesTail, meshProperties);
	}
	if (Lists(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME))
		chain(featuresTail, rayQueryFeatures);
	if (Lists(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME))
	{
		chain(featuresTail, structureFeatures);
		chain(propertiesTail, structureProperties);
	}
	if (Lists(extensions, VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME))
		chain(featuresTail, imageAtomicFeatures);
	if (Lists(extensions, VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME))
	{
		chain(featuresTail, clusterFeatures);
		chain(propertiesTail, clusterProperties);
	}
	vkGetPhysicalDeviceFeatures2(device, &features);
	vkGetPhysicalDeviceProperties2(device, &properties);

	DeviceCapabilities capabilities;
	capabilities.apiVersion = properties.properties.apiVersion;
	capabilities.pushDescriptor = features14.pushDescriptor == VK_TRUE;
	capabilities.maintenance5 = features14.maintenance5 == VK_TRUE;
	capabilities.maintenance6 = features14.maintenance6 == VK_TRUE;
	capabilities.dynamicRenderingLocalRead = features14.dynamicRenderingLocalRead == VK_TRUE;
	capabilities.hostImageCopy = features14.hostImageCopy == VK_TRUE;
	capabilities.multiDrawIndirect = features.features.multiDrawIndirect == VK_TRUE;
	capabilities.drawIndirectCount = features12.drawIndirectCount == VK_TRUE;
	capabilities.bufferDeviceAddress = features12.bufferDeviceAddress == VK_TRUE;
	capabilities.descriptorIndexing = features12.descriptorIndexing == VK_TRUE;
	capabilities.samplerFilterMinmax = features12.samplerFilterMinmax == VK_TRUE;
	capabilities.shaderBufferInt64Atomics = features12.shaderBufferInt64Atomics == VK_TRUE;
	capabilities.shaderImageInt64Atomics = imageAtomicFeatures.shaderImageInt64Atomics == VK_TRUE;
	capabilities.meshShader = meshFeatures.meshShader == VK_TRUE;
	capabilities.taskShader = meshFeatures.taskShader == VK_TRUE;
	capabilities.maxMeshOutputVertices = meshProperties.maxMeshOutputVertices;
	capabilities.maxMeshOutputPrimitives = meshProperties.maxMeshOutputPrimitives;
	capabilities.maxMeshWorkGroupInvocations = meshProperties.maxMeshWorkGroupInvocations;
	capabilities.maxPreferredMeshWorkGroupInvocations =
	    meshProperties.maxPreferredMeshWorkGroupInvocations;
	capabilities.maxTaskWorkGroupInvocations = meshProperties.maxTaskWorkGroupInvocations;
	capabilities.maxPreferredTaskWorkGroupInvocations =
	    meshProperties.maxPreferredTaskWorkGroupInvocations;
	capabilities.rayQuery = rayQueryFeatures.rayQuery == VK_TRUE;
	capabilities.accelerationStructure = structureFeatures.accelerationStructure == VK_TRUE;
	capabilities.maxGeometryCount = structureProperties.maxGeometryCount;
	capabilities.maxInstanceCount = structureProperties.maxInstanceCount;
	capabilities.maxPrimitiveCount = structureProperties.maxPrimitiveCount;
	capabilities.clusterAccelerationStructure = clusterFeatures.clusterAccelerationStructure == VK_TRUE;
	capabilities.maxVerticesPerCluster = clusterProperties.maxVerticesPerCluster;
	capabilities.maxTrianglesPerCluster = clusterProperties.maxTrianglesPerCluster;
	return capabilities;
}


DevicePaths ChoosePaths(const DeviceCapabilities &capabilities, const PathSettings &settings)
{
	DevicePaths paths;
	paths.meshlets = Decide(
	    settings.meshlets, "--no-meshlets or render.meshlets",
	    Lacking(
	        {{"meshShader", capabilities.meshShader},
	         {"taskShader", capabilities.taskShader},
	         {"multiDrawIndirect", capabilities.multiDrawIndirect},
	         {"drawIndirectCount", capabilities.drawIndirectCount}}));
	paths.rayTracing = Decide(
	    settings.rayTracing, "--no-ray-tracing or render.rayTracing",
	    Lacking(
	        {{"accelerationStructure", capabilities.accelerationStructure},
	         {"rayQuery", capabilities.rayQuery},
	         {"bufferDeviceAddress", capabilities.bufferDeviceAddress}}));

	// Cluster structures are built over the ray tracing path's, so it is off with that one.
	paths.megaGeometry = Decide(
	    settings.megaGeometry, "--no-mega-geometry or render.megaGeometry",
	    Lacking(
	        {{"clusterAccelerationStructure", capabilities.clusterAccelerationStructure},
	         {"the ray tracing path", paths.rayTracing.enabled}}));
	return paths;
}


std::vector<std::string> DescribeCapabilities(
    const DeviceCapabilities &capabilities, const DevicePaths &paths)
{
	const DeviceCapabilities &c = capabilities;
	std::vector<std::string> lines;
	lines.push_back(std::format("Vulkan {}", VersionText(c.apiVersion)));
	lines.push_back(
	    std::format(
	        "Vulkan 1.4 features: pushDescriptor {}, maintenance5 {}, maintenance6 {}, "
	        "dynamicRenderingLocalRead {}, hostImageCopy {}",
	        YesNo(c.pushDescriptor), YesNo(c.maintenance5), YesNo(c.maintenance6),
	        YesNo(c.dynamicRenderingLocalRead), YesNo(c.hostImageCopy)));
	lines.push_back(
	    std::format(
	        "Features: multiDrawIndirect {}, drawIndirectCount {}, bufferDeviceAddress {}, "
	        "descriptorIndexing {}, samplerFilterMinmax {}, shaderBufferInt64Atomics {}, "
	        "shaderImageInt64Atomics {}",
	        YesNo(c.multiDrawIndirect), YesNo(c.drawIndirectCount), YesNo(c.bufferDeviceAddress),
	        YesNo(c.descriptorIndexing), YesNo(c.samplerFilterMinmax),
	        YesNo(c.shaderBufferInt64Atomics), YesNo(c.shaderImageInt64Atomics)));
	lines.push_back(
	    std::format(
	        "Mesh shaders: mesh {}, task {}; at most {} vertices and {} primitives, workgroups of {} "
	        "invocations (preferred {}) for mesh and {} (preferred {}) for task",
	        YesNo(c.meshShader), YesNo(c.taskShader), c.maxMeshOutputVertices,
	        c.maxMeshOutputPrimitives, c.maxMeshWorkGroupInvocations,
	        c.maxPreferredMeshWorkGroupInvocations, c.maxTaskWorkGroupInvocations,
	        c.maxPreferredTaskWorkGroupInvocations));
	lines.push_back(
	    std::format(
	        "Ray tracing: rayQuery {}, accelerationStructure {}; at most {} geometries, {} instances, "
	        "{} "
	        "primitives",
	        YesNo(c.rayQuery), YesNo(c.accelerationStructure), c.maxGeometryCount, c.maxInstanceCount,
	        c.maxPrimitiveCount));
	lines.push_back(
	    std::format(
	        "Cluster acceleration structures: {}; at most {} vertices and {} triangles a cluster",
	        YesNo(c.clusterAccelerationStructure), c.maxVerticesPerCluster, c.maxTrianglesPerCluster));
	lines.push_back("Path " + PathText("meshlets", paths.meshlets));
	lines.push_back("Path " + PathText("ray tracing", paths.rayTracing));
	lines.push_back("Path " + PathText("mega geometry", paths.megaGeometry));
	return lines;
}
} // namespace Jettison::Render
