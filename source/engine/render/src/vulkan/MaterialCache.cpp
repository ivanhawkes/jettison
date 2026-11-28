#include "vulkan/MaterialCache.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <exception>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "vulkan/Model.h"
#include "vulkan/TextureUpload.h"
#include <jettison/core/Log.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/vfs/FileSystem.h>

namespace Jettison::Render
{
namespace
{
constexpr Core::Category kRender{"render"};

// The program every draw without a material of its own uses.
constexpr const char *kDefaultProgram = "mesh";

// A material block's slot indices go four to a vec4 (assets/shaders/include/material.glsl).
constexpr std::size_t kSlotsPerVector = 4;
static_assert(kMaterialTextureSlots % kSlotsPerVector == 0);


// The address of a Model is the key its materials live under, so it is the identifier a reader of
// a validation message has to go look up. Names built from it point at the same model from the
// set, the texture and the vertex buffer.
std::string ModelKey(const Model *pModel)
{
	return "model-" + std::to_string(reinterpret_cast<uintptr_t>(pModel));
}


std::string_view AsText(std::span<const std::byte> bytes)
{
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
} // namespace


MaterialCache::MaterialCache(
    std::shared_ptr<DeviceContext> pDeviceContext, const Vfs::FileSystem &assets,
    Resource::ResourceManager &resources, PipelineCache &pipelines, std::uint32_t framesInFlight)
    : m_pDeviceContext{std::move(pDeviceContext)}, m_assets{assets}, m_resources{resources},
      m_pipelines{pipelines}, m_framesInFlight{framesInFlight}
{
}


void MaterialCache::Init(
    VkDescriptorSetLayout textureLayout, VkPipelineLayout sceneLayout, const RenderState &sceneState,
    const RenderState &transparentState)
{
	m_sceneLayout = sceneLayout;
	m_sceneState = sceneState;
	m_transparentState = transparentState;
	m_transcodeTarget =
	    m_pDeviceContext->HasBlockCompression() ? TranscodeTarget::Bc7 : TranscodeTarget::Rgba8;
	Core::Log(
	    Core::Severity::Info, kRender, "Basis Universal textures transcode to {}",
	    m_transcodeTarget == TranscodeTarget::Bc7 ? "BC7"
	                                              : "RGBA8 (the device has no BC7 in hardware)");

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	// A CPU takes each anisotropic tap in software, and the triplanar program fetches nine texels a
	// pixel: on lavapipe at 640 x 480, the vehicle plating on the display wing's round shapes, seen at
	// grazing angles from 1.5 km, took the orbit scene's opaque pass from 22 ms to 59 ms at 16x and
	// back to 22 ms without it (9d). A hardware device filters at 16x.
	samplerInfo.anisotropyEnable = m_pDeviceContext->IsCpu() ? VK_FALSE : VK_TRUE;
	samplerInfo.maxAnisotropy = m_pDeviceContext->IsCpu() ? 1.0f : 16.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	if (vkCreateSampler(m_pDeviceContext->GetLogicalDevice(), &samplerInfo, nullptr, &m_sampler) !=
	    VK_SUCCESS)
		throw std::runtime_error("failed to create texture sampler");
	m_pDeviceContext->SetDebugName(m_sampler, "material-sampler");

	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	const std::array<VkDescriptorPoolSize, 2> poolSizes{{
	    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, DeviceContext::kTextureTableSize},
	    {VK_DESCRIPTOR_TYPE_SAMPLER, 1},
	}};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_texturePool) != VK_SUCCESS)
		throw std::runtime_error("failed to create the texture table's descriptor pool");
	m_pDeviceContext->SetDebugName(m_texturePool, "texture-table-pool");
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_texturePool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &textureLayout;
	if (vkAllocateDescriptorSets(device, &allocInfo, &m_textureSet) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate the texture table");
	m_pDeviceContext->SetDebugName(m_textureSet, "texture-table");
	const VkDescriptorImageInfo samplerInfoWrite{m_sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_textureSet;
	write.dstBinding = 1;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	write.pImageInfo = &samplerInfoWrite;
	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

	// By TextureFallback, which is their order in the empty table and so their elements: white,
	// black, and a flat tangent-space normal (0, 0, 1), stored as unsigned bytes and so linear.
	const std::array<TextureData, 3> fallbacks{
	    SolidTexture(255, 255, 255, 255, true), SolidTexture(0, 0, 0, 255, true),
	    SolidTexture(128, 128, 255, 255, false)};
	const std::array<const char *, 3> names{"fallback-white", "fallback-black", "fallback-normal"};
	for (std::size_t index = 0; index < fallbacks.size(); ++index)
		(void)Upload(fallbacks[index], names[index], m_fallbackImages[index], m_fallbackViews[index]);

	RegisterLoaders();
	m_pDefaultProgram = &GetProgram(kDefaultProgram);
}


void MaterialCache::RegisterLoaders()
{
	if (m_loadersRegistered)
		return;
	const TranscodeTarget target = m_transcodeTarget;
	m_resources.RegisterLoader(
	    Resource::Loader<TextureResource>{
	        [target](std::span<const std::byte> bytes, std::string_view path)
	        {
		        auto pTexture = std::make_unique<TextureResource>();
		        pTexture->data = DecodeTexture(bytes, path, target);
		        return pTexture;
	        },
	        [this](TextureResource &texture, std::string_view path)
	        {
		        texture.texture =
		            Upload(texture.data, "texture-" + std::string{path}, texture.image, texture.view);

		        // The device has its copy; the CPU one is a texture's worth of memory for nothing.
		        std::vector<std::byte>{}.swap(texture.data.bytes);
	        },
	        [this](TextureResource &texture)
	        {
		        Retire({m_frame, texture.image, texture.view, texture.texture});
		        texture.image = {};
		        texture.view = VK_NULL_HANDLE;
		        texture.texture = kNoTexture;
	        }});
	m_resources.RegisterLoader(
	    Resource::Loader<MaterialDefinition>{
	        [](std::span<const std::byte> bytes, std::string_view path)
	        { return std::make_unique<MaterialDefinition>(ReadMaterial(AsText(bytes), path)); },
	        {},
	        {}});
	m_loadersRegistered = true;
}


void MaterialCache::Destroy()
{
	ForgetAll();
	for (auto &[path, material] : m_named)
	{
		(void)path;
		ReleaseNamed(material);
	}
	m_named.clear();
	for (Retired &retired : m_retired)
		Free(retired);
	m_retired.clear();
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	for (std::size_t index = 0; index < m_fallbackImages.size(); ++index)
	{
		vkDestroyImageView(device, m_fallbackViews[index], nullptr);
		m_pDeviceContext->DestroyImage(m_fallbackImages[index]);
		m_fallbackViews[index] = VK_NULL_HANDLE;
	}
	vkDestroySampler(device, m_sampler, nullptr);
	m_sampler = VK_NULL_HANDLE;
	vkDestroyDescriptorPool(device, m_texturePool, nullptr);
	m_texturePool = VK_NULL_HANDLE;
	m_textureSet = VK_NULL_HANDLE;
	m_freeTextures.clear();
	m_nextTexture = 0;

	// The pipelines are the pipeline cache's, which destroys them.
	m_programs.clear();
	m_pDefaultProgram = nullptr;
}


void MaterialCache::Forget(const Model &model)
{
	const auto found = m_models.find(&model);
	if (found == m_models.end())
		return;
	for (const ModelMaterial &material : found->second.primitives)
		Retire({m_frame, material.image, material.view, material.texture});
	m_models.erase(found);
}


void MaterialCache::ForgetAll()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	for (auto &[model, materials] : m_models)
	{
		(void)model;
		for (ModelMaterial &material : materials.primitives)
		{
			vkDestroyImageView(device, material.view, nullptr);
			m_pDeviceContext->DestroyImage(material.image);
		}
	}
	m_models.clear();
}


