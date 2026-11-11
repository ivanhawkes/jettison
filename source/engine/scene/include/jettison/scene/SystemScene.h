#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <jettison/celestial/StarSystem.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Scene
{
class Scene;

// Every entity naming a celestial body, checked against the system. Throws std::runtime_error
// naming the first body the system does not have, at load, so a misspelt name is a start-up error
// rather than a body that never moves.
void ResolveCelestialBodies(const entt::registry &presentation, const Celestial::StarSystem &system);

// The sync phase's half for bodies on rails: each entity with a CelestialBodyComponent is placed
// where its body is at simSeconds, and turned as the body spins, through a PlacementComponent, so
// the authored transform is untouched. The time is the presentation time between two steps
// (Celestial::PresentationTime): every body shown is shown at the same moment, on its orbit.
void PlaceCelestialBodies(
    entt::registry &presentation, const Celestial::StarSystem &system, double simSeconds);

// The sync phase's half for the simulation: writes each proxy's simulation transform, alpha of the
// way from previous to current, into its TransformComponent. The position stays in double
// precision, in the system frame; it becomes a float only when the renderer takes the camera's
// position off it (docs/architecture.md, "Coordinate frames and units"). A proxy whose simulation
// entity has no transform is left where it was: the events that say why are presentation's to act
// on.
void SyncSimProxies(
    entt::registry &presentation, const Sim::Snapshot &previous, const Sim::Snapshot &current,
    double alpha);

// The id the system scene gives a body's entity: its name in lower case, each run of anything but
// letters and digits one hyphen ("Ostra Highport" is ostra-highport).
std::string BodyEntityId(std::string_view bodyName);

// Makes the presentation of a simulation entity the scene has no proxy for: the game's to supply,
// since only it knows what a ship looks like. Returns the proxy's root, which the system scene
// then makes the entity's proxy, or null to leave the entity unshown while it stays in the
// snapshots.
using ProxyFactory = std::function<entt::entity(entt::registry &presentation, entt::entity simEntity)>;

// The presentation scene for one star system (docs/scope.md, "Scene: management"), built from the
// system description and the simulation state onto a scene, which holds what a scene file authored
// or nothing at all (Scene::LoadSystem).
//
// The file's own entities are kept as they are. Each body that no entity of the file presents gets
// an entity of its own, in the environment layer, looking as its kind does (a sun on the star, a
// sphere the body's size for a planet or moon, a hull for a station, a ring in the effects layer
// for a jump point), so a system file alone is a scene whose bodies and stations follow their
// orbits. A scene with no sky gets one seeded from the system's name, and one with no active
// camera an orbit camera on the first station, or else the first planet, or else the primary. What
// it builds is a BuiltComponent, which the scene's writer leaves out.
//
// The traffic layer is the simulation's entities in this system: SyncTraffic gives each entity the
// snapshots put in it (Sim::InSystem) a proxy from the factory, and destroys the proxy when its
// entity leaves the snapshots or the system. The simulation is the galaxy's, so an entity in another
// system, or in transit between two, is not this scene's to show.
//
// It holds the scene by reference, and the scene outlives it. A scene loaded again needs a system
// scene built again: what this one built went with the scene's old contents.
class SystemScene
{
  public:
	// Builds the system's scene. Throws std::runtime_error for a body the scene names and the system
	// lacks, or for a built entity's id that the scene already gives an entity of its own; the scene
	// is then as it was.
	SystemScene(Scene &scene, Celestial::StarSystem system);

	SystemScene(const SystemScene &) = delete;
	SystemScene &operator=(const SystemScene &) = delete;

	const Celestial::StarSystem &System() const
	{
		return m_system;
	}

	// Places every body, the built and the authored, at the presentation time.
	void Place(double simSeconds);

	void SetProxyFactory(ProxyFactory factory)
	{
		m_factory = std::move(factory);
	}
	// The traffic layer: a proxy from the factory for each entity the current snapshot has in this
	// system and that has none (asked once while the entity stays), the proxies of entities gone from
	// it or from the system destroyed with their children, then every proxy interpolated
	// (SyncSimProxies). A proxy the factory made is a BuiltComponent and in the traffic layer unless
	// the factory gave it another. An entity that leaves and comes back is asked for again.
	void SyncTraffic(const Sim::Snapshot &previous, const Sim::Snapshot &current, double alpha);

	// Destroys every entity the system scene built, the proxies with it and whatever is parented to
	// them, and leaves what the file authored: the scene as it was loaded.
	void Unload();

  private:
	void Build();
	entt::entity BuildBody(std::size_t body);
	void BuildSky();

	// Whether the current snapshot has the entity in this system.
	bool IsHere(const Sim::Snapshot &current, entt::entity entity) const;

	Scene *m_pScene;
	Celestial::StarSystem m_system;
	// The system's Sim::SystemId, which the snapshots name it by.
	std::uint64_t m_systemId;
	ProxyFactory m_factory;
	// Simulation entity to the proxy the factory made for it, and the entities it declined.
	std::unordered_map<entt::entity, entt::entity> m_proxies;
	std::unordered_set<entt::entity> m_declined;
};
} // namespace Jettison::Scene
