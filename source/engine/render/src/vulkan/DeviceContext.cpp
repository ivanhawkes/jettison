#include <jettison/render/vulkan/DeviceContext.h>

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// STD.
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>


const std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};


#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif // NDEBUG


namespace Jettison::Render
{
void DeviceContext::Init()
{
	// Device initialisation.
	CreateInstance();
	CreateSurface();
	PickPhysicalDevice();
	CreateLogicalDevice();
	CreateAllocator();

	// Command pool.
	// One pool for the life of the device, and not recreated with the swapchain: the pool belongs
	// to the graphics queue family of this physical device, and a swapchain recreate replaces the
	// swapchain handle for the same surface, so neither the device nor its queue families change.
	// What does belong to the swapchain is the work recorded against the pool - the pipeline's
	// per-image command buffers, reallocated by Pipeline::Recreate for the new image count - and
	// the single-time copies that upload a texture, which open and close inside one call.
	CreateCommandPool();
}


void DeviceContext::Destroy()
{
	if (m_logicalDevice != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(m_logicalDevice, m_commandPool, nullptr);
		DestroyAllocator();
		vkDestroyDevice(m_logicalDevice, nullptr);
		m_logicalDevice = VK_NULL_HANDLE;
	}
	if (m_instance != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
		if (m_debugMessenger != VK_NULL_HANDLE)
			m_pfnDestroyDebugUtilsMessenger(m_instance, m_debugMessenger, nullptr);
		vkDestroyInstance(m_instance, nullptr);
		m_instance = VK_NULL_HANDLE;
	}
	m_commandPool = VK_NULL_HANDLE;
	m_surface = VK_NULL_HANDLE;
	m_debugMessenger = VK_NULL_HANDLE;
	m_pfnDestroyDebugUtilsMessenger = nullptr;
	m_pfnSetDebugUtilsObjectName = nullptr;
	m_pfnCmdBeginDebugLabel = nullptr;
	m_pfnCmdEndDebugLabel = nullptr;
	m_debugUtilsActive = false;
}


bool DeviceContext::CheckValidationLayerSupport()
{
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const char *layerName : validationLayers)
	{
		bool layerFound = false;

		for (const auto &layerProperties : availableLayers)
		{
			if (strcmp(layerName, layerProperties.layerName) == 0)
			{
				layerFound = true;
				break;
			}
		}

		if (!layerFound)
		{
			return false;
		}
	}

	return true;
}


