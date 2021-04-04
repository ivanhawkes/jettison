#include "Window.h"

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <../glfw/include/GLFW/glfw3.h>


namespace Jettison::Renderer
{
constexpr uint32_t kWindowWidth = 1920;
constexpr uint32_t kWindowHeight = 1080;


void Window::Init()
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	m_glfwWindow = glfwCreateWindow(kWindowWidth, kWindowHeight, "Vulkan", nullptr, nullptr);
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

	return VkExtent2D {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}


void Window::OnFramebufferResize(GLFWwindow* m_glfwWindow, int width, int height)
{
	auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(m_glfwWindow));
	window->m_hasBeenResized = true;
}
}
