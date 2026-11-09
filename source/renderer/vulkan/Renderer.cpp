#include "Renderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>


namespace Jettison::Renderer
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


bool Renderer::Render(
    Scene &scene, const SceneUpdateSystem &updateSystem, const SceneRenderSystem &renderSystem,
    float deltaTime, ImGuiRenderer &imguiRenderer)
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
	if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
	{
		vkWaitForFences(
		    m_pDeviceContext->GetLogicalDevice(), 1, &m_imagesInFlight[imageIndex], VK_TRUE,
		    UINT64_MAX);
	}

	m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];

	updateSystem.Update(scene.Registry(), deltaTime);
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

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;

	VkSwapchainKHR swapChains[] = {m_pSwapchain->GetVkSwapchainHandle()};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;

	result = vkQueuePresentKHR(m_pDeviceContext->GetPresentQueue(), &presentInfo);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || m_pWindow->HasBeenResized())
	{
		RecreateSwapchain(imguiRenderer);
		m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
		return true;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		throw std::runtime_error("failed to present swap chain image");
	}

	m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
	return false;
}


void Renderer::Present()
{
	// TODO: Split it out into Present() as well?
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
} // namespace Jettison::Renderer
