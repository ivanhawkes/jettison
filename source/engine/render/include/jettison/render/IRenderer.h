#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <jettison/render/Clusters.h>
#include <jettison/render/FrameStats.h>

struct GLFWwindow;

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
class DebugDraw;
class Overlay;

// The light index list's size, as entries per cluster on average: a budget for the frame, not a cap
// on any one cluster. 3.4 MiB at 32 x 18 x 48. Measured in 5e with 265 lights on a station, the
// list's busiest frame used 16 a cluster, from 20 m off, the closest the orbit camera comes; from
// 160 m, under one.
inline constexpr std::uint32_t kRendererLightIndicesPerCluster = 32;
inline constexpr std::uint32_t kRendererLightIndexCapacity =
    kClusterCount * kRendererLightIndicesPerCluster;

// What the lit programs show: the shaded scene, the lights-per-cluster heat map, or the sun's
// shadow cascades, each in its own colour (lighting.glsl's kDebugView*).
enum class DebugView : std::uint32_t
{
	Shaded = 0,
	ClusterHeat = 1,
	ShadowCascades = 2
};

// What a renderer is made with, before its device exists.
struct RendererOptions
{
	// Where the UI's fonts are read from: a host directory, not a virtual path, since ImGui opens
	// them itself.
	std::filesystem::path assetDirectory;
	// Fails Init on a device without the debug messenger, so a gated run cannot pass with the
	// validation layer's messages going nowhere. Only a debug build has the messenger.
	bool requireDebugMessenger{false};
	// Each frame's light assignment copied out and compared with the CPU's (CheckClusterAssignment),
	// once the frame's fence says it is done. It costs a copy of the grid and the index list a frame.
	bool clusterCheck{false};
	bool depthPrepass{false};
	// Part of the name of the GPU to draw with (--gpu), in place of the one the renderer would choose
	// (render/src/vulkan/DeviceChoice.h); Init fails, listing the GPUs, when no usable one has it.
	std::string gpu;
};

// The renderer as the rest of the engine sees it (docs/architecture.md, "Rendering"): the window it
// draws into, the frame of a scene, the UI drawn over it, and what the frame cost. No Vulkan type
// crosses it; the backend's objects are made and destroyed behind it (render/src/vulkan).
//
// Nothing behind it releases a GPU object in a destructor: Destroy does, in order, after the last
// frame's fence.
class IRenderer
{
  public:
	virtual ~IRenderer() = default;

	// Opens the window, makes the device, the swapchain, the frame and the UI, and registers the
	// model, texture and material loaders with the resource manager. A program missing from the
	// asset tree throws here, naming it.
	virtual void Init() = 0;
	// Waits for the device, then releases everything Init made, and every handle the renderer took
	// from the resource manager. Safe after an Init that threw part-way, and twice.
	virtual void Destroy() = 0;

	// The window the frames are drawn into; the shell reads its input and sizes it.
	virtual GLFWwindow *GetGLFWWindow() const = 0;

	// The UI's frame, between which ImGui's calls are made; Render draws what they made.
	virtual void BeginUi() = 0;
	virtual void EndUi() = 0;

	// Records and submits the scene as it stands: the presentation phase has already animated it
	// and computed its world transforms (Scene::SceneUpdateSystem), so this frame only reads it.
	// The overlay, the game UI's drawing (render/Overlay.h), goes over the tonemapped scene and under
	// the dev tools; its textures are uploaded as its draws first name them. The presentation time, in
	// seconds, drives the materials' time-driven parameters. True when this frame recreated the
	// swapchain.
	virtual bool Render(
	    const Scene::Scene &scene, const DebugDraw &debugDraw, const Overlay &overlay,
	    double presentationTime) = 0;

	// The linear scale applied to the HDR target on its way to the display, before tonemapping.
	virtual void SetExposure(float exposure) = 0;
	// A colour over the scene, by an amount from 0 (none) to 1 (the whole frame): a jump's effect
	// (scene/JumpTransition.h). Mixed in after tonemapping, in display light, and under the UI, so
	// the dev tools stay readable through it.
	virtual void SetFade(const glm::vec3 &color, float amount) = 0;
	virtual void SetDebugView(DebugView view) = 0;
	virtual DebugView GetDebugView() const = 0;
	// Whether the opaque surfaces' depth is laid down before they are shaded, so each pixel is
	// shaded once (docs/architecture.md, "The frame").
	virtual void SetDepthPrepass(bool enabled) = 0;
	virtual bool GetDepthPrepass() const = 0;

	// The last frame's counts, and the GPU's time in each declared pass.
	virtual const FrameStats &GetFrameStats() const = 0;
	virtual const std::vector<PassTiming> &GetPassTimings() const = 0;
	virtual std::uint64_t GetMeasuredFrames() const = 0;
	virtual void ResetPassTimings() = 0;
	// What the cluster check has found over the run (RendererOptions::clusterCheck).
	virtual const ClusterCheckTotals &GetClusterChecks() const = 0;

	// What the frames are drawn on, and their size in pixels.
	virtual const std::string &DeviceName() const = 0;
	virtual std::uint32_t FrameWidth() const = 0;
	virtual std::uint32_t FrameHeight() const = 0;
};

// The Vulkan renderer. The asset tree and the resource manager outlive it.
std::unique_ptr<IRenderer> CreateRenderer(
    const Vfs::FileSystem &assets, Resource::ResourceManager &resources, RendererOptions options);
} // namespace Jettison::Render
