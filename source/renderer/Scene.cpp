#include "Scene.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace Jettison::Renderer
{
namespace
{
glm::vec3 ReadVec3(const YAML::Node &node, const char *field, const glm::vec3 &fallback)
{
	const YAML::Node value = node[field];
	if (!value)
		return fallback;
	if (!value.IsSequence() || value.size() != 3)
		throw std::runtime_error(std::string("Scene field '") + field + "' must be a 3-value sequence");
	return {value[0].as<float>(), value[1].as<float>(), value[2].as<float>()};
}

std::filesystem::path RelativeModelPath(
    const std::filesystem::path &modelPath, const std::filesystem::path &scenePath)
{
	std::error_code error;
	auto relativePath = std::filesystem::relative(modelPath, scenePath.parent_path(), error);
	return error ? modelPath : relativePath;
}

void EmitVec3(YAML::Emitter &emitter, const char *key, const glm::vec3 &value)
{
	emitter << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq << value.x << value.y
	        << value.z << YAML::EndSeq;
}
} // namespace

void Scene::Load(const std::filesystem::path &path)
{
	Destroy();
	const auto scenePath = std::filesystem::absolute(path).lexically_normal();
	const YAML::Node document = YAML::LoadFile(scenePath.string());
	if (!document.IsMap() || !document["entities"].IsSequence())
		throw std::runtime_error("Scene file must contain an entities sequence: " + scenePath.string());
	if (document["version"] && document["version"].as<int>() != 1)
		throw std::runtime_error("Unsupported scene version in: " + scenePath.string());

	std::unordered_map<std::string, entt::entity> entities;
	std::vector<std::pair<entt::entity, std::string>> pendingParents;
	std::size_t activeCameraCount = 0;
	std::size_t activeLightCount = 0;

	try
	{
		for (const YAML::Node &entityNode : document["entities"])
		{
			if (!entityNode.IsMap() || !entityNode["id"])
				throw std::runtime_error("Every scene entity must be a mapping with an id");

			const auto id = entityNode["id"].as<std::string>();
			if (id.empty() || entities.count(id) != 0)
				throw std::runtime_error("Scene entity ids must be non-empty and unique: " + id);

			const entt::entity entity = m_registry.create();
			entities.emplace(id, entity);
			m_registry.emplace<SceneEntityId>(entity, id);

			TransformComponent transform;
			if (const YAML::Node transformNode = entityNode["transform"])
			{
				if (!transformNode.IsMap())
					throw std::runtime_error("Entity transform must be a mapping: " + id);
				transform.translation = ReadVec3(transformNode, "translation", transform.translation);
				transform.rotation = ReadVec3(transformNode, "rotation", transform.rotation);
				transform.scale = ReadVec3(transformNode, "scale", transform.scale);
			}
			m_registry.emplace<TransformComponent>(entity, transform);

			if (const YAML::Node cameraNode = entityNode["camera"])
			{
				if (!cameraNode.IsMap())
					throw std::runtime_error("Entity camera must be a mapping: " + id);
				CameraComponent camera;
				camera.position = ReadVec3(cameraNode, "position", camera.position);
				camera.rotation = ReadVec3(cameraNode, "rotation", camera.rotation);
				if (cameraNode["fov"])
					camera.verticalFovDegrees = cameraNode["fov"].as<float>();
				if (cameraNode["near"])
					camera.nearPlane = cameraNode["near"].as<float>();
				if (cameraNode["far"])
					camera.farPlane = cameraNode["far"].as<float>();
				if (cameraNode["active"])
					camera.active = cameraNode["active"].as<bool>();
				if (camera.verticalFovDegrees <= 0.0f || camera.verticalFovDegrees >= 180.0f ||
				    camera.nearPlane <= 0.0f || camera.farPlane <= camera.nearPlane)
					throw std::runtime_error("Invalid camera projection settings: " + id);
				if (camera.active && ++activeCameraCount > 1)
					throw std::runtime_error("A scene can have only one active camera");
				m_registry.emplace<CameraComponent>(entity, camera);
			}

			if (const YAML::Node lightNode = entityNode["light"])
			{
				if (!lightNode.IsMap())
					throw std::runtime_error("Entity light must be a mapping: " + id);
				LightComponent light;
				light.color = ReadVec3(lightNode, "color", light.color);
				light.ambientColor = ReadVec3(lightNode, "ambient", light.ambientColor);
				if (lightNode["intensity"])
					light.intensity = lightNode["intensity"].as<float>();
				if (lightNode["active"])
					light.active = lightNode["active"].as<bool>();
				if (light.intensity < 0.0f)
					throw std::runtime_error("Light intensity cannot be negative: " + id);
				if (light.active && ++activeLightCount > 1)
					throw std::runtime_error("A scene can have only one active light");
				m_registry.emplace<LightComponent>(entity, light);
			}

			if (const YAML::Node parentNode = entityNode["parent"])
				pendingParents.emplace_back(entity, parentNode.as<std::string>());

			bool hasMesh = false;
			if (const YAML::Node meshNode = entityNode["mesh"])
			{
				if (!meshNode.IsMap() || !meshNode["model"])
					throw std::runtime_error("Entity mesh needs a model field: " + id);

				const auto modelPath =
				    (scenePath.parent_path() / meshNode["model"].as<std::string>()).lexically_normal();
				auto model = LoadModel(modelPath);
				const bool hasSceneGraph = model->HasSceneGraph();
				m_registry.emplace<MeshComponent>(entity, modelPath, model);
				if (hasSceneGraph)
					m_registry.emplace<SkeletonComponent>(entity);
				hasMesh = true;
			}

			if (const YAML::Node animationNode = entityNode["transformAnimation"])
			{
				// A mesh is not part of what a rotation animation runs on: the update system walks
				// transform and rotation, and ComputeWorldTransform composes the angle, so an entity
				// with nothing to draw can still spin. The clip player below is the one that needs
				// the mesh, for a skin and a clip to sample. Save writes this block for every entity
				// that carries the component, and a loader demanding a mesh here refused a file the
				// writer had just produced.
				if (!animationNode.IsMap())
					throw std::runtime_error("Entity transform animation must be a mapping: " + id);

				RotationAnimationComponent animation;
				animation.rotationAxis = ReadVec3(animationNode, "axis", animation.rotationAxis);
				if (animationNode["speed"])
					animation.rotationSpeed = animationNode["speed"].as<float>();
				if (glm::length(animation.rotationAxis) == 0.0f)
					throw std::runtime_error("Animation rotation axis cannot be zero: " + id);
				m_registry.emplace<RotationAnimationComponent>(entity, animation);
			}

			if (const YAML::Node animationNode = entityNode["animation"])
			{
				if (!hasMesh || !animationNode.IsMap() || !animationNode["clip"])
					throw std::runtime_error("Entity animation needs a mesh and clip name: " + id);

				AnimationPlayerComponent player;
				player.clipName = animationNode["clip"].as<std::string>();
				if (player.clipName.empty())
					throw std::runtime_error("Animation clip name cannot be empty: " + id);
				const auto &mesh = m_registry.get<MeshComponent>(entity);
				if (!mesh.model->HasSkin() || !mesh.model->HasAnimation(player.clipName))
					throw std::runtime_error(
					    "glTF skin or animation clip is missing for entity: " + id);
				if (animationNode["time"])
					player.time = animationNode["time"].as<float>();
				if (animationNode["speed"])
					player.playbackSpeed = animationNode["speed"].as<float>();
				if (animationNode["loop"])
					player.looping = animationNode["loop"].as<bool>();
				if (animationNode["playing"])
					player.playing = animationNode["playing"].as<bool>();
				m_registry.emplace<AnimationPlayerComponent>(entity, std::move(player));
			}
		}

		for (const auto &[entity, parentId] : pendingParents)
		{
			const auto parent = entities.find(parentId);
			if (parent == entities.end() || parent->second == entity)
				throw std::runtime_error("Invalid parent '" + parentId + "' in scene");
			m_registry.emplace<ParentComponent>(entity, parent->second);
		}
	}
	catch (...)
	{
		Destroy();
		throw;
	}
}

void Scene::Save(const std::filesystem::path &path) const
{
	const auto scenePath = std::filesystem::absolute(path).lexically_normal();
	std::filesystem::create_directories(scenePath.parent_path());

	std::vector<entt::entity> entities;
	const auto view = m_registry.view<const SceneEntityId, const TransformComponent>();
	entities.assign(view.begin(), view.end());
	std::sort(
	    entities.begin(), entities.end(),
	    [this](entt::entity lhs, entt::entity rhs)
	    {
		    return m_registry.get<SceneEntityId>(lhs).value < m_registry.get<SceneEntityId>(rhs).value;
	    });

	YAML::Emitter emitter;
	emitter << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1;
	emitter << YAML::Key << "entities" << YAML::Value << YAML::BeginSeq;
	for (const entt::entity entity : entities)
	{
		emitter << YAML::BeginMap << YAML::Key << "id" << YAML::Value
		        << m_registry.get<SceneEntityId>(entity).value;
		if (const auto *parent = m_registry.try_get<ParentComponent>(entity))
			emitter << YAML::Key << "parent" << YAML::Value
			        << m_registry.get<SceneEntityId>(parent->entity).value;

		const auto &transform = m_registry.get<TransformComponent>(entity);
		emitter << YAML::Key << "transform" << YAML::Value << YAML::BeginMap;
		EmitVec3(emitter, "translation", transform.translation);
		EmitVec3(emitter, "rotation", transform.rotation);
		EmitVec3(emitter, "scale", transform.scale);
		emitter << YAML::EndMap;

		if (const auto *camera = m_registry.try_get<CameraComponent>(entity))
		{
			emitter << YAML::Key << "camera" << YAML::Value << YAML::BeginMap;
			EmitVec3(emitter, "position", camera->position);
			EmitVec3(emitter, "rotation", camera->rotation);
			emitter << YAML::Key << "fov" << YAML::Value << camera->verticalFovDegrees;
			emitter << YAML::Key << "near" << YAML::Value << camera->nearPlane;
			emitter << YAML::Key << "far" << YAML::Value << camera->farPlane;
			emitter << YAML::Key << "active" << YAML::Value << camera->active;
			emitter << YAML::EndMap;
		}
		if (const auto *light = m_registry.try_get<LightComponent>(entity))
		{
			emitter << YAML::Key << "light" << YAML::Value << YAML::BeginMap;
			EmitVec3(emitter, "color", light->color);
			emitter << YAML::Key << "intensity" << YAML::Value << light->intensity;
			EmitVec3(emitter, "ambient", light->ambientColor);
			emitter << YAML::Key << "active" << YAML::Value << light->active;
			emitter << YAML::EndMap;
		}

		if (const auto *mesh = m_registry.try_get<MeshComponent>(entity))
		{
			emitter << YAML::Key << "mesh" << YAML::Value << YAML::BeginMap;
			emitter << YAML::Key << "model" << YAML::Value
			        << RelativeModelPath(mesh->modelPath, scenePath).generic_string();
			emitter << YAML::EndMap;
		}
		if (const auto *animation = m_registry.try_get<RotationAnimationComponent>(entity))
		{
			// Written for every entity that carries it, mesh or not: the loader accepts it on an
			// entity with nothing to draw, because a rotation animation runs on the transform.
			emitter << YAML::Key << "transformAnimation" << YAML::Value << YAML::BeginMap;
			EmitVec3(emitter, "axis", animation->rotationAxis);
			emitter << YAML::Key << "speed" << YAML::Value << animation->rotationSpeed;
			emitter << YAML::EndMap;
		}
		// A clip player runs on a model's skin: SceneUpdateSystem walks mesh and skeleton, and the
		// loader refuses an animation block on an entity with no model to sample. With nothing to
		// animate there is no clip name worth writing, so this block goes with the mesh.
		if (const auto *player = m_registry.try_get<AnimationPlayerComponent>(entity);
		    player && m_registry.all_of<MeshComponent>(entity))
		{
			emitter << YAML::Key << "animation" << YAML::Value << YAML::BeginMap;
			emitter << YAML::Key << "clip" << YAML::Value << player->clipName;
			// No time: the player's time is the phase the clip is at, which the frame loop owns.
			// Writing it starts a saved-and-reloaded scene out of step with the scene graph that
			// was authored, and in a long-running session it writes a phase that has grown with
			// the clock. Load reads the key, so a scene file can still ask for a start offset.
			emitter << YAML::Key << "speed" << YAML::Value << player->playbackSpeed;
			emitter << YAML::Key << "loop" << YAML::Value << player->looping;
			emitter << YAML::Key << "playing" << YAML::Value << player->playing;
			emitter << YAML::EndMap;
		}
		emitter << YAML::EndMap;
	}
	emitter << YAML::EndSeq << YAML::EndMap;
	if (!emitter.good())
		throw std::runtime_error("Failed to serialize scene: " + emitter.GetLastError());

	std::ofstream output{scenePath};
	if (!output)
		throw std::runtime_error("Failed to open scene for writing: " + scenePath.string());
	output << emitter.c_str() << '\n';
	if (!output)
		throw std::runtime_error("Failed to write scene: " + scenePath.string());
}

void Scene::Destroy()
{
	// Every consumer that keys anything on a model's address has to be told this happened, and
	// Load() calls here first: a reload destroys the models and the next allocation reuses them.
	++m_generation;
	// The models own vertex and index buffers that the last submitted frame still references:
	// a scene reload happens between frames, not at a fence, so the engine is mid-flight here.
	// Validation reports destroying such a buffer as VUID-vkDestroyBuffer-buffer-00922, and the
	// driver is entitled to hand that memory to the next allocation. Waiting for the device is
	// the right cost on a reload, which is a rare event; the alternative is a retirement queue
	// keyed on fences the scene cannot see.
	if (m_deviceContext)
		check_vk_result(m_deviceContext->WaitIdle());
	m_registry.clear();
	for (const auto &[path, model] : m_models)
	{
		(void)path;
		model->Destroy();
	}
	m_models.clear();
}

std::shared_ptr<Model> Scene::LoadModel(const std::filesystem::path &path)
{
	std::error_code error;
	const auto resolvedPath = std::filesystem::weakly_canonical(path, error);
	const auto modelPath = error ? path : resolvedPath;
	const std::string key = modelPath.string();
	if (const auto found = m_models.find(key); found != m_models.end())
		return found->second;

	if (!std::filesystem::is_regular_file(modelPath))
		throw std::runtime_error("Scene model file does not exist: " + modelPath.string());
	auto model = std::make_shared<Model>(m_deviceContext);
	model->LoadModel(modelPath);
	m_models.emplace(key, model);
	return model;
}
} // namespace Jettison::Renderer