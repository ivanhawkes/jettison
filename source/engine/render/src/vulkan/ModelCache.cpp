#include "vulkan/ModelCache.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

#include <jettison/render/Procedural.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/scene/Components.h>

#include "vulkan/MaterialCache.h"

namespace Jettison::Render
{
namespace
{
bool IsGltf(std::string_view path)
{
	const auto dot = path.rfind('.');
	if (dot == std::string_view::npos)
		return false;
	std::string extension{path.substr(dot)};
	std::ranges::transform(
	    extension, extension.begin(),
	    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return extension == ".gltf" || extension == ".glb";
}

// One model per shape and colour, shared by every entity that asks for it, as a model file is.
std::string ShapeKey(const Scene::MeshComponent &mesh)
{
	std::string key = "procedural:" + std::string{Scene::ShapeName(*mesh.shape)} + ":" +
	                  std::to_string(mesh.color.r) + "," + std::to_string(mesh.color.g) + "," +
	                  std::to_string(mesh.color.b);
	if (*mesh.shape == Scene::Shape::Ring)
		key += ":" + std::to_string(mesh.innerRadius);
	return key;
}
} // namespace

ModelCache::ModelCache(
    std::shared_ptr<DeviceContext> pDeviceContext, Resource::ResourceManager &resources,
    MaterialCache &materials, std::uint32_t framesInFlight)
    : m_pDeviceContext{std::move(pDeviceContext)}, m_resources{resources}, m_materials{materials},
      m_framesInFlight{framesInFlight}
{
}

void ModelCache::Init()
{
	if (m_loaderRegistered)
		return;
	m_resources.RegisterLoader(
	    Resource::Loader<Model>{
	        [](std::span<const std::byte> bytes, std::string_view path)
	        {
		        if (!IsGltf(path))
			        throw std::runtime_error(
			            "Unsupported model format; expected .gltf or .glb: " + std::string{path});
		        return Model::FromGltf(bytes, path);
	        },
	        [this](Model &model, std::string_view path)
	        { model.Upload(*m_pDeviceContext, "model-" + std::string{path}); },
	        [this](Model &model) { Retire(model); }});
	m_loaderRegistered = true;
}

void ModelCache::Destroy()
{
	for (auto &[path, file] : m_files)
	{
		(void)path;
		m_resources.Release(file.handle);
	}
	m_files.clear();
	for (auto &[key, shape] : m_shapes)
	{
		(void)key;
		Retire(*shape.model);
	}
	m_shapes.clear();
	for (Retired &retired : m_retired)
		Free(retired);
	m_retired.clear();
}

void ModelCache::BeginFrame(std::uint64_t frameNumber)
{
	m_frame = frameNumber;
	// As the material cache's: frame N's slot fence has been waited on, so frames up to
	// N - kFramesInFlight are done, and an item retired at frame R was last recorded by frame R.
	const auto done = std::stable_partition(
	    m_retired.begin(), m_retired.end(),
	    [&](const Retired &retired) { return retired.frame + m_framesInFlight > frameNumber; });
	for (auto item = done; item != m_retired.end(); ++item)
		Free(*item);
	m_retired.erase(done, m_retired.end());
}

const Model *ModelCache::Resolve(const Scene::MeshComponent &mesh)
{
	if (mesh.shape)
	{
		const std::string key = ShapeKey(mesh);
		auto found = m_shapes.find(key);
		if (found == m_shapes.end())
		{
			std::vector<Model::MeshLevel> levels;
			for (MeshLod &lod : GenerateLods(*mesh.shape, mesh.color, mesh.innerRadius))
				levels.push_back(
				    {std::move(lod.mesh.vertices), std::move(lod.mesh.indices), lod.error});
			auto pModel = Model::FromMeshes(std::move(levels), key);
			pModel->Upload(*m_pDeviceContext, key);
			found = m_shapes.emplace(key, ShapeModel{std::move(pModel), m_frame}).first;
		}
		found->second.resolvedFrame = m_frame;
		return found->second.model.get();
	}
	auto found = m_files.find(mesh.model);
	if (found == m_files.end())
		found =
		    m_files.emplace(mesh.model, FileModel{m_resources.Load<Model>(mesh.model), m_frame}).first;
	found->second.resolvedFrame = m_frame;
	return m_resources.TryGet(found->second.handle);
}

void ModelCache::ReleaseUnresolved()
{
	std::erase_if(
	    m_files,
	    [this](auto &entry)
	    {
		    if (entry.second.resolvedFrame == m_frame)
			    return false;
		    m_resources.Release(entry.second.handle);
		    return true;
	    });
	std::erase_if(
	    m_shapes,
	    [this](auto &entry)
	    {
		    if (entry.second.resolvedFrame == m_frame)
			    return false;
		    Retire(*entry.second.model);
		    return true;
	    });
}

void ModelCache::Retire(Model &model)
{
	// The model's own materials are keyed on its address, which the next model may be given once
	// this one is gone, so they go with it, now.
	m_materials.Forget(model);
	Retired retired{m_frame};
	model.TakeBuffers(retired.vertexBuffer, retired.indexBuffer);
	m_retired.push_back(retired);
}

void ModelCache::Free(Retired &retired)
{
	m_pDeviceContext->DestroyBuffer(retired.indexBuffer);
	m_pDeviceContext->DestroyBuffer(retired.vertexBuffer);
}
} // namespace Jettison::Render
