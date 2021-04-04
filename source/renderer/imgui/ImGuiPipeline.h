#pragma once

#include <vulkan/vulkan.h>


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
