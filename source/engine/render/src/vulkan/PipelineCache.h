#pragma once

#include <vulkan/vulkan.h>

#include <compare>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Render
{
class DeviceContext;

// The vertex stream a program reads.
enum class VertexLayout
{
	None,    // generated from the vertex index: the fullscreen triangle
	Mesh,    // Render::Vertex
	Line,    // Render::LineVertex
	Overlay, // Render::OverlayVertex
};

enum class BlendMode
{
	Opaque,
	// Straight alpha, composited "over": source alpha, one minus source alpha.
	Alpha,
	// Alpha premultiplied into the colour, composited "over": one, one minus source alpha.
	Premultiplied,
	// Dual-source: the first output is the light added, the second what is let through of what is
	// behind, by colour (one, the second source's colour), so tinted glass tints what it is over.
	// The device's dualSrcBlend, which the renderer requires.
	Transmissive,
};

// Everything about a graphics pipeline other than its shaders and layout: the vertex stream, the
// rasteriser, the depth test, blending, and the attachments dynamic rendering will give it.
// Viewport and scissor are dynamic state, so no pipeline depends on the window's size and a
// resize rebuilds none of them. A state with no colour format is depth only, and its program is a
// vertex stage alone.
struct RenderState
{
	VertexLayout vertices{VertexLayout::Mesh};
	VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkCullModeFlags cullMode{VK_CULL_MODE_BACK_BIT};
	bool depthTest{true};
	bool depthWrite{true};
	VkCompareOp depthCompare{VK_COMPARE_OP_LESS};
	// Depth clamped to the viewport's range rather than clipped by it, so a caster nearer the sun
	// than a shadow map reaches is still drawn (the device is required to have depthClamp).
	bool depthClamp{false};
	// The rasteriser's depth bias, constant and by slope; none when both are zero.
	float depthBiasConstant{0.0f};
	float depthBiasSlope{0.0f};
	BlendMode blend{BlendMode::Opaque};
	VkFormat colorFormat{VK_FORMAT_UNDEFINED};
	VkFormat depthFormat{VK_FORMAT_UNDEFINED};
	VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};

	friend auto operator<=>(const RenderState &, const RenderState &) = default;
};

// A program is a name: "mesh" is shaders/mesh.vert.spv and shaders/mesh.frag.spv, found through
// the asset tree's mounts, so a mod can ship a program over the base one; a depth-only program,
// such as "shadow_depth", is its vertex stage alone. The layout is the
// renderer's, made once for its lifetime, so keying on its handle cannot outlive it.
struct PipelineKey
{
	std::string program;
	RenderState state;
	VkPipelineLayout layout{VK_NULL_HANDLE};

	friend auto operator<=>(const PipelineKey &, const PipelineKey &) = default;
};

// Pipelines by program and render state, made the first time each is asked for and kept until
// Destroy. The renderer asks for its passes' pipelines at start-up, so a missing or broken program
// is a start-up error naming its file, not a frame that throws.
class PipelineCache
{
  public:
	PipelineCache(std::shared_ptr<DeviceContext> pDeviceContext, const Vfs::FileSystem &assets);

	PipelineCache(const PipelineCache &) = delete;
	PipelineCache &operator=(const PipelineCache &) = delete;

	VkPipeline Get(const PipelineKey &key);
	// A compute program is one stage, shaders/<program>.comp.spv, with nothing else to vary.
	VkPipeline GetCompute(const std::string &program, VkPipelineLayout layout);

	std::size_t Size() const
	{
		return m_pipelines.size() + m_computePipelines.size();
	}

	void Destroy();

  private:
	VkShaderModule LoadModule(const std::string &virtualPath);
	VkPipeline Create(const PipelineKey &key);
	VkPipeline CreateCompute(const std::string &program, VkPipelineLayout layout);

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	const Vfs::FileSystem &m_assets;
	std::map<PipelineKey, VkPipeline> m_pipelines;
	std::map<std::pair<std::string, VkPipelineLayout>, VkPipeline> m_computePipelines;
};
} // namespace Jettison::Render
