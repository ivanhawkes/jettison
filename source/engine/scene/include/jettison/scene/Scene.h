#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/scene/Components.h>

namespace Jettison::Resource
{
class ResourceManager;
}

namespace Jettison::Scene
{
// A scene file read and checked, not yet made into entities (Scene::Load). What a jump reads its
// destination's scene as before it replaces the scene (scene/JumpTransition.h), so a file that is
// refused leaves the scene that is shown as it was.
class SceneFile
{
  public:
	// Throws, as Scene::LoadText does, for a file the scene would refuse.
	static SceneFile Read(std::string_view text);

	SceneFile(SceneFile &&) noexcept;
	SceneFile &operator=(SceneFile &&) noexcept;
	~SceneFile();

	// The star system it names, by virtual path; empty for a file that names none.
	const std::string &StarSystem() const;
	// The celestial bodies its entities present, in the file's order.
	std::vector<std::string> CelestialBodies() const;

  private:
	friend class Scene;
	struct Document;
	explicit SceneFile(std::unique_ptr<Document> pDocument);

	std::unique_ptr<Document> m_pDocument;
};

// A presentation scene (docs/architecture.md, "Simulation and presentation"): an EnTT registry of
// the components in scene/Components.h, loaded from and saved to a scene file. It names no device.
// What it draws is by name, a model's virtual path or a procedural shape, and the renderer loads or
// makes it; the rig a model's clip player samples is loaded here, through the resource manager.
//
// The file is YAML through the reflection archives (serialise/Archive.h), at kFormatVersion: a field
// it does not know is refused, naming its path and line, as is a version other than this one. The
// loader and the writer share one check of what a file may say (Validate, in Scene.cpp), so the
// writer cannot produce a file the loader refuses. Every path in it (the star system, a model, a
// material) is a virtual path, found through the asset mounts, so a mod can shadow any of them.
class Scene
{
  public:
	// The scene file's version: 2 since the scene left render (phase 6a), when a model's path became
	// a virtual path rather than one relative to the file.
	static constexpr std::uint32_t kFormatVersion = 2;

	// The resource manager loads each model mesh's rig, and outlives the scene. A scene without one
	// refuses a file with a model mesh, which the tests that need no assets rely on.
	explicit Scene(Resource::ResourceManager *pResources = nullptr) : m_pResources{pResources}
	{
	}
	~Scene();

	Scene(const Scene &) = delete;
	Scene &operator=(const Scene &) = delete;

	// Replaces the scene with the file's. A file that fails leaves the scene empty, and the exception
	// names it.
	void Load(const std::filesystem::path &path);
	void LoadText(std::string_view text);
	// Replaces the scene with a file already read and checked; it fails only where making the
	// entities does (a model, whose rig this scene has no resource manager to load), and then
	// leaves the scene empty.
	void Load(const SceneFile &file);
	void Save(const std::filesystem::path &path) const;
	std::string SaveText() const;
	// Replaces the scene with one that authors nothing and names a star system, by virtual path: the
	// system scene (scene/SystemScene.h) then builds all of it from the system file. Saved, it is a
	// file of that system and no entities.
	void LoadSystem(std::string_view starSystem);
	// Empties the scene, releasing the rigs it loaded.
	void Clear();

	// Destroys one entity, releasing its rig if it has one. What is parented to it is left with a
	// parent that no longer exists, which the scene update refuses, so the caller destroys a whole
	// tree.
	void Destroy(entt::entity entity);

	// Whether a layer is drawn and lights the scene; every layer is, to begin with. Held in the
	// registry's context (LayerVisibility), so it outlasts a load, and the scene update applies it.
	void SetLayerVisible(Layer layer, bool visible);
	bool IsLayerVisible(Layer layer) const;

	// Counts the times this scene's contents have been replaced, so the renderer knows to rebake the
	// environment and release what the new scene no longer draws.
	std::uint64_t Generation() const
	{
		return m_generation;
	}

	// The virtual path of the star system file the scene's celestial bodies come from; empty for a
	// scene without one.
	const std::string &StarSystem() const
	{
		return m_starSystem;
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
	Resource::ResourceManager *m_pResources;
	entt::registry m_registry;
	std::string m_starSystem;
	std::uint64_t m_generation{0};
};
} // namespace Jettison::Scene