void DeviceContext::CreateInstance()
{
	if (kEnableValidationLayers && !CheckValidationLayerSupport())
	{
		throw std::runtime_error("validation layers requested, but not available");
	}

	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Render Test";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Jettison";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = kApiVersion;

	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	uint32_t glfwExtensionCount = 0;
	const char **glfwExtensions;

	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

	// A messenger in the instance's pNext chain sees the messages emitted while the instance
	// and the logical device are being built, which is where object-lifetime misuse in device
	// setup shows up. Requested only with the validation layer, and only when it is present:
	// an extension the loader cannot supply fails instance creation, so asking unconditionally
	// would break machines without it.
	auto messengerInfo = VkDebugUtilsMessengerCreateInfoEXT{};
	const bool wantDebugUtils =
	    kEnableValidationLayers && HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	if (wantDebugUtils)
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	if (wantDebugUtils)
	{
		messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		// Errors and warnings only. Validation's info messages name every object the engine
		// creates, which buries the two lines that matter under a message per buffer, and the
		// smoke test reads this stream as pass/fail.
		messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
		                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
		messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		messengerInfo.pfnUserCallback = DebugUtilsCallback;
		createInfo.pNext = &messengerInfo;
	}

	// Synchronization validation, on top of the layer's default checks, which do not look at
	// barriers at all: the pass list derives every barrier in the frame, and this is what reports
	// one that is missing or too weak (a read racing a write, a layout used before its
	// transition) in the run the smoke test reads. It costs CPU time in the layer, which only a
	// Debug build pays. The setting goes through VK_EXT_layer_settings, which the layer itself
	// provides, so it is asked for only when the layer offers it.
	const VkBool32 validateSync = VK_TRUE;
	VkLayerSettingEXT syncSetting{};
	syncSetting.pLayerName = validationLayers[0];
	syncSetting.pSettingName = "validate_sync";
	syncSetting.type = VK_LAYER_SETTING_TYPE_BOOL32_EXT;
	syncSetting.valueCount = 1;
	syncSetting.pValues = &validateSync;
	VkLayerSettingsCreateInfoEXT layerSettings{};
	layerSettings.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
	layerSettings.settingCount = 1;
	layerSettings.pSettings = &syncSetting;
	if (kEnableValidationLayers &&
	    HasInstanceExtension(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME, validationLayers[0]))
	{
		extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
		layerSettings.pNext = createInfo.pNext;
		createInfo.pNext = &layerSettings;
	}

	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();

	if (kEnableValidationLayers)
	{
		createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
		createInfo.ppEnabledLayerNames = validationLayers.data();
	}
	else
	{
		createInfo.enabledLayerCount = 0;
	}

	if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create m_instance");
	}

	// Resolved through the loader: the promoted entry points of VK_EXT_debug_utils are absent
	// from a Vulkan 1.0 loader, and linking against them would make this project unbuildable
	// on a machine whose SDK predates them. A device-level command resolved from the instance
	// is valid; the loader routes it to the device the object belongs to.
	if (wantDebugUtils)
	{
		const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
		    vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
		m_pfnDestroyDebugUtilsMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
		    vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
		if (!createMessenger || !m_pfnDestroyDebugUtilsMessenger ||
		    createMessenger(m_instance, &messengerInfo, nullptr, &m_debugMessenger) != VK_SUCCESS)
			throw std::runtime_error("failed to create Vulkan debug messenger");
		m_pfnSetDebugUtilsObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
		    vkGetInstanceProcAddr(m_instance, "vkSetDebugUtilsObjectNameEXT"));
		m_pfnCmdBeginDebugLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
		    vkGetInstanceProcAddr(m_instance, "vkCmdBeginDebugUtilsLabelEXT"));
		m_pfnCmdEndDebugLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
		    vkGetInstanceProcAddr(m_instance, "vkCmdEndDebugUtilsLabelEXT"));
		m_debugUtilsActive = true;
	}
}


bool DeviceContext::HasInstanceExtension(const char *name, const char *layer) const
{
	uint32_t count = 0;
	vkEnumerateInstanceExtensionProperties(layer, &count, nullptr);
	std::vector<VkExtensionProperties> available(count);
	vkEnumerateInstanceExtensionProperties(layer, &count, available.data());
	for (const auto &extension : available)
		if (strcmp(extension.extensionName, name) == 0)
			return true;
	return false;
}


VKAPI_ATTR VkBool32 VKAPI_CALL DeviceContext::DebugUtilsCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT *pData, void *pUserData)
{
	// The severity spelled out, because a validation message that only says "Warning" is read
	// as an invitation to stop looking. Performance notes are kept: object lifetimes are what
	// this engine gets wrong, and the layer's performance messages name the same objects.
	(void)pUserData;
	const char *level = "info";
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		level = "error";
	else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		level = "warning";

	std::string objects;
	for (uint32_t i = 0; i < pData->objectCount; ++i)
	{
		const auto &object = pData->pObjects[i];
		if (object.pObjectName)
			objects += " '\"" + std::string{object.pObjectName} + "\"'";
		else
			objects += " 0x" + std::to_string(object.objectHandle);
	}
	std::string labels;
	for (uint32_t i = 0; i < pData->cmdBufLabelCount; ++i)
		if (pData->pCmdBufLabels[i].pLabelName)
			labels += " [" + std::string{pData->pCmdBufLabels[i].pLabelName} + "]";

	std::string kind;
	if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
		kind += "validation+";
	if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
		kind += "performance+";
	if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
		kind += "general+";
	if (!kind.empty())
		kind.pop_back();

	// To stderr, not the layer's own stdout: the smoke test greps this stream for a clean run.
	std::fprintf(
	    stderr, "[vulkan:%s%s%s] %s: %s%s%s\n", level, kind.empty() ? "" : ":", kind.c_str(),
	    pData->pMessageIdName ? pData->pMessageIdName : "message", pData->pMessage,
	    objects.empty() ? "" : " objects:", objects.c_str());
	if (!labels.empty())
		std::fprintf(stderr, "[vulkan] labels:%s\n", labels.c_str());

	// False: the layer's own report continues, and the callback is a listener here.
	return VK_FALSE;
}


