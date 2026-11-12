#include "vulkan/MaterialCache.h"

#include <algorithm>
#include <cstdint>
#include <exception>
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
    VkDescriptorSetLayout materialLayout, VkPipelineLayout sceneLayout, const RenderState &sceneState)
{
	m_layout = materialLayout;
	m_sceneLayout = sceneLayout;
	m_sceneState = sceneState;
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
	// pixel: on lavapipe at 640 x 480, the hull's plating on the display wing's round hulls, seen at
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

	// By TextureFallback: white, black, and a flat tangent-space normal (0, 0, 1), stored as
	// unsigned bytes and so linear.
	const std::array<TextureData, 3> fallbacks{
	    SolidTexture(255, 255, 255, 255, true), SolidTexture(0, 0, 0, 255, true),
	    SolidTexture(128, 128, 255, 255, false)};
	const std::array<const char *, 3> names{"fallback-white", "fallback-black", "fallback-normal"};
	for (std::size_t index = 0; index < fallbacks.size(); ++index)
		Upload(fallbacks[index], names[index], m_fallbackImages[index], m_fallbackViews[index]);

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
		        Upload(texture.data, "texture-" + std::string{path}, texture.image, texture.view);
		        // The device has its copy; the CPU one is a texture's worth of memory for nothing.
		        std::vector<std::byte>{}.swap(texture.data.bytes);
	        },
	        [this](TextureResource &texture)
	        {
		        Retire({m_frame, texture.image, texture.view, VK_NULL_HANDLE});
		        texture.image = {};
		        texture.view = VK_NULL_HANDLE;
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
	// The pipelines are the pipeline cache's, which destroys them.
	m_programs.clear();
	m_pDefaultProgram = nullptr;
}


void MaterialCache::Forget(const Model &model)
{
	const auto found = m_models.find(&model);
	if (found == m_models.end())
		return;
	for (const ModelMaterial &material : found->second)
		Retire({m_frame, material.image, material.view, material.pool});
	m_models.erase(found);
}


