#pragma once

#include <vulkan/vulkan.h>

// Forward declarations.
struct GLFWwindow;


namespace Jettison::Renderer
{
class Window
{
public:
	// Disable copying.
	Window() = default;
	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	void Init();

	void Destroy();

	GLFWwindow* GetGLFWWindow() const { return m_glfwWindow; }

	bool HasBeenResized() const { return m_hasBeenResized; }

	void HasBeenResized(bool hasBeenResized) { m_hasBeenResized = hasBeenResized; }

	VkExtent2D GetWindowExtents() const;

private:
	static void OnFramebufferResize(GLFWwindow* m_glfwWindow, int width, int height);

	GLFWwindow* m_glfwWindow {nullptr};

	bool m_hasBeenResized {false};
};
}