void MaterialCache::ReleaseUnresolved()
{
	for (auto entry = m_named.begin(); entry != m_named.end();)
	{
		if (entry->second.resolvedFrame == m_frame)
		{
			++entry;
			continue;
		}
		ReleaseNamed(entry->second);
		entry = m_named.erase(entry);
	}
}


void MaterialCache::BeginFrame(std::uint64_t frameNumber, std::vector<glm::vec4> &parameters)
{
	m_frame = frameNumber;

	// Frame N's slot fence has been waited on, so frames up to N - kFramesInFlight are done; an
	// item retired at frame R was last recorded by frame R at the latest.
	const auto done = std::stable_partition(
	    m_retired.begin(), m_retired.end(),
	    [&](const Retired &retired) { return retired.frame + m_framesInFlight > frameNumber; });
	for (auto item = done; item != m_retired.end(); ++item)
		Free(*item);
	m_retired.erase(done, m_retired.end());

	parameters.clear();
	if (m_pDefaultProgram != nullptr)
		AppendBlock(
		    FallbackSlots(m_pDefaultProgram->defaults), m_pDefaultProgram->defaults, 0.0, parameters);
}


const MaterialCache::Program &MaterialCache::GetProgram(const std::string &name)
{
	if (const auto found = m_programs.find(name); found != m_programs.end())
		return found->second;
	const std::string path = "shaders/" + name + ".program.yaml";
	const auto bytes = m_assets.Read(path);
	if (!bytes)
		throw std::runtime_error("failed to open file: " + path + " (no asset mount has it)");
	Program program;
	program.definition = ReadProgram(AsText(*bytes), path);
	program.defaults = ResolveDefaults(program.definition);
	const bool transparent = program.definition.blend == ProgramBlend::Transparent;
	program.pipeline =
	    m_pipelines.Get({name, transparent ? m_transparentState : m_sceneState, m_sceneLayout});
	return m_programs.emplace(name, std::move(program)).first->second;
}


