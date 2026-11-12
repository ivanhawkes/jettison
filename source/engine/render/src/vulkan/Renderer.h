#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "vulkan/DeviceContext.h"
#include "vulkan/Environment.h"
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
#include <jettison/render/Particles.h>
#include <jettison/render/PassList.h>
#include <jettison/render/PostProcess.h>
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
class ImGuiRenderer;
class MaterialCache;
struct MaterialBinding;
class ModelCache;
class PipelineCache;
class Overlay;
class OverlayRenderer;

// The frame, as the declared pass list docs/architecture.md describes ("The frame"): the sun's
// shadow cascades, each a depth-only rendering of every draw into one layer of the shadow map; the
// cluster passes (the index list's allocator zeroed, then the clusters' bounds and the lights
// assigned to them, both in compute); the particles stepped, in compute; an opaque pass into a
// multisampled HDR target, which draws the sky over what no surface covered, then the transparent
// draws back to front, and is resolved into a single-sampled target as it ends; the post chain over
// that (docs/architecture.md, "Post-processing": ambient occlusion before the opaque pass, from the
// depth prepass, and bloom and the outlines after it); an output pass that tonemaps into the
// swapchain, with the UI drawn over the result; and the cluster check's copy.
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
	// The depth prepass's view-space normals, from [-1, 1] to [0, 1], and its object IDs as whole
	// numbers in a float (render/PostProcess.h): multisampled, as its depth is, and read at sample
	// zero.
	static constexpr VkFormat kNormalFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	static constexpr VkFormat kObjectIdFormat = VK_FORMAT_R32_SFLOAT;
	// The ambient access the ambient occlusion passes leave, and the jump flood's nearest seeds.
	static constexpr VkFormat kAccessFormat = VK_FORMAT_R8_UNORM;
	static constexpr VkFormat kSeedFormat = VK_FORMAT_R16G16_UINT;
	// Ambient occlusion's reach in metres, how dark its corners go, and the view depth past which it
	// occludes nothing (docs/architecture.md, "Ambient occlusion").
	static constexpr float kAoRadius = 2.0f;
	static constexpr float kAoIntensity = 1.0f;
	static constexpr float kAoDistance = 1000.0f;
	// The share of the frame's light bloom spreads (post.glsl's ApplyBloom).
	static constexpr float kDefaultBloom = 0.04f;
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
	// The pixels a draw has to be across to be drawn (TooSmallToSee). Measured in 9d against Ostra
	// Highport's dressing from 1.5 km: on lavapipe at 640 x 480 it leaves out 529 of 925 meshes, the
	// pads' paint, poles and lamps, and takes the opaque pass from 31 ms to 22 ms, every tiny
	// triangle having shaded a block of the lit program.
	static constexpr double kDetailPixels = 1.0;
	// The particle pool's slots, shared by every emitter (render/Particles.h): 4 MiB of the device's.
	static constexpr std::uint32_t kParticleSlots = 1u << 17;
	// The bloom chain's levels and the jump flood's steps, as post.glsl has them.
	static constexpr std::uint32_t kBloomLevelCount = kBloomLevels;
	static constexpr std::uint32_t kFloodStepCount = kFloodSteps;

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
	// The overlay is drawn over the tonemapped scene and under the dev tools.
	bool Render(
	    const Scene::Scene &scene, const DebugDraw &debugDraw, const Overlay &overlay,
	    ImGuiRenderer &imguiRenderer, double presentationTime);

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

	// Ambient occlusion over the opaque surfaces' image-based light, which runs the depth prepass
	// whenever it is on (docs/architecture.md, "Ambient occlusion").
	void SetAmbientOcclusion(bool enabled)
	{
		m_ambientOcclusion = enabled;
	}
	bool GetAmbientOcclusion() const
	{
		return m_ambientOcclusion;
	}

	// The share of the frame's light bloom spreads, 0 for none (docs/architecture.md, "Bloom").
	void SetBloom(float strength)
	{
		m_bloom = std::max(strength, 0.0f);
	}
	float GetBloom() const
	{
		return m_bloom;
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

	// The next frame drawn, as the output pass leaves it, written to a PNG once the GPU has drawn it
	// (render/Screenshot.h): kFramesInFlight frames later, or as the renderer is destroyed, so the run
	// may end on the frame after it asks. A second request before the next frame replaces the first.
	// Refused, having logged why, on a swapchain that cannot be copied from or is not of 8-bit
	// channels.
	void RequestScreenshot(std::filesystem::path path);

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
		// The celestial bodies with an atmosphere or rings (GpuBody).
		Buffer bodies{};
		// The particle emitters (GpuEmitter).
		Buffer emitters{};
		// The outlines drawn this frame (GpuOutline).
		Buffer outlines{};
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
		// A screenshot: the swapchain image as this slot's frame left it, copied out to be written
		// to its file once the slot's fence is next waited on (or as the renderer is destroyed).
		Buffer screenshotReadback{};
		std::filesystem::path screenshotPath;
		VkExtent2D screenshotExtent{0, 0};
		bool screenshotPending{false};
	};

	// An image and a view of it.
	struct Target
	{
		Image image{};
		VkImageView view{VK_NULL_HANDLE};
	};

	// The post passes, each with its own set of the images it reads (WritePostSets).
	enum PostSet : std::uint32_t
	{
		kPostSsao,
		kPostSsaoBlur,
		kPostBloomDown,
		kPostBloomUp = kPostBloomDown + kBloomLevelCount,
		kPostOutlineSeed = kPostBloomUp + kBloomLevelCount - 1,
		// The flood's two targets, each read by the step after the one that wrote it.
		kPostFloodFromFirst,
		kPostFloodFromSecond,
		kPostOutlines,
		kPostOutput,
		kPostSetCount
	};

	struct Targets
	{
		VkExtent2D extent{0, 0};
		Image hdrMsaa{};
		VkImageView hdrMsaaView{VK_NULL_HANDLE};
		// Multisampled depth and stencil: a view of both aspects to render with, and of the depth
		// alone for ambient occlusion to read.
		Image depth{};
		VkImageView depthView{VK_NULL_HANDLE};
		VkImageView depthSampleView{VK_NULL_HANDLE};
		Image hdr{};
		VkImageView hdrView{VK_NULL_HANDLE};
		// The depth prepass's normals and object IDs, multisampled.
		Target normals;
		Target objectIds;
		// The depth and stencil resolved to sample zero as the opaque pass ends, for the post passes
		// to test against, and a view of the stencil alone for them to read.
		Image resolved{};
		VkImageView resolvedView{VK_NULL_HANDLE};
		VkImageView resolvedStencilView{VK_NULL_HANDLE};
		// Ambient access, before and after its blur.
		Target access;
		Target accessBlurred;
		std::array<Target, kBloomLevelCount> bloom{};
		std::array<VkExtent2D, kBloomLevelCount> bloomExtents{};
		std::array<Target, 2> flood{};
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
	void WritePostSets();
	void CreatePipelines();
	void CreateRenderFinishedSemaphores();
	// The scene's sky, baked into the environment if it is not the one there already, or if the scene
	// was replaced; every frame set then names the new environment.
	void UpdateEnvironment(
	    const entt::registry &sceneRegistry, bool sceneReplaced, const CameraView &camera,
	    const std::vector<SceneDrawCommand> &draws);
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
	// The celestial bodies drawn with an atmosphere or rings into the slot's body list, whether or
	// not they are in view (the air over a planet behind the camera can still be), and each draw's
	// entry in it, one more than its index, 0 for none.
	void WriteBodies(FrameData &frame, const std::vector<SceneDrawCommand> &draws);
	// The view's draws and each cascade's casters, culled and batched, the transparent draws and the
	// emitters in view sorted back to front, and every batch's instances into the slot's instance
	// buffer.
	void WriteInstances(
	    FrameData &frame, const std::vector<SceneDrawCommand> &draws,
	    const std::vector<uint32_t> &jointOffsets);
	// The scene's emitters, given their share of the particle pool, into the slot's emitter list, with
	// the seconds since the last frame.
	void WriteEmitters(
	    FrameData &frame, const entt::registry &sceneRegistry, const CameraView &camera,
	    double seconds);
	// The slot's last frame's assignment against the CPU's, now that its fence has been waited on.
	void CheckClusters(FrameData &frame);
	// The frame's debug lines into the slot's vertex buffer; how many are depth-tested.
	uint32_t WriteLines(FrameData &frame, const DebugDraw &debugDraw, const glm::dvec3 &camera);
	// The frame's constants, lights, bodies, emitters and instances; the seconds since the last frame
	// step the particles.
	void WriteFrame(
	    FrameData &frame, const entt::registry &sceneRegistry, const CameraView &camera,
	    const std::vector<SceneDrawCommand> &draws, std::vector<uint32_t> &jointOffsets,
	    double seconds);
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
	void RecordParticles(const FrameData &frame, VkCommandBuffer commandBuffer) const;
	// A batch of the view's, its material's pipeline and set bound when they differ from what is; an
	// opaque one with its stencil bits as the reference, which only the opaque pipelines take.
	void RecordMeshBatch(
	    VkCommandBuffer commandBuffer, const DrawBatch &batch, VkPipeline &boundPipeline,
	    const Model *&boundModel, bool opaque);
	// The transparent draws and the emitters, back to front, in the opaque pass's rendering.
	void RecordTransparent(VkCommandBuffer commandBuffer);
	void RecordScreenshot(FrameData &frame, VkCommandBuffer commandBuffer, uint32_t imageIndex) const;
	// The slot's screenshot from its readback into its file, now that its fence has been waited on.
	void SaveScreenshot(FrameData &frame);
	void RecordOpaque(const FrameData &frame, VkCommandBuffer commandBuffer);
	void RecordOutput(
	    const FrameData &frame, VkCommandBuffer commandBuffer, uint32_t imageIndex,
	    const ImGuiRenderer &imguiRenderer);
	// A full-screen post pass: one triangle into a single-sampled target, its images in its post set
	// and its values as push constants; tested against the resolved stencil, read-only, when one is
	// given; loading what is there when it blends over it.
	struct FullscreenPass
	{
		VkImageView target{VK_NULL_HANDLE};
		VkExtent2D extent{0, 0};
		VkPipeline pipeline{VK_NULL_HANDLE};
		PostSet set{kPostSsao};
		glm::vec4 first{0.0f};
		glm::vec4 second{0.0f};
		bool load{false};
		bool stencil{false};
	};
	void RecordFullscreen(
	    const FrameData &frame, VkCommandBuffer commandBuffer, const FullscreenPass &pass) const;
	void RecordAmbientOcclusion(const FrameData &frame, VkCommandBuffer commandBuffer, bool blur) const;
	void RecordBloom(
	    const FrameData &frame, VkCommandBuffer commandBuffer, bool down, std::uint32_t level) const;
	void RecordOutlineSeeds(const FrameData &frame, VkCommandBuffer commandBuffer) const;
	void RecordFlood(const FrameData &frame, VkCommandBuffer commandBuffer, std::uint32_t step) const;
	void RecordOutlines(const FrameData &frame, VkCommandBuffer commandBuffer) const;
	// The outlines of the draws the view keeps, each draw's index among them, and their list into the
	// slot's outline buffer.
	void WriteOutlines(FrameData &frame);
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
	std::unique_ptr<OverlayRenderer> m_pOverlay;
	// The sky the environment was baked from, or empty for the black one.
	std::optional<Scene::SkyComponent> m_bakedSky;
	// What lit the last bake besides the sky, and the frame it was made on.
	CaptureLight m_bakedLight;
	std::uint64_t m_bakedFrame{0};

	// Set 0 of the scene and cluster programs (constants, skin palette, lights, material
	// parameters, the environment, the cluster grid, the shadow map), set 1 of the scene programs
	// (a material's textures), and set 0 of the output program (the resolved HDR target).
	VkDescriptorSetLayout m_frameSetLayout{VK_NULL_HANDLE};
	VkDescriptorSetLayout m_materialSetLayout{VK_NULL_HANDLE};
	// Set 1 of the post programs: the images a post pass reads, by binding.
	VkDescriptorSetLayout m_postSetLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_meshLayout{VK_NULL_HANDLE};
	// The post programs and the output: the frame set, a post set, and two vec4s of push constants.
	VkPipelineLayout m_postLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_lineLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_clusterLayout{VK_NULL_HANDLE};
	// The particles' step: the frame set, and the emitter's index as a push constant.
	VkPipelineLayout m_particleLayout{VK_NULL_HANDLE};
	VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
	std::array<VkDescriptorSet, kPostSetCount> m_postSets{};
	// Texel for texel, and filtered (the bloom chain).
	VkSampler m_outputSampler{VK_NULL_HANDLE};
	VkSampler m_linearSampler{VK_NULL_HANDLE};
	VkPipeline m_outputPipeline{VK_NULL_HANDLE};
	VkPipeline m_ssaoPipeline{VK_NULL_HANDLE};
	VkPipeline m_ssaoBlurPipeline{VK_NULL_HANDLE};
	VkPipeline m_bloomDownPipeline{VK_NULL_HANDLE};
	VkPipeline m_bloomUpPipeline{VK_NULL_HANDLE};
	VkPipeline m_outlineSeedPipeline{VK_NULL_HANDLE};
	VkPipeline m_outlineFloodPipeline{VK_NULL_HANDLE};
	VkPipeline m_outlinesPipeline{VK_NULL_HANDLE};
	VkPipeline m_skyPipeline{VK_NULL_HANDLE};
	// The bodies' air and rings over the sky, after it (atmosphere.frag).
	VkPipeline m_atmospherePipeline{VK_NULL_HANDLE};
	VkPipeline m_linePipeline{VK_NULL_HANDLE};
	VkPipeline m_lineOverlayPipeline{VK_NULL_HANDLE};
	VkPipeline m_clusterBoundsPipeline{VK_NULL_HANDLE};
	VkPipeline m_clusterLightsPipeline{VK_NULL_HANDLE};
	VkPipeline m_shadowPipeline{VK_NULL_HANDLE};
	VkPipeline m_prepassPipeline{VK_NULL_HANDLE};
	VkPipeline m_particleStepPipeline{VK_NULL_HANDLE};
	VkPipeline m_particlePipeline{VK_NULL_HANDLE};

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
	// The transparent draws and the emitters in view, back to front.
	std::vector<SortedInput> m_sortedScratch;
	std::vector<SortedBatch> m_transparentBatches;
	std::vector<GpuInstance> m_instanceScratch;
	float m_lodThreshold{kDefaultLodThreshold};
	bool m_depthPrepass{false};
	bool m_ambientOcclusion{true};
	float m_bloom{kDefaultBloom};
	// What this frame runs: the prepass (asked for, or for ambient occlusion or an outline), ambient
	// occlusion, and the outlines and the glows among them.
	bool m_framePrepass{false};
	bool m_frameAmbientOcclusion{false};
	bool m_frameGlows{false};
	// The outlines of the draws the view keeps, by the entity that has each, and each draw's index
	// among them, or kMaxOutlines for none.
	std::vector<GpuOutline> m_outlineScratch;
	std::vector<entt::entity> m_outlineOwners;
	std::vector<std::uint32_t> m_drawOutlines;
	FrameStats m_frameStats{};

	// The particle pool, device-local and shared by the frames in flight, stepped in place each frame;
	// each emitter's share of it and its births; this frame's emitters, and the presentation time of
	// the last frame, which the particles are stepped from.
	Buffer m_particles{};
	ParticlePool m_particlePool{kParticleSlots};
	std::vector<EmitterInput> m_emitterInputs;
	std::vector<EmitterDraw> m_emitterDraws;
	std::vector<GpuEmitter> m_emitterScratch;
	std::optional<double> m_lastPresentationTime;
	// This frame's presentation time, which the lights' animations are evaluated at.
	double m_presentationTime{0.0};
	bool m_particlesRefused{false};

	// The cluster grid: each cluster's view-space bounds, its offset and count into the index list,
	// and the list, its allocator first. Device-local, shared by the frames in flight.
	Buffer m_clusterBounds{};
	Buffer m_clusterGrid{};
	Buffer m_lightIndices{};
	// What the bounds were built for, empty until they are; and this frame's grid.
	std::optional<ClusterView> m_clusterBoundsView;
	ClusterView m_frameClusterView{};
	std::vector<GpuLight> m_lightScratch;
	std::vector<GpuBody> m_bodyScratch;
	std::vector<std::uint32_t> m_drawBodies;
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
	PassResource m_stencilResource;
	PassResource m_normalsResource;
	PassResource m_objectIdsResource;
	PassResource m_resolvedDepthResource;
	PassResource m_resolvedStencilResource;
	PassResource m_accessResource;
	PassResource m_accessBlurredResource;
	std::array<PassResource, kBloomLevelCount> m_bloomResources{};
	std::array<PassResource, 2> m_floodResources{};
	PassResource m_hdrResource;
	PassResource m_swapchainResource;
	PassResource m_clusterBoundsResource;
	PassResource m_clusterGridResource;
	PassResource m_lightIndicesResource;
	PassResource m_particlesResource;
	uint32_t m_shadowPass{0};
	uint32_t m_prepassPass{0};
	uint32_t m_clusterResetPass{0};
	uint32_t m_clusterBoundsPass{0};
	uint32_t m_clusterLightsPass{0};
	uint32_t m_particlePass{0};
	uint32_t m_opaquePass{0};
	uint32_t m_ssaoPass{0};
	uint32_t m_ssaoBlurPass{0};
	std::array<uint32_t, kBloomLevelCount> m_bloomDownPasses{};
	// The way up, from the level second from the bottom: m_bloomUpPasses[level] adds level + 1 to it.
	std::array<uint32_t, kBloomLevelCount - 1> m_bloomUpPasses{};
	uint32_t m_outlineSeedPass{0};
	std::array<uint32_t, kFloodStepCount> m_floodPasses{};
	uint32_t m_outlinesPass{0};
	uint32_t m_outputPass{0};
	uint32_t m_clusterReadbackPass{0};
	// Absent from the plan on a swapchain that cannot be copied from.
	static constexpr uint32_t kNoPass = UINT32_MAX;
	uint32_t m_screenshotPass{kNoPass};
	// The file the next frame is to be written to, or empty.
	std::filesystem::path m_screenshotRequest;

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
