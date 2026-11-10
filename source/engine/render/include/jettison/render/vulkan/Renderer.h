#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <jettison/render/PassList.h>
#include <jettison/render/Scene.h>
#include <jettison/render/SceneSystems.h>
#include <jettison/render/vulkan/DeviceContext.h>
#include <jettison/render/vulkan/Swapchain.h>
#include <jettison/render/vulkan/Window.h>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Render
{
class ImGuiRenderer;
class MaterialCache;
class PipelineCache;

// The frame, as the declared pass list docs/architecture.md describes ("The frame"): an opaque
// pass into a multisampled HDR target, resolved into a single-sampled one, and an output pass that
// reads it into the swapchain, with the UI drawn over the result. Phase 5 adds its passes to the
// same list (shadows, the cluster compute passes, sky).
//
// What the CPU writes each frame lives once per frame in flight: the frame constants, the skin
// palette, the light list, and the command buffer that reads them. A frame slot's fence is waited
// on before any of them is touched, so nothing a submission in flight reads is ever rewritten.
// The render targets are one set, shared by the frames in flight, because the pass list's
// barriers order each frame's first use of a target after the last frame's last use.
class Renderer
{
  public:
	static constexpr uint32_t kFramesInFlight = 2;
	static constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

	Renderer(
	    std::shared_ptr<DeviceContext> pDeviceContext, std::shared_ptr<Window> pWindow,
	    std::shared_ptr<Swapchain> pSwapchain, const Vfs::FileSystem &assets);
	~Renderer();

	Renderer(const Renderer &) = delete;
	Renderer &operator=(const Renderer &) = delete;

	// Builds the layouts, the pass list's plan, the targets, the frames in flight and every
	// pipeline the passes use. A program missing from the asset tree throws here, naming it.
	void Init();

	// Releases everything Init made. Safe on a renderer whose Init threw part-way, and twice.
	void Destroy();

	// Records and submits the scene as it stands: the presentation phase has already animated it
	// and computed its world transforms (SceneUpdateSystem), so this frame only reads it. True
	// when this frame recreated the swapchain.
	bool Render(Scene &scene, const SceneRenderSystem &renderSystem, ImGuiRenderer &imguiRenderer);

	// What the UI is drawn into: the swapchain's format, single-sampled.
	VkFormat GetOutputFormat() const
	{
		return m_pSwapchain->GetImageFormat();
	}

	// The linear scale applied to the HDR target on its way to the display.
	void SetExposure(float exposure)
	{
		m_exposure = exposure;
	}

  private:
	struct FrameData
	{
		VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
		VkFence inFlight{VK_NULL_HANDLE};
		VkSemaphore imageAvailable{VK_NULL_HANDLE};
		Buffer constants{};
		Buffer joints{};
		Buffer lights{};
		VkDescriptorSet frameSet{VK_NULL_HANDLE};
	};

	struct Targets
	{
		VkExtent2D extent{0, 0};
		Image hdrMsaa{};
		VkImageView hdrMsaaView{VK_NULL_HANDLE};
		Image depth{};
		VkImageView depthView{VK_NULL_HANDLE};
		Image hdr{};
		VkImageView hdrView{VK_NULL_HANDLE};
	};

	void CreateLayouts();
	void CreatePassList();
	void CreateTargets();
	void DestroyTargets();
	void CreateFrames();
	void WriteFrameSet(const FrameData &frame);
	void WriteOutputSet();
	void CreatePipelines();
	void CreateRenderFinishedSemaphores();
	void DestroyRenderFinishedSemaphores();
	void RecreateSwapchain();

	// Grows an upload buffer of a frame slot to hold the bytes asked for, after the slot's fence.
	void EnsureCapacity(FrameData &frame, Buffer &buffer, VkDeviceSize bytes, const char *name);
	void WriteFrame(
	    FrameData &frame, const entt::registry &sceneRegistry,
	    const std::vector<SceneDrawCommand> &draws, std::vector<uint32_t> &jointOffsets);
	void Record(
	    const FrameData &frame, uint32_t imageIndex, const std::vector<SceneDrawCommand> &draws,
	    const std::vector<uint32_t> &jointOffsets, const ImGuiRenderer &imguiRenderer);
	void RecordOpaque(
	    const FrameData &frame, VkCommandBuffer commandBuffer,
	    const std::vector<SceneDrawCommand> &draws, const std::vector<uint32_t> &jointOffsets);
	void RecordOutput(
	    VkCommandBuffer commandBuffer, uint32_t imageIndex, const ImGuiRenderer &imguiRenderer);
	void SetViewport(VkCommandBuffer commandBuffer) const;

	bool Present();

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	std::shared_ptr<Window> m_pWindow;
	std::shared_ptr<Swapchain> m_pSwapchain;
	const Vfs::FileSystem &m_assets;

	std::unique_ptr<PipelineCache> m_pPipelines;
	std::unique_ptr<MaterialCache> m_pMaterials;

	// Set 0 of the scene programs (constants, skin palette, lights), set 1 of the mesh program (its
	// material), and set 0 of the output program (the resolved HDR target).
	VkDescriptorSetLayout m_frameSetLayout{VK_NULL_HANDLE};
	VkDescriptorSetLayout m_materialSetLayout{VK_NULL_HANDLE};
	VkDescriptorSetLayout m_outputSetLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_meshLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_outputLayout{VK_NULL_HANDLE};
	VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
	VkDescriptorSet m_outputSet{VK_NULL_HANDLE};
	VkSampler m_outputSampler{VK_NULL_HANDLE};
	VkPipeline m_meshPipeline{VK_NULL_HANDLE};
	VkPipeline m_outputPipeline{VK_NULL_HANDLE};

	PassList m_passes;
	PassPlan m_plan;
	PassResource m_hdrMsaaResource;
	PassResource m_depthResource;
	PassResource m_hdrResource;
	PassResource m_swapchainResource;
	uint32_t m_opaquePass{0};
	uint32_t m_outputPass{0};

	VkFormat m_depthFormat{VK_FORMAT_UNDEFINED};
	Targets m_targets;
	float m_exposure{1.0f};

	std::array<FrameData, kFramesInFlight> m_frames{};
	uint32_t m_currentFrame{0};
	// The swapchain image the current frame slot acquired. Render's acquire writes it and Present
	// reads it, one frame at a time.
	uint32_t m_presentImageIndex{0};
	// Per swapchain image: the semaphore its present waits on, and the fence of the frame that last
	// rendered into it.
	std::vector<VkSemaphore> m_renderFinished;
	std::vector<VkFence> m_imagesInFlight;

	// The scene generation the materials currently cached were built from. Empty until the first
	// frame, so a scene loaded before Init() costs nothing.
	std::optional<std::uint64_t> m_sceneGeneration{};
};
} // namespace Jettison::Render