const MaterialCache::Binding *MaterialCache::Resolve(
    const std::string &virtualPath, double time, std::vector<glm::vec4> &parameters)
{
	auto [entry, inserted] = m_named.try_emplace(virtualPath);
	NamedMaterial &material = entry->second;
	if (inserted)
		material.definition = m_resources.Load<MaterialDefinition>(virtualPath);
	material.resolvedFrame = m_frame;
	if (!Refresh(virtualPath, material))
		return nullptr;
	if (material.parameterFrame != m_frame)
	{
		material.binding.parameterOffset = static_cast<std::uint32_t>(parameters.size());
		AppendBlock(material.slots, material.resolved, time, parameters);
		material.parameterFrame = m_frame;
	}
	return &material.binding;
}


bool MaterialCache::Refresh(const std::string &virtualPath, NamedMaterial &material)
{
	// A reload in flight keeps the last value Ready, so Loading and Failed are a first load's.
	if (m_resources.StatusOf(material.definition) != Resource::Status::Ready)
		return false;

	const std::uint32_t revision = m_resources.RevisionOf(material.definition);
	if (revision != material.definitionRevision)
	{
		material.definitionRevision = revision;
		material.failed = false;
		try
		{
			const MaterialDefinition &definition = *m_resources.TryGet(material.definition);
			const Program &program = GetProgram(definition.program);
			ResolvedMaterial resolved = ResolveMaterial(program.definition, definition, virtualPath);

			// Every path is checked before any is loaded, so a bad one cannot strand the handles
			// taken before it.
			for (const std::string &path : resolved.textures)
				if (!path.empty() && !Vfs::FileSystem::IsValidPath(path))
					throw std::runtime_error("texture '" + path + "' is not a virtual path");

			// The new textures are loaded before the old ones are released, so one both name keeps
			// its slot rather than being freed and read again.
			std::array<Resource::ResourceHandle<TextureResource>, kMaterialTextureSlots> textures{};
			for (std::size_t binding = 0; binding < kMaterialTextureSlots; ++binding)
				if (!resolved.textures[binding].empty())
					textures[binding] = m_resources.Load<TextureResource>(resolved.textures[binding]);
			for (const auto &texture : material.textures)
				if (!texture.IsNull())
					m_resources.Release(texture);
			material.textures = textures;
			material.resolved = std::move(resolved);
			material.pipeline = program.pipeline;
			material.transparent = program.definition.blend == ProgramBlend::Transparent;
		}
		catch (const std::exception &error)
		{
			material.failed = true;
			Core::Log(
			    Core::Severity::Warning, kRender, "material '{}' cannot be drawn, and falls back: {}",
			    virtualPath, error.what());
		}
	}
	if (material.failed)
		return false;

	// A reload's texture replaces its old one's element in the slot, which the old one keeps until no
	// frame in flight reads it.
	TextureSlots slots = FallbackSlots(material.resolved);
	for (std::size_t slot = 0; slot < kMaterialTextureSlots; ++slot)
	{
		const auto &texture = material.textures[slot];
		if (texture.IsNull())
			continue;
		const Resource::Status status = m_resources.StatusOf(texture);
		if (status == Resource::Status::Loading)
			return false;

		// A texture that failed (the resource manager said why) draws as its slot's fallback until a
		// reload fixes it.
		if (status == Resource::Status::Ready)
			slots[slot] = m_resources.TryGet(texture)->texture;
	}
	material.slots = slots;
	material.binding.pipeline = material.pipeline;
	material.binding.transparent = material.transparent;
	return true;
}


void MaterialCache::ReleaseNamed(NamedMaterial &material)
{
	for (const auto &texture : material.textures)
		if (!texture.IsNull())
			m_resources.Release(texture);
	if (!material.definition.IsNull())
		m_resources.Release(material.definition);
	material = {};
}


