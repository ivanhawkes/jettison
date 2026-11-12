#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>
#include <vector>

#include <jettison/animation/Components.h>
#include <jettison/render/Culling.h>

namespace Jettison::Scene
{
struct AtmosphereComponent;
struct RingsComponent;
} // namespace Jettison::Scene

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
	// The model's radius times the transform's middle scale: how wide the draw is at its narrowest
	// that a viewer sees (Render::TooSmallToSee).
	double breadth{0.0};
	// A celestial body drawn as a sphere hides what is behind its horizon (Render::Occludes): this is
	// its radius, the transform's smallest scale, before the drawn level's error takes it inside the
	// mesh. Zero for every other draw.
	double occluderRadius{0.0};
	// Such a body's air and rings, which point into the scene, and its spin axis in render space,
	// the transform's Y axis, which the rings' plane is square to.
	const Scene::AtmosphereComponent *atmosphere{};
	const Scene::RingsComponent *rings{};
	glm::vec3 spinAxis{0.0f, 1.0f, 0.0f};
	// The mesh's colour: what it shows without its material, and the albedo the environment's
	// capture draws its sphere with.
	glm::vec3 color{1.0f};

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