void DeviceContext::SetObjectName(VkObjectType type, uint64_t handle, const std::string &name) const
{
	if (!m_pfnSetDebugUtilsObjectName || handle == 0)
		return;
	VkDebugUtilsObjectNameInfoEXT nameInfo{};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = type;
	nameInfo.objectHandle = handle;
	nameInfo.pObjectName = name.c_str();
	const VkResult result = m_pfnSetDebugUtilsObjectName(m_logicalDevice, &nameInfo);
	// A name is diagnostics. A failure to set one is not a reason to stop a frame, and it is
	// not silent either: this is the only place the engine would learn that names are absent.
	if (result != VK_SUCCESS && result != VK_ERROR_EXTENSION_NOT_PRESENT)
		std::fprintf(stderr, "[vulkan] vkSetDebugUtilsObjectNameEXT: VkResult = %d\n", result);
}


void DeviceContext::BeginDebugLabel(VkCommandBuffer commandBuffer, const std::string &name) const
{
	if (!m_pfnCmdBeginDebugLabel || commandBuffer == VK_NULL_HANDLE)
		return;
	VkDebugUtilsLabelEXT label{};
	label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
	label.pLabelName = name.c_str();
	// No device argument: the command-buffer label entry points of VK_EXT_debug_utils take the
	// command buffer alone. The Vulkan 1.3 core promotion is what adds the device parameter,
	// and that entry point has a different name.
	m_pfnCmdBeginDebugLabel(commandBuffer, &label);
}


void DeviceContext::EndDebugLabel(VkCommandBuffer commandBuffer) const
{
	if (!m_pfnCmdEndDebugLabel || commandBuffer == VK_NULL_HANDLE)
		return;
	m_pfnCmdEndDebugLabel(commandBuffer);
}


SwapChainSupportDetails DeviceContext::QuerySwapChainSupport(VkPhysicalDevice device)
{
	SwapChainSupportDetails details;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

	if (formatCount != 0)
	{
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
	}

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

	if (presentModeCount != 0)
	{
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(
		    device, m_surface, &presentModeCount, details.presentModes.data());
	}

	return details;
}


bool DeviceContext::CheckDeviceExtensionSupport(VkPhysicalDevice device)
{
	uint32_t extensionCount = 0;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

	std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

	for (const auto &extension : availableExtensions)
	{
		requiredExtensions.erase(extension.extensionName);
	}

	return requiredExtensions.empty();
}


QueueFamilyIndices DeviceContext::FindQueueFamilies(VkPhysicalDevice device)
{
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	uint32_t i = 0;
	for (const auto &queueFamily : queueFamilies)
	{
		if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			m_graphicsQueueIndex = i;
			indices.graphicsFamily = i;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
		if (presentSupport)
		{
			m_presentQueueIndex = i;
			indices.presentFamily = i;
		}

		if (indices.isComplete())
		{
			break;
		}

		++i;
	}

	return indices;
}


bool DeviceContext::IsDeviceSuitable(VkPhysicalDevice device)
{
	QueueFamilyIndices indices = FindQueueFamilies(device);

	bool extensionsSupported = CheckDeviceExtensionSupport(device);

	bool swapChainAdequate = false;
	if (extensionsSupported)
	{
		SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
		swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
	}

	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(device, &properties);
	VkPhysicalDeviceVulkan13Features features13{};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	VkPhysicalDeviceFeatures2 features{};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features.pNext = &features13;
	if (properties.apiVersion >= kApiVersion)
		vkGetPhysicalDeviceFeatures2(device, &features);

	return indices.isComplete() && extensionsSupported && swapChainAdequate &&
	       properties.apiVersion >= kApiVersion && features.features.samplerAnisotropy &&
	       features13.dynamicRendering && features13.synchronization2;
}


