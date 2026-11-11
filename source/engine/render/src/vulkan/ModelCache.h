#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <jettison/resource/ResourceHandle.h>

#include "vulkan/DeviceContext.h"
#include "vulkan/Model.h"

namespace Jettison::Resource
{
class ResourceManager;
}
namespace Jettison::Scene
{
struct MeshComponent;
}

namespace Jettison::Render
{
class MaterialCache;

// The models a scene draws, by what the scene names (Scene::MeshComponent). A model file is a
// resource: decoded on the I/O worker, uploaded by its loader's finish, reloaded when its file
// changes, and keyed on its virtual path. A procedural shape is made here on first use, keyed on its
// shape, colour and inner radius. Neither is keyed on an address, so a scene reload that frees one
// model and makes another where it was cannot be mistaken for the old one: the cache that once was
// keyed so needed the scene to count its reloads and the device to idle on each.
//
// Nothing a frame in flight may read is freed on the spot. A model that a reload replaced, or that no
// scene draws any more, hands its buffers to a queue that waits out kFramesInFlight frames, and its
// own materials to the material cache's, which waits the same.
class ModelCache
{
  public:
	ModelCache(
	    std::shared_ptr<DeviceContext> pDeviceContext, Resource::ResourceManager &resources,
	    MaterialCache &materials, std::uint32_t framesInFlight);
	ModelCache(const ModelCache &) = delete;
	ModelCache &operator=(const ModelCache &) = delete;

	// Registers the model loader, so a renderer is made once per resource manager.
	void Init();
	// Releases every model and frees everything, queued or not: the device has to be idle.
	void Destroy();

	// The frame's start, after its slot's fence: what was retired kFramesInFlight frames before is
	// no longer read by any submission, and is freed.
	void BeginFrame(std::uint64_t frameNumber);
	// The model the mesh draws this frame: null while its file loads for the first time, and after it
	// failed, which the resource manager reports.
	const Model *Resolve(const Scene::MeshComponent &mesh);
	// Releases every model this frame did not resolve: after a scene reload, the ones the new scene
	// does not draw.
	void ReleaseUnresolved();

  private:
	struct FileModel
	{
		Resource::ResourceHandle<Model> handle;
		std::uint64_t resolvedFrame{0};
	};
	struct ShapeModel
	{
		std::unique_ptr<Model> model;
		std::uint64_t resolvedFrame{0};
	};
	struct Retired
	{
		std::uint64_t frame{0};
		Buffer vertexBuffer{};
		Buffer indexBuffer{};
	};

	// The model's buffers and own materials into the queues, as of this frame.
	void Retire(Model &model);
	void Free(Retired &retired);

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	Resource::ResourceManager &m_resources;
	MaterialCache &m_materials;
	const std::uint32_t m_framesInFlight;

	std::unordered_map<std::string, FileModel> m_files;
	std::unordered_map<std::string, ShapeModel> m_shapes;
	std::vector<Retired> m_retired;
	std::uint64_t m_frame{0};
	bool m_loaderRegistered{false};
};
} // namespace Jettison::Render
