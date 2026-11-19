#pragma once

#include <vulkan/vulkan.h>

struct GLFWwindow;


namespace Jettison::Render
{
class Window
{
  public:
	Window() = default;

	Window(const Window &) = delete;

	Window &operator=(const Window &) = delete;

	// Offscreen, on GLFW's null platform: a window with a size, input callbacks and iconify, and no
	// display, so nothing appears on any desktop and the renderer draws with no surface. The null
	// platform has no surface any NVIDIA driver can make (it would need VK_EXT_headless_surface).
	void Init(bool offscreen);

	void Destroy();

	GLFWwindow *GetGLFWWindow() const
	{
		return m_glfwWindow;
	}

	bool HasBeenResized() const
	{
		return m_hasBeenResized;
	}

	void HasBeenResized(bool hasBeenResized)
	{
		m_hasBeenResized = hasBeenResized;
	}

	VkExtent2D GetWindowExtents() const;

	bool IsOffscreen() const
	{
		return m_offscreen;
	}

  private:
	static void OnFramebufferResize(GLFWwindow *m_glfwWindow, int width, int height);

	GLFWwindow *m_glfwWindow{nullptr};

	bool m_hasBeenResized{false};
	bool m_offscreen{false};
};
} // namespace Jettison::Render