void DeviceContext::PickPhysicalDevice()
{
	uint32_t deviceCount = 0;

	vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
	if (deviceCount == 0)
	{
		throw std::runtime_error("failed to find GPUs with Vulkan support");
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

	for (const auto &device : devices)
	{
		if (IsDeviceSuitable(device))
		{
			m_physicalDevice = device;
			m_msaaSamples = GetMaxUsableSampleCount();
			break;
		}
	}

	if (m_physicalDevice == VK_NULL_HANDLE)
	{
		throw std::runtime_error(
		    "failed to find a suitable GPU (Vulkan 1.3 with dynamic rendering and synchronization2)");
	}
}


void DeviceContext::CreateLogicalDevice()
{
	QueueFamilyIndices familyIndicies = FindQueueFamilies(m_physicalDevice);

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<uint32_t> uniqueQueueFamilies = {
	    familyIndicies.graphicsFamily.value(), familyIndicies.presentFamily.value()};

	float queuePriority = 1.0f;
	for (uint32_t queueFamily : uniqueQueueFamilies)
	{
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	VkPhysicalDeviceVulkan13Features features13{};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;
	VkPhysicalDeviceFeatures2 deviceFeatures{};
	deviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	deviceFeatures.pNext = &features13;
	deviceFeatures.features.samplerAnisotropy = VK_TRUE;
	// Block compression where the device has it, which is every desktop GPU and lavapipe: Basis
	// Universal textures transcode to BC7 then, and to RGBA8, four times the memory, without it.
	VkPhysicalDeviceFeatures supported{};
	vkGetPhysicalDeviceFeatures(m_physicalDevice, &supported);
	VkFormatProperties bc7{};
	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_BC7_SRGB_BLOCK, &bc7);
	constexpr VkFormatFeatureFlags kSampled =
	    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
	m_blockCompression =
	    supported.textureCompressionBC == VK_TRUE && (bc7.optimalTilingFeatures & kSampled) == kSampled;
	deviceFeatures.features.textureCompressionBC = m_blockCompression ? VK_TRUE : VK_FALSE;

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = &deviceFeatures;
	createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
	createInfo.pQueueCreateInfos = queueCreateInfos.data();

	createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = deviceExtensions.data();

	if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_logicalDevice) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create logical m_logicalDevice");
	}

	vkGetDeviceQueue(m_logicalDevice, familyIndicies.graphicsFamily.value(), 0, &m_graphicsQueue);
	vkGetDeviceQueue(m_logicalDevice, familyIndicies.presentFamily.value(), 0, &m_presentQueue);
}


