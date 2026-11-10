#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

#include <jettison/render/vulkan/DeviceContext.h>
#include <jettison/render/vulkan/FrameData.h>

namespace Jettison::Render
{
class PipelineCache;
struct SkyComponent;

// The sky as sky.glsl takes it. The stars sit on a shell of an integer lattice half a cell thick,
// which holds 2 pi r^2 of them at radius r, so r is sqrt(count / 2 pi); a star is drawn no wider
// than 0.06 / r radians, which keeps every star a direction can see in the eight cells around it.
GpuSky PackSky(const SkyComponent &sky);

// The image-based lighting of a star system (docs/architecture.md, "The frame"): the sky captured
// into a cube map and prefiltered for the metallic-roughness BRDF, and the split sum's lookup
// table, which depends on the BRDF alone and is made once.
//
// A bake renders the sky's six faces with sky_capture, builds the capture's mip chain, then renders
// prefilter_specular into every level of the specular cube (roughness 0 to 1, one level each) and
// prefilter_irradiance into the irradiance cube. It is a one-off on the graphics queue, as a texture
// upload is, not a node of the frame's pass list, and like an upload it waits for the queue: when
// Bake returns, no submission reads the cubes it replaced, and they are freed on the spot. The
// caller rewrites whatever descriptor sets named them, which no submission reads either.
class Environment
{
  public:
	static constexpr VkFormat kCubeFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	static constexpr VkFormat kBrdfLutFormat = VK_FORMAT_R16G16_SFLOAT;
	// Kept small for lavapipe, where the gate bakes twice a scene, though the bake is not yet timed:
	// the sky's glow is smooth at these sizes, and the stars, which are not, are averaged away by
	// every level above the first.
	static constexpr std::uint32_t kCaptureSize = 128;
	static constexpr std::uint32_t kSpecularSize = 128;
	static constexpr std::uint32_t kSpecularLevels = 6;
	static constexpr std::uint32_t kIrradianceSize = 32;
	static constexpr std::uint32_t kBrdfLutSize = 128;

	Environment(std::shared_ptr<DeviceContext> pDeviceContext, PipelineCache &pipelines);

	Environment(const Environment &) = delete;
	Environment &operator=(const Environment &) = delete;

	// The sampler, the bake's layouts and pipelines, the lookup table, and a black environment, so
	// there is always one to bind. A bake program missing from the asset tree throws here.
	void Init();
	// The device has to be idle. Safe after an Init that threw part-way, and twice.
	void Destroy();

	// Replaces the environment with the sky's, or with black for none.
	void Bake(const SkyComponent *pSky);

	VkSampler GetSampler() const
	{
		return m_sampler;
	}
	VkImageView GetSpecularView() const
	{
		return m_specular.view;
	}
	VkImageView GetIrradianceView() const
	{
		return m_irradiance.view;
	}
	VkImageView GetBrdfLutView() const
	{
		return m_brdfLutView;
	}
	// The specular cube's last level, which roughness 1 samples.
	float GetMaxSpecularLevel() const
	{
		return static_cast<float>(kSpecularLevels - 1);
	}

  private:
	struct Cube
	{
		Image image{};
		VkImageView view{VK_NULL_HANDLE};
	};

	void CreateLayouts();
	void BakeBrdfLut();
	Cube CreateCube(
	    std::uint32_t size, std::uint32_t levels, VkImageUsageFlags usage, const char *name);
	void DestroyCube(Cube &cube);
	// One fullscreen draw into a square target, with the program's set and push constants.
	void RenderFace(
	    VkCommandBuffer commandBuffer, VkImageView target, std::uint32_t size, VkPipeline pipeline,
	    VkPipelineLayout layout, VkDescriptorSet set, const void *pushConstants,
	    std::uint32_t pushSize) const;

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	PipelineCache &m_pipelines;

	VkSampler m_sampler{VK_NULL_HANDLE};
	VkDescriptorSetLayout m_captureSetLayout{VK_NULL_HANDLE};
	VkDescriptorSetLayout m_prefilterSetLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_captureLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_prefilterLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_brdfLutLayout{VK_NULL_HANDLE};
	VkPipeline m_capturePipeline{VK_NULL_HANDLE};
	VkPipeline m_specularPipeline{VK_NULL_HANDLE};
	VkPipeline m_irradiancePipeline{VK_NULL_HANDLE};
	VkPipeline m_brdfLutPipeline{VK_NULL_HANDLE};

	Image m_brdfLut{};
	VkImageView m_brdfLutView{VK_NULL_HANDLE};
	Cube m_specular;
	Cube m_irradiance;
};
} // namespace Jettison::Render
