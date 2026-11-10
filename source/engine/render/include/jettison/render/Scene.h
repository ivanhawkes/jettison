#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <jettison/animation/Components.h>
#include <jettison/render/CameraRigs.h>
#include <jettison/render/Procedural.h>
#include <jettison/render/vulkan/Model.h>

namespace Jettison::Render
{
struct SceneEntityId
{
	std::string value;
};

// Relative to the parent, or to the system frame's origin for an entity without one. The
// translation is metres in double precision, so a scene can place a planet a hundred million
// kilometres out and a hull plate on a station beside it; rotation and scale stay float, since
// neither grows with the distance from the origin.
struct TransformComponent
{
	glm::dvec3 translation{0.0};
	glm::vec3 rotation{0.0f};
	glm::vec3 scale{1.0f};
};

// Where something outside the scene puts an entity this frame: the star system, for a celestial
// body. It replaces the transform's translation and rotation, in double precision, and keeps its
// scale. Runtime state, not written to the scene file, so the transform stays as authored and a
// scene saved from a running engine is the scene that was loaded.
struct PlacementComponent
{
	glm::dvec3 position{0.0};
	glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
};

struct ParentComponent
{
	entt::entity entity{entt::null};
};

// The transform in the system frame, composed in double precision. The renderer takes the
// camera's position off it before anything becomes a float (Render::ToRenderSpace).
struct WorldTransformComponent
{
	glm::dmat4 matrix{1.0};
};

// The camera as authored. There is no far plane: the projection is reversed-Z with an infinite far
// plane (Render::ReversedInfinitePerspective).
struct CameraComponent
{
	glm::dvec3 position{2.0, 2.0, 2.0};
	// Scene coordinates are right-handed and Y-up. Radians: pitch about X, roll about view, yaw about
	// Y.
	glm::vec3 rotation{-0.615479709f, 0.0f, 0.785398163f};
	float verticalFovDegrees{45.0f};
	float nearPlane{0.1f};
	bool active{true};
};

// Where a camera is this frame, when something other than its authored pose moves it: a camera
// rig writes it in the presentation phase. Runtime state, not written to the scene file; a camera
// without one is where CameraComponent puts it.
struct CameraPoseComponent
{
	glm::dvec3 position{0.0};
	glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
};

// Today's one point light, read into the frame UBO with an unbounded falloff. The renderer is
// clustered forward (docs/architecture.md, "Rendering: clustered forward"): phase 5 gives this a
// type, a finite range and spot cone angles, moves light data into storage buffers, and makes the
// sun a directional light of its own rather than one of these.
struct LightComponent
{
	glm::vec3 color{1.0f};
	float intensity{6.0f};
	glm::vec3 ambientColor{0.08f};
	bool active{true};
};

// What an entity draws: a model file, or a shape made in code in one colour. A procedural shape is
// unit-sized (a sphere of radius one, a cube from -1 to 1), and the transform's scale sizes it.
struct MeshComponent
{
	std::filesystem::path modelPath;
	std::optional<Shape> shape;
	glm::vec3 color{1.0f};
	std::shared_ptr<Model> model;
};

// An entity the star system places: each frame its translation is the body's position at the
// presentation time, and its rotation the body's spin. The scene names the system file; the app
// evaluates it (engine/celestial), since render does not see the system.
struct CelestialBodyComponent
{
	std::string body;
};

// The star system's one directional light (docs/architecture.md, "Rules that follow"): it shines
// from the entity's position, which is normally the star's, towards whatever the camera sees, and
// lives in the frame constants rather than the light list. Linear colour times intensity.
struct SunComponent
{
	glm::vec3 color{1.0f};
	float intensity{1.0f};
};

// A camera that orbits another entity. The authored fields are where it starts; the rig and the
// resolved target are runtime state, not written to the scene file.
struct OrbitCameraComponent
{
	std::string targetId;
	double distance{10.0};
	double yaw{0.0};
	double pitch{0.3};
	double minDistance{1.0};

	entt::entity target{entt::null};
	OrbitCamera rig;
	CameraTransition transition;
	bool placed{false};
};

class Scene
{
  public:
	explicit Scene(std::shared_ptr<DeviceContext> deviceContext)
	    : m_deviceContext{std::move(deviceContext)}
	{
	}

	Scene(const Scene &) = delete;
	Scene &operator=(const Scene &) = delete;

	void Load(const std::filesystem::path &path);
	void Save(const std::filesystem::path &path) const;
	void Destroy();

	// Counts the times this scene's contents have been thrown away. Pipeline caches each model's
	// Vulkan resources under the address of that Model, and a destroyed model is commonly
	// followed by one allocated at the same address: a cache entry can therefore outlive the
	// model it describes, and be taken for the new one. This counter is how the renderer learns
	// the cache is stale, without the scene having to know who consumes it.
	std::uint64_t Generation() const
	{
		return m_generation;
	}

	// The star system file the scene's celestial bodies come from, absolute; empty for a scene
	// without one.
	const std::filesystem::path &StarSystemPath() const
	{
		return m_starSystemPath;
	}

	entt::registry &Registry()
	{
		return m_registry;
	}
	const entt::registry &Registry() const
	{
		return m_registry;
	}

  private:
	std::shared_ptr<Model> LoadModel(const std::filesystem::path &path);
	std::shared_ptr<Model> ProceduralModel(Shape shape, const glm::vec3 &color);

	std::shared_ptr<DeviceContext> m_deviceContext;
	entt::registry m_registry;
	std::unordered_map<std::string, std::shared_ptr<Model>> m_models;
	std::filesystem::path m_starSystemPath;
	std::uint64_t m_generation{0};
};
} // namespace Jettison::Render