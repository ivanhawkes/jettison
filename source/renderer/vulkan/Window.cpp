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

	m_window = glfwCreateWindow(kWindowWidth, kWindowHeight, "Vulkan", nullptr, nullptr);
	glfwSetWindowUserPointer(m_window, this);
	glfwSetFramebufferSizeCallback(m_window, OnFramebufferResize);
}


void Window::Destroy()
{
	glfwDestroyWindow(m_window);
	glfwTerminate();
}


void Window::OnFramebufferResize(GLFWwindow* m_window, int width, int height)
{
	// TODO: Is this really needed? Can I just flip the member variable since it's set to this anyway?
	auto window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(m_window));
	window->m_hasBeenResized = true;
}
}
