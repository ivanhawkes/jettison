#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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
// on any one cluster. 3.4 MiB at 32 x 18 x 48. Measured in 5e with 265 lights on a platform, the
// list's busiest frame used 16 a cluster, from 20 m off, the closest the orbit camera comes; from
// 160 m, under one.
inline constexpr std::uint32_t kRendererLightIndicesPerCluster = 32;
inline constexpr std::uint32_t kRendererLightIndexCapacity =
    kClusterCount * kRendererLightIndicesPerCluster;

// What the lit programs show: the shaded scene, the lights-per-cluster heat map, the sun's shadow
// cascades, each in its own colour, or the share of the ambient light ambient occlusion leaves each
// opaque surface, as grey (lighting.glsl's kDebugView*).
enum class DebugView : std::uint32_t
{
	Shaded = 0,
	ClusterHeat = 1,
	ShadowCascades = 2,
	AmbientAccess = 3
};

// What a renderer is made with, before its device exists.
struct RendererOptions
{
	// Fails Init on a device without the debug messenger, so a gated run cannot pass with the
	// validation layer's messages going nowhere. Only a debug build has the messenger.
	bool requireDebugMessenger{false};

	// Each frame's light assignment copied out and compared with the CPU's (CheckClusterAssignment),
	// once the frame's fence says it is done. It costs a copy of the grid and the index list a frame.
	bool clusterCheck{false};
	bool depthPrepass{false};

	// Ambient occlusion, and bloom's strength (0 for none), until the dev tools change them. Without a
	// word either way, ambient occlusion is on, except on a CPU device: it runs the depth prepass,
	// which in the dressed orbit scene costs lavapipe 11 ms of a 50 ms budget (docs/history/phases.md,
	// 10c).
	std::optional<bool> ambientOcclusion;
	float bloom{0.04f};

	// Part of the name of the GPU to draw with (--gpu), in place of the one the renderer would choose
	// (render/src/vulkan/DeviceChoice.h); Init fails, listing the GPUs, when no usable one has it.
	std::string gpu;

	// The optional paths the player allows (--no-meshlets, --no-ray-tracing, --no-mega-geometry, and
	// the settings render.meshlets, render.rayTracing and render.megaGeometry): false turns one off on
	// a device that offers it, and the log says so. A path the device lacks is off whatever these say.
	bool meshlets{true};
	bool rayTracing{true};
	bool megaGeometry{true};

	// A frame whose acquire finds the swapchain out of date is recreated until it draws, rather than
	// retried once and skipped: for a fixed-step run, whose frames must not follow the display
	// server's timing. Not for a window a user may be resizing, where it could keep recreating.
	bool drawEveryFrame{false};

	// Drawn into images of the renderer's own, with no surface, no swapchain and no display (GLFW's
	// null platform): a run on a GPU that cannot present where it runs, as no NVIDIA card can under
	// Xvfb. The screenshot reads the image as it would the swapchain's.
	bool offscreen{false};
};

// The renderer as the rest of the engine sees it (docs/rendering.md): the window it draws into, the
// frame of a scene, the UI drawn over it, and what the frame cost. No Vulkan type crosses it; the
// backend's objects are made and destroyed behind it (render/src/vulkan).
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

	// Whether the attachment the UI is drawn into encodes the colour it is given, which the dev
	// tools' theme decodes its palette for so that a palette hex reads as it is written on an sRGB
	// swapchain, and as authored on the UNORM one a compositor can ask for (docs/ui.md, "The dev tools'
	// theme", devtools/Theme.h). True before Init, when there is no attachment yet.
	virtual bool UiColoursLinearised() const = 0;

	// Records and submits the scene as it stands: the presentation phase has already animated it
	// and computed its world transforms (Scene::SceneUpdateSystem), so this frame only reads it.
	// The overlay, the game UI's drawing (render/Overlay.h), goes over the tonemapped scene and under
	// the dev tools; its textures are uploaded as its draws first name them. The presentation time, in
	// seconds, drives the materials' time-driven parameters. True when this frame recreated the
	// swapchain.
	virtual bool Render(
	    const Scene::Scene &scene, const DebugDraw &debugDraw, const Overlay &overlay,
	    double presentationTime) = 0;

	// Asks for every model file the scene's meshes name, so each loads before the scene draws it; true
	// once each has loaded or failed. The shell calls it each frame after a scene loads, and holds the
	// scene behind a full fade until it is true (App::Shell::SceneReady).
	virtual bool PrefetchScene(const Scene::Scene &scene) = 0;

	// The linear scale applied to the HDR target on its way to the display, before tonemapping. The
	// scene's colour grade (Scene::ColourGradeComponent) is applied after it, to the display's colours.
	virtual void SetExposure(float exposure) = 0;

	// A colour over the scene, by an amount from 0 (none) to 1 (the whole frame): a game's fade between
	// two scenes. Mixed in after tonemapping, in display light, and under the UI, so
	// the dev tools stay readable through it.
	virtual void SetFade(const glm::vec3 &color, float amount) = 0;

	// Black bars over the top and the bottom of the frame, each this share of its height, from 0 to a
	// half: a cinematic's (scene/Sequence.h). Drawn with the fade, under the UI.
	virtual void SetLetterbox(float share) = 0;

	virtual void SetDebugView(DebugView view) = 0;

	virtual DebugView GetDebugView() const = 0;

	// Whether the opaque surfaces' depth is laid down before they are shaded, so each pixel is
	// shaded once (docs/rendering.md, "The frame").
	virtual void SetDepthPrepass(bool enabled) = 0;

	virtual bool GetDepthPrepass() const = 0;

	// Ambient occlusion over the opaque surfaces' image-based light, which runs the depth prepass
	// while it is on (docs/rendering-effects.md, "Ambient occlusion").
	virtual void SetAmbientOcclusion(bool enabled) = 0;

	virtual bool GetAmbientOcclusion() const = 0;

	// The share of the frame's light bloom spreads, 0 for none (docs/rendering-effects.md, "Bloom").
	virtual void SetBloom(float strength) = 0;

	virtual float GetBloom() const = 0;

	// The last frame's counts, and the GPU's time in each declared pass.
	virtual const FrameStats &GetFrameStats() const = 0;

	virtual const std::vector<PassTiming> &GetPassTimings() const = 0;

	virtual std::uint64_t GetMeasuredFrames() const = 0;

	virtual void ResetPassTimings() = 0;

	// What the cluster check has found over the run (RendererOptions::clusterCheck).
	virtual const ClusterCheckTotals &GetClusterChecks() const = 0;

	// The next frame, as the output pass leaves it with the UI over it, written to a PNG once the GPU
	// has drawn it (render/Screenshot.h); a run that ends on the frame after asking still writes it.
	// Logged and dropped where the swapchain cannot be read back.
	virtual void RequestScreenshot(const std::filesystem::path &path) = 0;

	// Whether a request is still waiting for a frame to draw it: a frame whose acquire found the
	// swapchain out of date twice draws nothing, and the request stays for the next.
	virtual bool ScreenshotRequested() const = 0;

	// What the frames are drawn on, and their size in pixels.
	virtual const std::string &DeviceName() const = 0;

	virtual std::uint32_t FrameWidth() const = 0;

	virtual std::uint32_t FrameHeight() const = 0;
};

// The Vulkan renderer. The asset tree and the resource manager outlive it.
std::unique_ptr<IRenderer> CreateRenderer(
    const Vfs::FileSystem &assets, Resource::ResourceManager &resources, RendererOptions options);
} // namespace Jettison::Render
