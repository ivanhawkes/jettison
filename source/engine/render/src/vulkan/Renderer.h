#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vulkan/DeviceContext.h"
#include "vulkan/GpuProfiler.h"
#include "vulkan/SceneDraws.h"
#include "vulkan/Swapchain.h"
#include "vulkan/Window.h"
#include <jettison/render/Camera.h>
#include <jettison/render/Clusters.h>
#include <jettison/render/Culling.h>
#include <jettison/render/DebugDraw.h>
#include <jettison/render/DrawBatches.h>
#include <jettison/render/FrameData.h>
#include <jettison/render/FrameStats.h>
#include <jettison/render/IRenderer.h>
#include <jettison/render/PassList.h>
#include <jettison/render/SceneView.h>
#include <jettison/render/Shadows.h>
#include <jettison/scene/Components.h>

namespace Jettison::Resource
{
class ResourceManager;
}
namespace Jettison::Scene
{
class Scene;
}
namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Render
{
class Environment;
class ImGuiRenderer;
class MaterialCache;
struct MaterialBinding;
class ModelCache;
class PipelineCache;

// The frame, as the declared pass list docs/architecture.md describes ("The frame"): the sun's
// shadow cascades, each a depth-only rendering of every draw into one layer of the shadow map; the
// cluster passes (the index list's allocator zeroed, then the clusters' bounds and the lights
// assigned to them, both in compute); an opaque pass into a multisampled HDR target, which draws the
// sky last over what no surface covered and is resolved into a single-sampled target as it ends; an
// output pass that tonemaps that into the swapchain, with the UI drawn over the result; and the
// cluster check's copy.
//
// The shadow map and the cluster grid's buffers are one set each, shared by the frames in flight as
// the render targets are.
// The bounds are kept between frames and rebuilt only in a frame whose projection, viewport or
// cluster range differs from the one they were built for.
//
// The scene's sky is also its environment (vulkan/Environment.h): it is baked when a scene
// loads and whenever the sky changes, before the frame that first draws it.
//
// What the CPU writes each frame lives once per frame in flight: the frame constants, the skin
// palette, the light list, the material parameters, and the command buffer that reads them; and
// what the cluster check copies out. A frame
// slot's fence is waited on before any of them is touched, so nothing a submission in flight reads is
// ever rewritten. The render targets are one set, shared by the frames in flight, because the pass
// list's barriers order each frame's first use of a target after the last frame's last use.
class Renderer
{
  public:
	static constexpr uint32_t kFramesInFlight = 2;
	static constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	// The light index list's budget (render/IRenderer.h), which the shell reports against.
	static constexpr std::uint32_t kLightIndicesPerCluster = kRendererLightIndicesPerCluster;
	static constexpr std::uint32_t kLightIndexCapacity = kRendererLightIndexCapacity;
	// The view depth past which no local light is assigned (docs/architecture.md, "The grid and the
	// infinite far plane"): a station's lights from further off are its emissive panels. Chosen
	// with the light budget, and tuned in 5e.
	static constexpr float kDefaultClusterRange = 2000.0f;
	// The sun's shadow distance (render/Shadows.h): the view depth past which the sun casts no
	// shadow. It covers a station seen whole, from its own docks to a few hundred metres out.
	static constexpr float kDefaultShadowDistance = 400.0f;
	// Each cascade's map, texels on a side.
	static constexpr std::uint32_t kShadowMapSize = 2048;
	// The pixels a level of detail's error may cover before a finer one is drawn.
	static constexpr float kDefaultLodThreshold = 1.0f;

	// The resource manager loads the models, materials and textures, and outlives the renderer;
	// Destroy releases everything the renderer loaded through it.
	Renderer(
	    std::shared_ptr<DeviceContext> pDeviceContext, std::shared_ptr<Window> pWindow,
	    std::shared_ptr<Swapchain> pSwapchain, const Vfs::FileSystem &assets,
	    Resource::ResourceManager &resources);
	~Renderer();

	Renderer(const Renderer &) = delete;
	Renderer &operator=(const Renderer &) = delete;

	// Builds the layouts, the pass list's plan, the targets, the frames in flight and every
	// pipeline the passes use, and registers the model, texture and material loaders. A program missing
	// from the asset tree throws here, naming it; a material's program is built when the material
	// first draws, and one that is missing fails that material alone.
	void Init();

	// Releases everything Init made, and every model, material and texture handle. The device has to
	// be idle. Safe on a renderer whose Init threw part-way, and twice.
	void Destroy();

	// Records and submits the scene as it stands: the presentation phase has already animated it
	// and computed its world transforms (SceneUpdateSystem), so this frame only reads it. The
	// presentation time, in seconds, drives the materials' time-driven parameters. True when this
	// frame recreated the swapchain.
	bool Render(
	    const Scene::Scene &scene, const DebugDraw &debugDraw, ImGuiRenderer &imguiRenderer,
	    double presentationTime);

	// What the UI is drawn into: the swapchain's format, single-sampled.
	VkFormat GetOutputFormat() const
	{
		return m_pSwapchain->GetImageFormat();
	}

	// The linear scale applied to the HDR target on its way to the display, before tonemapping.
	void SetExposure(float exposure)
	{
		m_exposure = exposure;
	}
	void SetFade(const glm::vec3 &color, float amount)
	{
		m_fadeColor = color;
		m_fade = amount;
	}

	void SetDebugView(DebugView view)
	{
		m_debugView = view;
	}
	DebugView GetDebugView() const
	{
		return m_debugView;
	}

	// The grid's far end, in metres of view depth; it is never less than twice the near plane.
	void SetClusterRange(float range)
	{
		m_clusterRange = range;
	}

	// The view depth past which the sun casts no shadow, in metres; never less than twice the near
	// plane.
	void SetShadowDistance(float distance)
	{
		m_shadowSettings.distance = distance;
	}

	// The pixels a level of detail's error may cover where it is drawn (render/Culling.h).
	void SetLodThreshold(float pixels)
	{
		m_lodThreshold = pixels;
	}

	// Whether the opaque surfaces' depth is laid down before they are shaded, so each pixel is shaded
	// once, by the surface that ends up in it (docs/architecture.md, "The frame").
	void SetDepthPrepass(bool enabled)
	{
		m_depthPrepass = enabled;
	}
	bool GetDepthPrepass() const
	{
		return m_depthPrepass;
	}

	// The last frame's counts, and the GPU's time in each pass (vulkan/GpuProfiler.h).
	const FrameStats &GetFrameStats() const
	{
		return m_frameStats;
	}
	const std::vector<PassTiming> &GetPassTimings() const;
	std::uint64_t GetMeasuredFrames() const;
	void ResetPassTimings();

	// Each frame's light assignment copied out and compared with the CPU's (CheckClusterAssignment),
	// once the frame's fence says it is done. The smoke test turns it on; it costs a copy of the grid
	// and the index list a frame.
	void SetClusterCheck(bool enabled)
	{
		m_checkClusters = enabled;
	}
	const ClusterCheckTotals &GetClusterChecks() const
	{
		return m_clusterChecks;
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
		Buffer lines{};
		Buffer materials{};
		Buffer instances{};
		VkDescriptorSet frameSet{VK_NULL_HANDLE};
		// The cluster check: the grid and the index list as this slot's frame left them, and what
		// the CPU needs to assign the same lights. Pending from the frame that copied them out until
		// the slot's fence is next waited on.
		Buffer gridReadback{};
		Buffer indexReadback{};
		bool checkPending{false};
		ClusterView checkView{};
		glm::mat3 checkViewMatrix{1.0f};
		std::vector<GpuLight> checkLights;
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
	void CreateClusterBuffers();
	void DestroyClusterBuffers();
	void CreateShadowMap();
	void DestroyShadowMap();
	void CreateFrames();
	void WriteFrameSet(const FrameData &frame);
	void WriteOutputSet();
	void CreatePipelines();
	void CreateRenderFinishedSemaphores();
	// The scene's sky, baked into the environment if it is not the one there already, or if the scene
	// was replaced; every frame set then names the new environment.
	void UpdateEnvironment(const entt::registry &sceneRegistry, bool sceneReplaced);
	void DestroyRenderFinishedSemaphores();
	void RecreateSwapchain();

	// Grows an upload buffer of a frame slot to hold the bytes asked for, after the slot's fence.
	void EnsureCapacity(
	    FrameData &frame, Buffer &buffer, VkDeviceSize bytes, VkBufferUsageFlags usage,
	    const char *name);
	// Each draw's level of detail, and the planets that hide what is behind them, from this frame's
	// view.
	void SelectLods(const std::vector<SceneDrawCommand> &draws);
	// The frame's local lights into the slot's light list: camera-relative, and only those whose
	// range reaches into the cluster range, the view's frustum, and out from behind every planet.
	void WriteLights(FrameData &frame, const entt::registry &sceneRegistry, const CameraView &camera);
	// The view's draws and each cascade's casters, culled and batched, and every batch's instances
	// into the slot's instance buffer.
	void WriteInstances(
	    FrameData &frame, const std::vector<SceneDrawCommand> &draws,
	    const std::vector<uint32_t> &jointOffsets);
	// The slot's last frame's assignment against the CPU's, now that its fence has been waited on.
	void CheckClusters(FrameData &frame);
	// The frame's debug lines into the slot's vertex buffer; how many are depth-tested.
	uint32_t WriteLines(FrameData &frame, const DebugDraw &debugDraw, const glm::dvec3 &camera);
	void WriteFrame(
	    FrameData &frame, const entt::registry &sceneRegistry, const CameraView &camera,
	    const std::vector<SceneDrawCommand> &draws, std::vector<uint32_t> &jointOffsets);
	// Each draw's named material, or null for the model's own, and the frame's parameters into the
	// slot's material buffer.
	void WriteMaterials(FrameData &frame, const std::vector<SceneDrawCommand> &draws, double time);
	void Record(FrameData &frame, uint32_t imageIndex, const ImGuiRenderer &imguiRenderer);
	void RecordShadows(const FrameData &frame, VkCommandBuffer commandBuffer) const;
	void RecordDepthPrepass(const FrameData &frame, VkCommandBuffer commandBuffer) const;
	// The batches' vertex stages alone: the shadow cascades and the depth prepass, which bind no
	// material.
	void RecordDepthBatches(
	    VkCommandBuffer commandBuffer, const std::vector<DrawBatch> &batches,
	    std::uint32_t cascade) const;
	void RecordClusterBounds(const FrameData &frame, VkCommandBuffer commandBuffer);
	void RecordClusterLights(const FrameData &frame, VkCommandBuffer commandBuffer) const;
	void RecordClusterReadback(FrameData &frame, VkCommandBuffer commandBuffer) const;
	void RecordOpaque(const FrameData &frame, VkCommandBuffer commandBuffer);
	void RecordOutput(
	    VkCommandBuffer commandBuffer, uint32_t imageIndex, const ImGuiRenderer &imguiRenderer);
	void SetViewport(VkCommandBuffer commandBuffer) const;

	bool Present();

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	std::shared_ptr<Window> m_pWindow;
	std::shared_ptr<Swapchain> m_pSwapchain;
	const Vfs::FileSystem &m_assets;

	Resource::ResourceManager &m_resources;

	std::unique_ptr<PipelineCache> m_pPipelines;
	std::unique_ptr<MaterialCache> m_pMaterials;
	std::unique_ptr<ModelCache> m_pModels;
	std::unique_ptr<Environment> m_pEnvironment;
	std::unique_ptr<GpuProfiler> m_pProfiler;
	// The sky the environment was baked from, or empty for the black one.
	std::optional<Scene::SkyComponent> m_bakedSky;

	// Set 0 of the scene and cluster programs (constants, skin palette, lights, material
	// parameters, the environment, the cluster grid, the shadow map), set 1 of the scene programs
	// (a material's textures), and set 0 of the output program (the resolved HDR target).
	VkDescriptorSetLayout m_frameSetLayout{VK_NULL_HANDLE};
	VkDescriptorSetLayout m_materialSetLayout{VK_NULL_HANDLE};
	VkDescriptorSetLayout m_outputSetLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_meshLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_outputLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_lineLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_clusterLayout{VK_NULL_HANDLE};
	VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
	VkDescriptorSet m_outputSet{VK_NULL_HANDLE};
	VkSampler m_outputSampler{VK_NULL_HANDLE};
	VkPipeline m_outputPipeline{VK_NULL_HANDLE};
	VkPipeline m_skyPipeline{VK_NULL_HANDLE};
	VkPipeline m_linePipeline{VK_NULL_HANDLE};
	VkPipeline m_lineOverlayPipeline{VK_NULL_HANDLE};
	VkPipeline m_clusterBoundsPipeline{VK_NULL_HANDLE};
	VkPipeline m_clusterLightsPipeline{VK_NULL_HANDLE};
	VkPipeline m_shadowPipeline{VK_NULL_HANDLE};
	VkPipeline m_prepassPipeline{VK_NULL_HANDLE};

	// The sun's shadow map: one layer per cascade, a view of the whole array for the lighting
	// include, one of each layer to render into, and the comparison sampler. Shared by the frames in
	// flight; its size does not depend on the window's.
	VkFormat m_shadowFormat{VK_FORMAT_UNDEFINED};
	Image m_shadowMap{};
	VkImageView m_shadowArrayView{VK_NULL_HANDLE};
	std::array<VkImageView, kShadowCascades> m_shadowLayerViews{};
	VkSampler m_shadowSampler{VK_NULL_HANDLE};
	ShadowSettings m_shadowSettings{.distance = kDefaultShadowDistance, .resolution = kShadowMapSize};
	// Whether this frame's cascades have anything to draw: a scene with no sun clears them only.
	bool m_frameShadows{false};
	SunShadows m_frameCascades{};

	// This frame's view, each draw's level of detail and the planets that hide what is behind them;
	// the batches the view and each cascade draw, whose instances are runs of the frame's instance
	// buffer.
	Frustum m_frameFrustum{};
	LodSelection m_frameLods{};
	std::vector<std::uint32_t> m_drawLods;
	std::vector<HorizonOccluder> m_occluders;
	std::vector<DrawBatch> m_viewBatches;
	std::array<std::vector<DrawBatch>, kShadowCascades> m_shadowBatches;
	std::vector<BatchInput> m_batchScratch;
	std::vector<std::uint32_t> m_instanceDraws;
	std::vector<GpuInstance> m_instanceScratch;
	float m_lodThreshold{kDefaultLodThreshold};
	bool m_depthPrepass{false};
	FrameStats m_frameStats{};

	// The cluster grid: each cluster's view-space bounds, its offset and count into the index list,
	// and the list, its allocator first. Device-local, shared by the frames in flight.
	Buffer m_clusterBounds{};
	Buffer m_clusterGrid{};
	Buffer m_lightIndices{};
	// What the bounds were built for, empty until they are; and this frame's grid.
	std::optional<ClusterView> m_clusterBoundsView;
	ClusterView m_frameClusterView{};
	std::vector<GpuLight> m_lightScratch;
	float m_clusterRange{kDefaultClusterRange};
	DebugView m_debugView{DebugView::Shaded};
	bool m_checkClusters{false};
	ClusterCheckTotals m_clusterChecks;
	// This frame's line vertices: the depth-tested ones, then the overlay.
	uint32_t m_lineVertices{0};
	uint32_t m_testedLineVertices{0};
	std::vector<LineVertex> m_lineScratch;
	// This frame's material parameters, and each draw's named material (null for the model's own).
	std::vector<glm::vec4> m_materialParameters;
	std::vector<const MaterialBinding *> m_drawMaterials;

	PassList m_passes;
	PassPlan m_plan;
	PassResource m_shadowMapResource;
	PassResource m_hdrMsaaResource;
	PassResource m_depthResource;
	PassResource m_hdrResource;
	PassResource m_swapchainResource;
	PassResource m_clusterBoundsResource;
	PassResource m_clusterGridResource;
	PassResource m_lightIndicesResource;
	uint32_t m_shadowPass{0};
	uint32_t m_prepassPass{0};
	uint32_t m_clusterResetPass{0};
	uint32_t m_clusterBoundsPass{0};
	uint32_t m_clusterLightsPass{0};
	uint32_t m_opaquePass{0};
	uint32_t m_outputPass{0};
	uint32_t m_clusterReadbackPass{0};

	VkFormat m_depthFormat{VK_FORMAT_UNDEFINED};
	Targets m_targets;
	float m_exposure{1.0f};
	glm::vec3 m_fadeColor{0.0f};
	float m_fade{0.0f};

	std::array<FrameData, kFramesInFlight> m_frames{};
	uint32_t m_currentFrame{0};
	// The frames submitted so far: the number of the frame being recorded. A frame slot's fence,
	// once waited on, says every frame up to this one less kFramesInFlight is done.
	std::uint64_t m_frameNumber{0};
	// The swapchain image the current frame slot acquired. Render's acquire writes it and Present
	// reads it, one frame at a time.
	uint32_t m_presentImageIndex{0};
	// Per swapchain image: the semaphore its present waits on, and the fence of the frame that last
	// rendered into it.
	std::vector<VkSemaphore> m_renderFinished;
	std::vector<VkFence> m_imagesInFlight;

	// The generation of the scene the last frame drew (Scene::Scene::Generation): a change says its
	// contents were replaced, so the environment is rebaked and the models and materials the new
	// contents do not draw are released. Empty until the first frame.
	std::optional<std::uint64_t> m_sceneGeneration{};
};
} // namespace Jettison::Render
