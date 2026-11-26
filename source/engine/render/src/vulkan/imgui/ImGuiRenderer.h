#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "vulkan/DeviceContext.h"
#include "vulkan/imgui/FontFaces.h"

struct GLFWwindow;

namespace Jettison::Vfs
{
class FileSystem;
}


namespace Jettison::Render
{
class ImGuiRenderer
{
  public:
	// The fonts are read through the assets' mounts, so they come from a pack as from a directory.
	ImGuiRenderer(std::shared_ptr<DeviceContext> pDeviceContext, const Vfs::FileSystem &assets)
	    : m_pDeviceContext{std::move(pDeviceContext)}, m_pAssets{&assets}
	{
	}

	ImGuiRenderer() = default;

	ImGuiRenderer(const ImGuiRenderer &) = delete;

	ImGuiRenderer &operator=(const ImGuiRenderer &) = delete;

	// The UI is drawn with dynamic rendering into one single-sampled attachment of colorFormat,
	// which is the output pass's: nothing about it depends on the swapchain's size or image count,
	// so a resize leaves the backend alone. frameCount is how many frames may be in flight, which
	// is how many sets of vertex buffers the backend cycles through.
	void Init(GLFWwindow *window, VkFormat colorFormat, uint32_t frameCount);

	// Whether the attachment the UI draws into encodes what it is given, which is what makes the dev
	// tools' theme hand its palette over in linear light (devtools/Theme.h): true of an sRGB format,
	// which is what the swapchain prefers, false of the UNORM fallback it takes when the compositor
	// asks for one. True before Init, when there is no attachment to speak of.
	bool ColoursLinearised() const
	{
		return m_coloursLinearised;
	}

	// The size the UI's fonts are rasterised at, and the size the theme's spacing and radii are set
	// against: the two have to agree, so one number is stated here and Theme.cpp says why 16 px.
	static constexpr float kFontSize = 16.0f;

	void ShutdownVulkanBackend();

	void Destroy();

	void BeginFrame();

	void EndFrame();

	void RenderDrawData(VkCommandBuffer commandBuffer) const;

  private:
	void CreateDescriptorPool();

	void InitVulkanBackend(VkFormat colorFormat, uint32_t frameCount);

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	const Vfs::FileSystem *m_pAssets{nullptr};

	// The dev tools' faces, which ImGui reads glyphs from as it draws them, for as long as its
	// context, and the faces themselves (vulkan/imgui/FontFaces.h).
	DevToolFaces m_faces;
	bool m_coloursLinearised{true};
	VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
	bool m_contextCreated{false};
	bool m_glfwBackendInitialized{false};
	bool m_vulkanBackendInitialized{false};
};

} // namespace Jettison::Render