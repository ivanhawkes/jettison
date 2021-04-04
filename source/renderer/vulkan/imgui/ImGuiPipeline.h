#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"


namespace Jettison::Renderer
{
class ImGuiPipeline
{
public:
	// Disable copying.
	ImGuiPipeline() = default;
	ImGuiPipeline(const ImGuiPipeline&) = delete;
	ImGuiPipeline& operator=(const ImGuiPipeline&) = delete;
};
}