void MaterialCache::EnsureModel(const Model &model, std::vector<glm::vec4> &parameters)
{
	ModelMaterials &materials = m_models[&model];
	if (materials.parameterFrame == m_frame)
		return;
	std::size_t count = 0;
	for (const ModelLod &lod : model.m_lods)
		count = std::max<std::size_t>(count, lod.primitiveCount);
	if (count > model.m_primitives.size())
		throw std::runtime_error("Model primitive index is out of range");
	while (materials.primitives.size() < count)
	{
		const std::size_t primitive = materials.primitives.size();
		const std::string name = ModelKey(&model) + "-p" + std::to_string(primitive);
		ModelMaterial material;
		const ModelPrimitive &source = model.m_primitives[primitive];
		if (!source.baseColorImageData.empty())
		{
			const TextureData texture = source.DecodeBaseColor(m_transcodeTarget, name);
			material.texture = Upload(texture, "texture-" + name, material.image, material.view);
		}
		materials.primitives.push_back(material);
	}

	const ResolvedMaterial &defaults = m_pDefaultProgram->defaults;
	for (ModelMaterial &material : materials.primitives)
	{
		TextureSlots slots = FallbackSlots(defaults);
		if (material.texture != kNoTexture)
			slots[0] = material.texture;
		material.parameterOffset = static_cast<std::uint32_t>(parameters.size());
		AppendBlock(slots, defaults, 0.0, parameters);
	}
	materials.parameterFrame = m_frame;
}


MaterialCache::Binding MaterialCache::Ensure(const Model &model, std::size_t primitiveIndex) const
{
	const auto found = m_models.find(&model);
	if (found == m_models.end() || found->second.parameterFrame != m_frame ||
	    primitiveIndex >= found->second.primitives.size())
		throw std::runtime_error("A model's own material was drawn before it was placed this frame");
	return {m_pDefaultProgram->pipeline, found->second.primitives[primitiveIndex].parameterOffset};
}


std::uint32_t MaterialCache::Upload(
    const TextureData &data, const std::string &name, Image &image, VkImageView &view)
{
	UploadTexture(*m_pDeviceContext, data, name, image, view);
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	std::uint32_t texture = m_nextTexture;
	if (!m_freeTextures.empty())
	{
		texture = m_freeTextures.back();
		m_freeTextures.pop_back();
	}
	else if (m_nextTexture < DeviceContext::kTextureTableSize)
		++m_nextTexture;
	else
	{
		vkDestroyImageView(device, view, nullptr);
		m_pDeviceContext->DestroyImage(image);
		image = {};
		view = VK_NULL_HANDLE;
		throw std::runtime_error(
		    std::format(
		        "the texture table's {} elements are all held, and '{}' needs one",
		        DeviceContext::kTextureTableSize, name));
	}

	// No submission reads the element: a new one never was, and one given back waited out the frames
	// in flight (Free).
	const VkDescriptorImageInfo info{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_textureSet;
	write.dstBinding = 0;
	write.dstArrayElement = texture;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	write.pImageInfo = &info;
	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	return texture;
}


void MaterialCache::AppendBlock(
    const TextureSlots &slots, const ResolvedMaterial &material, double time,
    std::vector<glm::vec4> &parameters)
{
	// The indices' bits in floats' places, since the buffer is the parameters' vec4s: the shader
	// reads them back as uints, and nothing on the way does arithmetic on them.
	for (std::size_t slot = 0; slot < slots.size(); slot += kSlotsPerVector)
		parameters.emplace_back(
		    std::bit_cast<float>(slots[slot]), std::bit_cast<float>(slots[slot + 1]),
		    std::bit_cast<float>(slots[slot + 2]), std::bit_cast<float>(slots[slot + 3]));
	EvaluateParameters(material, time, parameters);
}


MaterialCache::TextureSlots MaterialCache::FallbackSlots(const ResolvedMaterial &material)
{
	TextureSlots slots{};
	for (std::size_t slot = 0; slot < kMaterialTextureSlots; ++slot)
		slots[slot] = static_cast<std::uint32_t>(material.fallbacks[slot]);
	return slots;
}


void MaterialCache::Retire(Retired retired)
{
	m_retired.push_back(retired);
}


void MaterialCache::Free(Retired &retired)
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	vkDestroyImageView(device, retired.view, nullptr);
	m_pDeviceContext->DestroyImage(retired.image);
	if (retired.texture != kNoTexture)
		m_freeTextures.push_back(retired.texture);
	retired = {};
}
} // namespace Jettison::Render
