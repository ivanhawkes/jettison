#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>
#include <vector>

#include <jettison/animation/Components.h>
#include <jettison/render/Culling.h>

namespace Jettison::Render
{
class Model;
class ModelCache;

struct SceneDrawCommand
{
	const Model *model{};
	// In render space: metres from the camera, as a float.
	glm::mat4 renderTransform{1.0f};
	const Animation::SkeletonComponent *skeleton{};
	// The mesh's material path; empty for the model's own. Points into the scene, which outlives
	// the frame's draw list.
	const std::string *material{};
	// The model's bounds in render space, taken in double precision, and the transform's largest
	// scale, which its levels' errors are measured in.
	BoundingSphere bounds;
	double scale{1.0};
	// A celestial body drawn as a sphere hides what is behind its horizon (Render::Occludes): this is
	// its radius, the transform's smallest scale, before the drawn level's error takes it inside the
	// mesh. Zero for every other draw.
	double occluderRadius{0.0};

	bool Skinned() const
	{
		return skeleton != nullptr && !skeleton->jointMatrices.empty();
	}
};

// Every mesh the scene draws this frame, made camera-relative against the camera's position, with
// the model the cache has for it. A mesh whose model is still loading is left out, and so is a model
// file's mesh until its rig has posed it (Scene::SceneUpdateSystem): the rig is what places the mesh
// within its file.
std::vector<SceneDrawCommand> CollectDraws(
    const entt::registry &registry, const glm::dvec3 &camera, ModelCache &models);
} // namespace Jettison::Render
