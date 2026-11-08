#include "Window.h"

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdexcept>
#include <string>


namespace Jettison::Renderer
{
constexpr uint32_t kWindowWidth = 1920;
constexpr uint32_t kWindowHeight = 1080;


void Window::Init()
{
	if (!glfwInit())
	{
		const char *description = nullptr;
		glfwGetError(&description);
		throw std::runtime_error(
		    std::string("Failed to initialize GLFW: ") + (description ? description : "unknown error"));
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	m_glfwWindow = glfwCreateWindow(kWindowWidth, kWindowHeight, "Vulkan", nullptr, nullptr);
	if (!m_glfwWindow)
	{
		const char *description = nullptr;
		glfwGetError(&description);
		std::string message = std::string("Failed to create GLFW window: ") +
		                      (description ? description : "unknown error");
		glfwTerminate();
		throw std::runtime_error(message);
	}
	glfwSetWindowUserPointer(m_glfwWindow, this);
	glfwSetFramebufferSizeCallback(m_glfwWindow, OnFramebufferResize);
}


void Window::Destroy()
{
	glfwDestroyWindow(m_glfwWindow);
	glfwTerminate();
}


VkExtent2D Window::GetWindowExtents() const
{
	int width;
	int height;

	glfwGetFramebufferSize(m_glfwWindow, &width, &height);

	return VkExtent2D{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}


void Window::OnFramebufferResize(GLFWwindow *m_glfwWindow, int width, int height)
{
	auto window = reinterpret_cast<Window *>(glfwGetWindowUserPointer(m_glfwWindow));
	window->m_hasBeenResized = true;
}
} // namespace Jettison::Renderer
