#include "Renderer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
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
		vkDestroySemaphore(m_pDeviceContext->GetLogicalDevice(), m_renderFinishedSemaphores[i], nullptr);
		vkDestroySemaphore(m_pDeviceContext->GetLogicalDevice(), m_imageAvailableSemaphores[i], nullptr);
		vkDestroyFence(m_pDeviceContext->GetLogicalDevice(), m_inFlightFences[i], nullptr);
	}
}


void Renderer::CreateSyncObjects()
{
	m_imageAvailableSemaphores.resize(kMaxFramesInFlight);
	m_renderFinishedSemaphores.resize(kMaxFramesInFlight);
	m_inFlightFences.resize(kMaxFramesInFlight);
	m_imagesInFlight.resize(m_pSwapchain->GetImageCount(), VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphoreInfo {};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < kMaxFramesInFlight; ++i)
	{
		if (vkCreateSemaphore(m_pDeviceContext->GetLogicalDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS
			|| vkCreateSemaphore(m_pDeviceContext->GetLogicalDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS
			|| vkCreateFence(m_pDeviceContext->GetLogicalDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create semaphores");
		}
	}
}


void Renderer::DrawFrame()
{
	vkWaitForFences(m_pDeviceContext->GetLogicalDevice(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(m_pDeviceContext->GetLogicalDevice(), m_pSwapchain->GetVkSwapchainHandle(), 
		UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_pSwapchain->Recreate();
		m_pPipeline->Recreate();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		throw std::runtime_error("failed to aquire swap chain image");
	}

	if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
	{
		vkWaitForFences(m_pDeviceContext->GetLogicalDevice(), 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
	}

	m_imagesInFlight[imageIndex] = m_inFlightFences[m_currentFrame];

	UpdateUniformBuffer(imageIndex);

	VkSubmitInfo submitInfo {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
	VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_pPipeline->GetCommandBuffers()[imageIndex];

	VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	vkResetFences(m_pDeviceContext->GetLogicalDevice(), 1, &m_inFlightFences[m_currentFrame]);

	if (vkQueueSubmit(m_pDeviceContext->GetGraphicsQueue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to submit the draw command buffer");
	}

	VkPresentInfoKHR presentInfo {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;

	VkSwapchainKHR swapChains[] = {m_pSwapchain->GetVkSwapchainHandle()};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;

	result = vkQueuePresentKHR(m_pDeviceContext->GetPresentQueue(), &presentInfo);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_pWindow->HasBeenResized())
	{
		m_pSwapchain->Recreate();
		m_pPipeline->Recreate();
		m_pWindow->HasBeenResized(false);
	}
	else if (result != VK_SUCCESS)
	{
		throw std::runtime_error("failed to present swap chain image");
	}

	m_currentFrame = (m_currentFrame + 1) % kMaxFramesInFlight;
}


void Renderer::UpdateUniformBuffer(uint32_t currentImage)
{
	static auto startTime = std::chrono::high_resolution_clock::now();

	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

	ModelUBO ubo {};
	ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	ubo.projection = glm::perspective(glm::radians(45.0f), m_pSwapchain->GetExtents().width / static_cast<float>(m_pSwapchain->GetExtents().height), 0.1f, 10.0f);

	// Flip projection matrix on the y axis for Vulkan.
	ubo.projection[1][1] *= -1;

	void* data;
	vkMapMemory(m_pDeviceContext->GetLogicalDevice(), m_pPipeline->GetUniformBuffersMemory()[currentImage], 0, sizeof(ubo), 0, &data);
	memcpy(data, &ubo, sizeof(ubo));
	vkUnmapMemory(m_pDeviceContext->GetLogicalDevice(), m_pPipeline->GetUniformBuffersMemory()[currentImage]);
}
}