void MaterialCache::ForgetAll()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	for (auto &[model, materials] : m_models)
	{
		(void)model;
		for (ModelMaterial &material : materials)
		{
			vkDestroyDescriptorPool(device, material.pool, nullptr);
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
		EvaluateParameters(m_pDefaultProgram->defaults, 0.0, parameters);
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
	program.pipeline = m_pipelines.Get({name, m_sceneState, m_sceneLayout});
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
		EvaluateParameters(material.resolved, time, parameters);
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
	bool rebuild = material.set == VK_NULL_HANDLE;
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
			material.textureRevisions = {};
			material.resolved = std::move(resolved);
			material.pipeline = program.pipeline;
			rebuild = true;
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

	std::array<VkImageView, kMaterialTextureSlots> views{};
	for (std::size_t binding = 0; binding < kMaterialTextureSlots; ++binding)
	{
		const auto &texture = material.textures[binding];
		std::uint32_t textureRevision = 0;
		views[binding] = FallbackView(material.resolved.fallbacks[binding]);
		if (!texture.IsNull())
		{
			const Resource::Status status = m_resources.StatusOf(texture);
			if (status == Resource::Status::Loading)
				return false;
			// A texture that failed (the resource manager said why) draws as its slot's fallback
			// until a reload fixes it.
			if (status == Resource::Status::Ready)
			{
				views[binding] = m_resources.TryGet(texture)->view;
				textureRevision = m_resources.RevisionOf(texture);
			}
		}
		if (textureRevision != material.textureRevisions[binding])
		{
			material.textureRevisions[binding] = textureRevision;
			rebuild = true;
		}
	}

	if (rebuild)
	{
		// Sets are never written in place: a frame in flight may have the old one bound.
		if (material.pool != VK_NULL_HANDLE)
			Retire({m_frame, {}, VK_NULL_HANDLE, material.pool});
		material.pool = VK_NULL_HANDLE;
		material.set = VK_NULL_HANDLE;
		AllocateSet(views, "material-" + virtualPath, material.pool, material.set);
	}
	material.binding.pipeline = material.pipeline;
	material.binding.set = material.set;
	return true;
}


void MaterialCache::ReleaseNamed(NamedMaterial &material)
{
	for (const auto &texture : material.textures)
		if (!texture.IsNull())
			m_resources.Release(texture);
	if (!material.definition.IsNull())
		m_resources.Release(material.definition);
	if (material.pool != VK_NULL_HANDLE)
		Retire({m_frame, {}, VK_NULL_HANDLE, material.pool});
	material = {};
}


MaterialCache::Binding MaterialCache::Ensure(const Model &model, std::size_t primitiveIndex)
{
	if (primitiveIndex >= model.m_primitives.size())
		throw std::runtime_error("Model primitive index is out of range");
	auto &materials = m_models[&model];
	const Binding binding{m_pDefaultProgram->pipeline, VK_NULL_HANDLE, 0};
	if (primitiveIndex < materials.size())
		return {binding.pipeline, materials[primitiveIndex].set, 0};
	if (primitiveIndex != materials.size())
		throw std::runtime_error("Model primitive materials must be created in order");

	const std::string name = ModelKey(&model) + "-p" + std::to_string(primitiveIndex);
	ModelMaterial material;
	std::array<VkImageView, kMaterialTextureSlots> views{};
	for (std::size_t slot = 0; slot < kMaterialTextureSlots; ++slot)
		views[slot] = FallbackView(m_pDefaultProgram->defaults.fallbacks[slot]);
	const auto &imageData = model.m_primitives[primitiveIndex].baseColorImageData;
	try
	{
		if (!imageData.empty())
		{
			// glTF says a base-colour image is sRGB.
			const TextureData texture = DecodeImage(imageData, true, name);
			Upload(texture, "texture-" + name, material.image, material.view);
			views[0] = material.view;
		}
		AllocateSet(views, name, material.pool, material.set);
	}
	catch (...)
	{
		vkDestroyImageView(m_pDeviceContext->GetLogicalDevice(), material.view, nullptr);
		m_pDeviceContext->DestroyImage(material.image);
		throw;
	}
	materials.push_back(material);
	return {binding.pipeline, material.set, 0};
}


void MaterialCache::AllocateSet(
    const std::array<VkImageView, kMaterialTextureSlots> &views, const std::string &name,
    VkDescriptorPool &pool, VkDescriptorSet &set)
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	const VkDescriptorPoolSize poolSize{
	    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaterialTextureSlots};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 1;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create a material descriptor pool");
	m_pDeviceContext->SetDebugName(pool, name + "-pool");

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_layout;
	if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS)
	{
		vkDestroyDescriptorPool(device, pool, nullptr);
		pool = VK_NULL_HANDLE;
		throw std::runtime_error("Failed to allocate a material descriptor set");
	}
	m_pDeviceContext->SetDebugName(set, name + "-set");

	std::array<VkDescriptorImageInfo, kMaterialTextureSlots> images{};
	for (std::size_t binding = 0; binding < kMaterialTextureSlots; ++binding)
		images[binding] = {m_sampler, views[binding], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = set;
	write.dstBinding = 0;
	write.descriptorCount = static_cast<uint32_t>(images.size());
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = images.data();
	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}


VkImageView MaterialCache::FallbackView(TextureFallback fallback) const
{
	return m_fallbackViews[static_cast<std::size_t>(fallback)];
}


void MaterialCache::Upload(
    const TextureData &data, const std::string &name, Image &image, VkImageView &view)
{
	UploadTexture(*m_pDeviceContext, data, name, image, view);
}


void MaterialCache::Retire(Retired retired)
{
	m_retired.push_back(retired);
}


void MaterialCache::Free(Retired &retired)
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	vkDestroyDescriptorPool(device, retired.pool, nullptr);
	vkDestroyImageView(device, retired.view, nullptr);
	m_pDeviceContext->DestroyImage(retired.image);
	retired = {};
}
} // namespace Jettison::Render
