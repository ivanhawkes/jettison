#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Jettison::Resource
{
class ResourceManager;
}

namespace Jettison::Scene
{
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
	void Save(const std::filesystem::path &path) const;
	std::string SaveText() const;
	// Empties the scene, releasing the rigs it loaded.
	void Clear();

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
