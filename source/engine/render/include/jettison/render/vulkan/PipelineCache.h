#pragma once

#include <vulkan/vulkan.h>

#include <compare>
#include <map>
#include <memory>
#include <string>

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
	None, // generated from the vertex index: the fullscreen triangle
	Mesh, // Render::Vertex
	Line, // Render::LineVertex
};

enum class BlendMode
{
	Opaque,
	// Straight alpha, composited "over": source alpha, one minus source alpha.
	Alpha,
};

// Everything about a graphics pipeline other than its shaders and layout: the vertex stream, the
// rasteriser, the depth test, blending, and the attachments dynamic rendering will give it.
// Viewport and scissor are dynamic state, so no pipeline depends on the window's size and a
// resize rebuilds none of them.
struct RenderState
{
	VertexLayout vertices{VertexLayout::Mesh};
	VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkCullModeFlags cullMode{VK_CULL_MODE_BACK_BIT};
	bool depthTest{true};
	bool depthWrite{true};
	VkCompareOp depthCompare{VK_COMPARE_OP_LESS};
	BlendMode blend{BlendMode::Opaque};
	VkFormat colorFormat{VK_FORMAT_UNDEFINED};
	VkFormat depthFormat{VK_FORMAT_UNDEFINED};
	VkSampleCountFlagBits samples{VK_SAMPLE_COUNT_1_BIT};

	friend auto operator<=>(const RenderState &, const RenderState &) = default;
};

// A program is a name: "mesh" is shaders/mesh.vert.spv and shaders/mesh.frag.spv, found through
// the asset tree's mounts, so a mod can ship a program over the base one. The layout is the
// renderer's, made once for its lifetime, so keying on its handle cannot outlive it.
struct PipelineKey
{
	std::string program;
	RenderState state;
	VkPipelineLayout layout{VK_NULL_HANDLE};

	friend auto operator<=>(const PipelineKey &, const PipelineKey &) = default;
};

// Graphics pipelines by program and render state, made the first time each is asked for and kept
// until Destroy. The renderer asks for its passes' pipelines at start-up, so a missing or broken
// program is a start-up error naming its file, not a frame that throws.
class PipelineCache
{
  public:
	PipelineCache(std::shared_ptr<DeviceContext> pDeviceContext, const Vfs::FileSystem &assets);

	PipelineCache(const PipelineCache &) = delete;
	PipelineCache &operator=(const PipelineCache &) = delete;

	VkPipeline Get(const PipelineKey &key);

	std::size_t Size() const
	{
		return m_pipelines.size();
	}

	void Destroy();

  private:
	VkShaderModule LoadModule(const std::string &virtualPath);
	VkPipeline Create(const PipelineKey &key);

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	const Vfs::FileSystem &m_assets;
	std::map<PipelineKey, VkPipeline> m_pipelines;
};
} // namespace Jettison::Render
