#include <jettison/render/vulkan/Renderer.h>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>


namespace Jettison::Render
{
constexpr int kMaxFramesInFlight = 2;


void Renderer::Init()
{
	InitVulkan();
}


void Renderer::InitVulkan()
{
	// Device initialisation.
	assert(m_pWindow != nullptr && m_pWindow->GetGLFWWindow() != nullptr);

	CreateSyncObjects();
}


void Renderer::Destroy()
{
	for (size_t i = 0; i < kMaxFramesInFlight; ++i)
	{
		vkDestroySemaphore(
		    m_pDeviceContext->GetLogicalDevice(), m_imageAvailableSemaphores[i], nullptr);
		vkDestroyFence(m_pDeviceContext->GetLogicalDevice(), m_inFlightFences[i], nullptr);
	}
	for (const auto semaphore : m_renderFinishedSemaphores)
		vkDestroySemaphore(m_pDeviceContext->GetLogicalDevice(), semaphore, nullptr);
}


void Renderer::CreateSyncObjects()
{
	m_imageAvailableSemaphores.resize(kMaxFramesInFlight);
	m_inFlightFences.resize(kMaxFramesInFlight);
	m_imagesInFlight.resize(m_pSwapchain->GetImageCount(), VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < kMaxFramesInFlight; ++i)
	{
		if (vkCreateSemaphore(
		        m_pDeviceContext->GetLogicalDevice(), &semaphoreInfo, nullptr,
		        &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
		    vkCreateFence(
		        m_pDeviceContext->GetLogicalDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]) !=
		        VK_SUCCESS)
		{
			throw std::runtime_error("failed to create semaphores");
		}
		// The two frame-loop waits in Renderer::Render read differently, one per frame and one
		// per image, and a message that names the fence is how a reader tells which one fired.
		m_pDeviceContext->SetDebugName(
		    m_imageAvailableSemaphores[i], "image-available-" + std::to_string(i));
		m_pDeviceContext->SetDebugName(m_inFlightFences[i], "in-flight-" + std::to_string(i));
	}
	CreateRenderFinishedSemaphores();
}


void Renderer::CreateRenderFinishedSemaphores()
{
	m_renderFinishedSemaphores.resize(m_pSwapchain->GetImageCount());
	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	for (auto &semaphore : m_renderFinishedSemaphores)
	{
		if (vkCreateSemaphore(
		        m_pDeviceContext->GetLogicalDevice(), &semaphoreInfo, nullptr, &semaphore) !=
		    VK_SUCCESS)
		{
			throw std::runtime_error("failed to create render-finished semaphore");
		}
	}
	for (size_t i = 0; i < m_renderFinishedSemaphores.size(); ++i)
		m_pDeviceContext->SetDebugName(
		    m_renderFinishedSemaphores[i], "render-finished-" + std::to_string(i));
}


void Renderer::RecreateRenderFinishedSemaphores()
{
	for (const auto semaphore : m_renderFinishedSemaphores)
		vkDestroySemaphore(m_pDeviceContext->GetLogicalDevice(), semaphore, nullptr);
	m_renderFinishedSemaphores.clear();
	CreateRenderFinishedSemaphores();
}


void Renderer::RecreateSwapchain(ImGuiRenderer &imguiRenderer)
{
	check_vk_result(m_pDeviceContext->WaitIdle());
	imguiRenderer.ShutdownVulkanBackend();
	m_pSwapchain->Recreate();
	m_pPipeline->Recreate();
	imguiRenderer.InitVulkanBackend(m_pPipeline->GetRenderPass(), m_pSwapchain->GetImageCount());
	RecreateRenderFinishedSemaphores();
	m_imagesInFlight.assign(m_pSwapchain->GetImageCount(), VK_NULL_HANDLE);
	m_pWindow->HasBeenResized(false);
}


bool Renderer::Render(Scene &scene, const SceneRenderSystem &renderSystem, ImGuiRenderer &imguiRenderer)
{
	vkWaitForFences(
	    m_pDeviceContext->GetLogicalDevice(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE,
	    UINT64_MAX);

	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(
	    m_pDeviceContext->GetLogicalDevice(), m_pSwapchain->GetVkSwapchainHandle(), UINT64_MAX,
	    m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		RecreateSwapchain(imguiRenderer);
		return true;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		throw std::runtime_error("failed to aquire swap chain image");
	}
	// Present reads this index, and it is the only writer: a recreate can change the image count
	// behind it, and the next acquire replaces it before the next present looks at it.
	m_presentImageIndex = imageIndex;
	if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
	{
		vkWaitForFences(
		    m_pDeviceContext->GetLogicalDevice(), 1, &m_imagesInFlight[imageIndex], VK_TRUE,
		    UINT64_MAX);
	}

	m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];

	// A model's Vulkan resources live under the address of the Model, and a scene reload
	// destroys models whose addresses the next allocation reuses. The scene says when that
	// happened. Freeing what is left needs the device idle, because the frame in flight may be
	// reading a descriptor set from a pool that is about to go; a reload is rare, so stalling is
	// the right trade against retiring pools behind a fence that cannot be named.
	if (m_sceneGeneration && *m_sceneGeneration != scene.Generation())
	{
		check_vk_result(m_pDeviceContext->WaitIdle());
		m_pPipeline->ForgetModelResources();
	}
	m_sceneGeneration = scene.Generation();

	UpdateUniformBuffer(imageIndex, scene.Registry());
	const auto drawCommands = renderSystem.Collect(scene.Registry());
	m_pPipeline->RecordCommandBuffer(imageIndex, drawCommands, imguiRenderer);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
	VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_pPipeline->GetCommandBuffers()[imageIndex];

	VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[imageIndex]};
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	vkResetFences(m_pDeviceContext->GetLogicalDevice(), 1, &m_inFlightFences[m_currentFrame]);