void DeviceContext::WaitOnWindowResized() const
{
	int width{0};
	int height{0};

	// Initially test the size...
	glfwGetFramebufferSize(m_pWindow->GetGLFWWindow(), &width, &height);

	// Idle here until it's got a sensible values.
	while (width == 0 || height == 0)
	{
		glfwGetFramebufferSize(m_pWindow->GetGLFWWindow(), &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(m_logicalDevice);
}


VkFormat DeviceContext::FindSupportedFormat(
    const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
{
	for (VkFormat format : candidates)
	{
		VkFormatProperties props{};
		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

		// Which feature set to test depends on the tiling, and both branches answered with
		// the same format, which is what clang-tidy's repeated-branch finding saw. A tiling
		// outside these two has no feature set to test, so the candidate cannot match.
		const VkFormatFeatureFlags *available = nullptr;
		if (tiling == VK_IMAGE_TILING_LINEAR)
		{
			available = &props.linearTilingFeatures;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL)
		{
			available = &props.optimalTilingFeatures;
		}

		if (available != nullptr && (*available & features) == features)
		{
			return format;
		}
	}

	throw std::runtime_error("failed to find supported format");
}


VkSurfaceFormatKHR DeviceContext::FindSupportedSurfaceFormat(
    const std::vector<VkFormat> &candidates, VkColorSpaceKHR requestedColourSpace)
{
	// Discover what surface formats are available.
	uint32_t availableCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &availableCount, nullptr);
	std::vector<VkSurfaceFormatKHR> availableFormats;
	availableFormats.resize(availableCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(
	    m_physicalDevice, m_surface, &availableCount, availableFormats.data());

	// A surface that reports no formats, and an empty request list, are hard failures. The
	// branches below index availableFormats[0] without asking, so ask once, here.
	if (availableCount == 0 || candidates.empty())
	{
		throw std::runtime_error("failed to find supported surface format");
	}

	// First check if only one format, VK_FORMAT_UNDEFINED, is available, which would imply that any
	// format is available.
	if (availableCount == 1)
	{
		if (availableFormats[0].format == VK_FORMAT_UNDEFINED)
		{
			VkSurfaceFormatKHR ret;
			ret.format = candidates[0];
			ret.colorSpace = requestedColourSpace;
			return ret;
		}
		else
		{
			// No point in searching another format.
			return availableFormats[0];
		}
	}
	else
	{
		// Request several formats, the first found will be used
		for (VkFormat candidate : candidates)
		{
			for (uint32_t availableIndex = 0; availableIndex < availableCount; availableIndex++)
			{
				if (availableFormats[availableIndex].format == candidate &&
				    availableFormats[availableIndex].colorSpace == requestedColourSpace)
					return availableFormats[availableIndex];
			}
		}

		// If none of the requested image formats could be found, use the first available
		return availableFormats[0];
	}
}


VkFormat DeviceContext::FindDepthFormat()
{
	return FindSupportedFormat(
	    {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
	    VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}


VkPresentModeKHR DeviceContext::FindSupportedPresentMode(const std::vector<VkPresentModeKHR> candidates)
{
	// Discover what surface present modes are available.
	uint32_t availableCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &availableCount, nullptr);
	std::vector<VkPresentModeKHR> availableModes;
	availableModes.resize(availableCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(
	    m_physicalDevice, m_surface, &availableCount, availableModes.data());

	for (const auto &candidate : candidates)
	{
		for (uint32_t availableIndex = 0; availableIndex < availableCount; availableIndex++)
		{
			if (candidate == availableModes[availableIndex])
				return candidate;
		}
	}

	// This mode is always available.
	return VK_PRESENT_MODE_FIFO_KHR;
}


bool DeviceContext::HasStencilComponent(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


VkSampleCountFlagBits DeviceContext::GetMaxUsableSampleCount()
{
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(m_physicalDevice, &physicalDeviceProperties);

	VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts &
	                            physicalDeviceProperties.limits.framebufferDepthSampleCounts;
	if (counts & kMaxMsaaSamples)
	{
		return kMaxMsaaSamples;
	}
	if (counts & VK_SAMPLE_COUNT_4_BIT)
	{
		return VK_SAMPLE_COUNT_4_BIT;
	}
	if (counts & VK_SAMPLE_COUNT_2_BIT)
	{
		return VK_SAMPLE_COUNT_2_BIT;
	}

	return VK_SAMPLE_COUNT_1_BIT;
}


VkImageView DeviceContext::CreateImageView(
    VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView imageView;
	if (vkCreateImageView(m_logicalDevice, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create texture image view!");
	}

	return imageView;
}


VkShaderModule DeviceContext::CreateShaderModule(
    std::span<const std::byte> code, const std::string &name)
{
	// SPIR-V is a stream of 32-bit words, and pCode is read as one: a file whose size is not a
	// whole number of words is not SPIR-V, and the storage it came in has to be aligned for them
	// (the VFS reads into a std::vector, whose storage comes from operator new).
	if (code.size() % sizeof(uint32_t) != 0 ||
	    reinterpret_cast<std::uintptr_t>(code.data()) % alignof(uint32_t) != 0)
		throw std::runtime_error("shader module '" + name + "' is not SPIR-V");
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(m_logicalDevice, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create shader module '" + name + "'");
	}

	return shaderModule;
}


VkCommandBuffer DeviceContext::BeginSingleTimeCommands()
{
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = m_commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(m_logicalDevice, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}


void DeviceContext::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
{
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(m_graphicsQueue);

	vkFreeCommandBuffers(m_logicalDevice, m_commandPool, 1, &commandBuffer);
}


void DeviceContext::TransitionImageLayout(
    VkImage image, VkFormat /*format*/, VkImageLayout oldLayout, VkImageLayout newLayout,
    uint32_t mipLevels)
{
	VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (
	    oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
	    newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else
	{
		throw std::invalid_argument("unsupported layout transition");
	}

	vkCmdPipelineBarrier(
	    commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	EndSingleTimeCommands(commandBuffer);
}


void DeviceContext::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
	VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

	VkBufferCopy copyRegion{};
	copyRegion.size = size;
	vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

	EndSingleTimeCommands(commandBuffer);
}


void DeviceContext::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
	VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;

	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;

	region.imageOffset = {0, 0, 0};
	region.imageExtent = {width, height, 1};

	vkCmdCopyBufferToImage(
	    commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	EndSingleTimeCommands(commandBuffer);
}


void DeviceContext::CreateSurface()
{
	if (glfwCreateWindowSurface(m_instance, GetWindow()->GetGLFWWindow(), nullptr, &m_surface) !=
	    VK_SUCCESS)
	{
		throw std::runtime_error("failed to create window surface");
	}
}


void DeviceContext::CreateCommandPool()
{
	QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

	if (vkCreateCommandPool(m_logicalDevice, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create command pool");
	}
}
} // namespace Jettison::Render
