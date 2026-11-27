#pragma once

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "vulkan/DeviceContext.h"
#include "vulkan/PipelineCache.h"
#include <jettison/render/Material.h>
#include <jettison/render/Texture.h>
#include <jettison/resource/ResourceHandle.h>

namespace Jettison::Resource
{
class ResourceManager;
}
namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Render
{
class Model;

// An element of the texture table no texture holds.
constexpr std::uint32_t kNoTexture = ~std::uint32_t{0};

// A texture as a resource: decoded on the I/O worker (TextureData), uploaded on the main thread by
// the loader's finish, which frees the CPU copy and gives it its element of the texture table. A
// reload or the last release hands the image and the element to the material cache's deferred
// release, because a frame in flight may still sample it.
struct TextureResource
{
	TextureData data;
	Image image{};
	VkImageView view{VK_NULL_HANDLE};
	std::uint32_t texture{kNoTexture};
};

// What a draw binds for its material.
struct MaterialBinding
{
	VkPipeline pipeline{VK_NULL_HANDLE};

	// Where its block starts in the frame's material buffer: its slots' indices into the texture
	// table, then its parameters (assets/shaders/include/material.glsl).
	std::uint32_t parameterOffset{0};

	// Its program's blend is ProgramBlend::Transparent: drawn in the transparent pass, sorted back to
	// front, and casting no shadow. A model's own material never is.
	bool transparent{false};
};

// The materials on the device (docs/rendering.md): what a draw binds for its material, which is a
// pipeline for the material's program and an offset into the frame's material buffer, where the
// material's block holds its kMaterialTextureSlots indices into the texture table and its
// parameters.
//
// The texture table is one descriptor set (set 1), bound with the frame's: an array of
// DeviceContext::kTextureTableSize sampled images, partially bound and updated after bind, and the
// sampler. A texture takes an element when it finishes uploading, and gives it back once no frame in
// flight can read it, so an element is written only while no submission reads it. The fallbacks
// hold the first elements, by TextureFallback.
//
// Two kinds of material. A named one is a material file, loaded through the resource manager with
// its textures, and rebuilt when either reloads; it is keyed on its path, so a scene reload does
// not make it stale, and the renderer releases the ones the new scene no longer draws. A model's
// own is what a glTF primitive or a procedural shape without a material draws with: the default
// "mesh" program at its defaults, with the primitive's embedded base-colour image, keyed on the
// Model's address. The model cache calls Forget as a model goes, before the next allocation can be
// given the address (render/vulkan/ModelCache.h).
//
// Nothing a frame in flight may read is destroyed on the spot. The image of a texture a reload
// replaced, and its element of the table, wait in a queue for kFramesInFlight frames.
class MaterialCache
{
  public:
	using Binding = MaterialBinding;

	MaterialCache(
	    std::shared_ptr<DeviceContext> pDeviceContext, const Vfs::FileSystem &assets,
	    Resource::ResourceManager &resources, PipelineCache &pipelines, std::uint32_t framesInFlight);

	MaterialCache(const MaterialCache &) = delete;

	MaterialCache &operator=(const MaterialCache &) = delete;

	// The layouts and the render states are the renderer's: the opaque pass's for an opaque program,
	// the transparent draws' for a transparent one (ProgramBlend). Makes the texture table in the
	// layout given. Registers the texture and material loaders, so a renderer is made once per
	// resource manager. Reads the default program, which a missing asset tree fails here.
	void Init(
	    VkDescriptorSetLayout textureLayout, VkPipelineLayout sceneLayout,
	    const RenderState &sceneState, const RenderState &transparentState);

	VkDescriptorSet GetTextureSet() const
	{
		return m_textureSet;
	}

	// Releases every handle and frees everything, queued or not: the device has to be idle. The
	// resource manager outlives this, and its loaders must not run after it.
	void Destroy();

	// The frame's start, after its slot's fence: what was retired kFramesInFlight frames before is
	// no longer read by any submission, and is freed. Starts the frame's material blocks with the
	// default program's, at offset 0.
	void BeginFrame(std::uint64_t frameNumber, std::vector<glm::vec4> &parameters);

	// A named material for this frame, its block appended once a frame, its parameters evaluated at
	// the presentation time. Null while it or one of its textures is loading for the first time,
	// and after it failed (logged once), when the draw falls back to the model's own.
	const Binding *Resolve(
	    const std::string &virtualPath, double time, std::vector<glm::vec4> &parameters);

	// A model's own materials, one a primitive of its levels, made on first use, and their blocks
	// appended once a frame: for a draw with no named material, before its primitives are recorded.
	void EnsureModel(const Model &model, std::vector<glm::vec4> &parameters);

	// A model's own material for one primitive, which EnsureModel made and placed this frame.
	Binding Ensure(const Model &model, std::size_t primitiveIndex) const;

	// Retires a model's own materials, which wait out the frames in flight: the model is going, and
	// its address may be the next one's.
	void Forget(const Model &model);

	// Releases every named material this frame did not resolve: after a scene reload, the ones the
	// new scene does not draw. What they bound waits out the frames in flight.
	void ReleaseUnresolved();

  private:
	using TextureSlots = std::array<std::uint32_t, kMaterialTextureSlots>;

	struct ModelMaterial
	{
		Image image{};
		VkImageView view{VK_NULL_HANDLE};
		std::uint32_t texture{kNoTexture};
		std::uint32_t parameterOffset{0};
	};

	struct ModelMaterials
	{
		std::vector<ModelMaterial> primitives;

		// The last frame that placed their blocks.
		std::uint64_t parameterFrame{~std::uint64_t{0}};
	};

	struct NamedMaterial
	{
		Resource::ResourceHandle<MaterialDefinition> definition;

		// What the resolution was built from; a revision that moves rebuilds it.
		std::uint32_t definitionRevision{0};
		std::array<Resource::ResourceHandle<TextureResource>, kMaterialTextureSlots> textures{};
		ResolvedMaterial resolved;
		VkPipeline pipeline{VK_NULL_HANDLE};
		bool transparent{false};

		// Each slot's element of the texture table, as its texture is now: a reload's new one, or the
		// fallback for a slot that is empty or whose texture failed.
		TextureSlots slots{};

		// Set when the definition at definitionRevision cannot be drawn; a reload clears it.
		bool failed{false};

		// The last frame that resolved it, and the last that evaluated its parameters.
		std::uint64_t resolvedFrame{0};
		std::uint64_t parameterFrame{~std::uint64_t{0}};
		Binding binding;
	};

	// What waits for the frames in flight to be done with it.
	struct Retired
	{
		std::uint64_t frame{0};
		Image image{};
		VkImageView view{VK_NULL_HANDLE};
		std::uint32_t texture{kNoTexture};
	};

	struct Program
	{
		ProgramDefinition definition;
		ResolvedMaterial defaults;
		VkPipeline pipeline{VK_NULL_HANDLE};
	};

	void RegisterLoaders();

	// The program's descriptor and pipeline, read and built once; throws for a program that is not
	// in the asset tree.
	const Program &GetProgram(const std::string &name);

	// Resolves a named material again if its definition moved, and takes its textures' elements as
	// they are now. False while it cannot be drawn yet, or at all.
	bool Refresh(const std::string &virtualPath, NamedMaterial &material);

	void ReleaseNamed(NamedMaterial &material);

	// Uploads the texture and gives it an element of the table, which this writes. Throws, having
	// freed what it made, when either fails: the table is full when every element is held.
	std::uint32_t Upload(
	    const TextureData &data, const std::string &name, Image &image, VkImageView &view);

	// A material's block: its slots' elements, then its parameters at a presentation time.
	static void AppendBlock(
	    const TextureSlots &slots, const ResolvedMaterial &material, double time,
	    std::vector<glm::vec4> &parameters);

	static TextureSlots FallbackSlots(const ResolvedMaterial &material);

	void Retire(Retired retired);

	void Free(Retired &retired);

	// Frees every model's own material at once; the device has to be idle.
	void ForgetAll();

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	const Vfs::FileSystem &m_assets;
	Resource::ResourceManager &m_resources;
	PipelineCache &m_pipelines;
	const std::uint32_t m_framesInFlight;
	TranscodeTarget m_transcodeTarget{TranscodeTarget::Rgba8};

	VkDescriptorPool m_texturePool{VK_NULL_HANDLE};
	VkDescriptorSet m_textureSet{VK_NULL_HANDLE};

	// The elements given back, reused before the table grows; and the first never yet given.
	std::vector<std::uint32_t> m_freeTextures;
	std::uint32_t m_nextTexture{0};
	VkPipelineLayout m_sceneLayout{VK_NULL_HANDLE};
	RenderState m_sceneState;
	RenderState m_transparentState;
	VkSampler m_sampler{VK_NULL_HANDLE};

	// The empty-slot textures, by TextureFallback, which is also their element of the table.
	std::array<Image, 3> m_fallbackImages{};
	std::array<VkImageView, 3> m_fallbackViews{};

	std::map<std::string, Program> m_programs;
	const Program *m_pDefaultProgram{nullptr};
	std::unordered_map<std::string, NamedMaterial> m_named;
	std::unordered_map<const Model *, ModelMaterials> m_models;
	std::vector<Retired> m_retired;
	std::uint64_t m_frame{0};
	bool m_loadersRegistered{false};
};
} // namespace Jettison::Render