	if (vkQueueSubmit(
	        m_pDeviceContext->GetGraphicsQueue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]) !=
	    VK_SUCCESS)
	{
		throw std::runtime_error("failed to submit the draw command buffer");
	}

	// Everything this frame slot queued is submitted; the present that follows is the half that
	// learns the swapchain went out of date under it.
	return Present(imguiRenderer);
}


bool Renderer::Present(ImGuiRenderer &imguiRenderer)
{
	// The image this frame slot acquired, the render-finished semaphore of that image, and the
	// queue that was told to present it: Render ends by calling this, so a frame loop that
	// records several views into one image calls it once, after the last one it submitted.
	const uint32_t imageIndex = m_presentImageIndex;
	VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[imageIndex]};

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;

	VkSwapchainKHR swapChains[] = {m_pSwapchain->GetVkSwapchainHandle()};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;

	const VkResult result = vkQueuePresentKHR(m_pDeviceContext->GetPresentQueue(), &presentInfo);

	// Out of date is not one answer either side of a submit. Render's acquire returns early on it
	// with the frame slot untouched, because nothing was queued; the frame has been queued here,
	// so the slot is spent whether or not the swapchain survives the present, and it moves.
	const bool outOfDate = result == VK_ERROR_OUT_OF_DATE_KHR || m_pWindow->HasBeenResized();
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
		throw std::runtime_error("failed to present swap chain image");

	m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
	if (outOfDate)
	{
		RecreateSwapchain(imguiRenderer);
		return true;
	}
	return false;
}


void Renderer::UpdateUniformBuffer(uint32_t currentImage, const entt::registry &sceneRegistry)
{
	FrameUBO ubo{};
	ubo.ambientColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
	CameraComponent fallbackCamera;
	const CameraComponent *camera = &fallbackCamera;
	for (const entt::entity entity : sceneRegistry.view<const CameraComponent>())
	{
		const auto &candidate = sceneRegistry.get<CameraComponent>(entity);
		if (candidate.active)
		{
			camera = &candidate;
			break;
		}
	}

	const float pitch = camera->rotation.x;
	const float roll = camera->rotation.y;
	const float yaw = camera->rotation.z;
	const glm::vec3 direction{
	    -std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
	const glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
	const glm::vec3 right = glm::normalize(glm::cross(direction, worldUp));
	glm::vec3 up = glm::normalize(glm::cross(right, direction));
	if (roll != 0.0f)
		up = glm::vec3(glm::rotate(glm::mat4(1.0f), roll, direction) * glm::vec4(up, 0.0f));

	ubo.view = glm::lookAt(camera->position, camera->position + direction, up);
	ubo.cameraPosition = glm::vec4(camera->position, 1.0f);
	for (const entt::entity entity : sceneRegistry.view<const LightComponent>())
	{
		const auto &light = sceneRegistry.get<LightComponent>(entity);
		if (!light.active)
			continue;

		glm::vec3 position{0.0f};
		if (const auto *worldTransform = sceneRegistry.try_get<WorldTransformComponent>(entity))
			position = glm::vec3(worldTransform->matrix[3]);
		else if (const auto *transform = sceneRegistry.try_get<TransformComponent>(entity))
			position = transform->translation;
		ubo.lightPosition = glm::vec4(position, 1.0f);
		ubo.lightColorIntensity = glm::vec4(light.color, light.intensity);
		ubo.ambientColor = glm::vec4(light.ambientColor, 1.0f);
		break;
	}
	ubo.projection = glm::perspective(
	    glm::radians(camera->verticalFovDegrees),
	    static_cast<float>(m_pSwapchain->GetExtents().width) /
	        static_cast<float>(m_pSwapchain->GetExtents().height),
	    camera->nearPlane, camera->farPlane);

	// Flip projection matrix on the y axis for Vulkan.
	ubo.projection[1][1] *= -1;

	void *data;
	vkMapMemory(
	    m_pDeviceContext->GetLogicalDevice(), m_pPipeline->GetUniformBuffersMemory()[currentImage], 0,
	    sizeof(ubo), 0, &data);
	memcpy(data, &ubo, sizeof(ubo));
	vkUnmapMemory(
	    m_pDeviceContext->GetLogicalDevice(), m_pPipeline->GetUniformBuffersMemory()[currentImage]);
}
} // namespace Jettison::Render
