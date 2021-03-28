#pragma once

#include <vulkan/vulkan.h>

// GL Math.
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <../glm/glm/glm.hpp>
#include <../glm/glm/gtc/matrix_transform.hpp>
#include <../glm/glm/gtx/hash.hpp>


namespace Jettison::Renderer
{
class RenderPass
{
public:
	// Disable copying.
	RenderPass() = default;
	RenderPass(const RenderPass&) = delete;
	RenderPass& operator=(const RenderPass&) = delete;
};
}
