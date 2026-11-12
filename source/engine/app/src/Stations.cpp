#include "Stations.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <array>
#include <string>
#include <utility>

#include <jettison/celestial/StarSystem.h>
#include <jettison/core/Log.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/scene/Components.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::App
{
namespace
{
constexpr Core::Category kStations{"stations"};

// The sample's parked ships: on two or three pads of each station, leaving at least two free.
struct Parked
{
	std::string_view station;
	std::uint32_t pad;
	std::string_view hull;
	float mass;
};
constexpr std::array<Parked, 6> kParked{{
    {"Ostra Highport", 2, "heavy-transport", 1.2e7f},
    {"Ostra Highport", 7, "heavy-transport", 1.2e7f},
    {"Ostra Highport", 11, "heavy-transport", 1.2e7f},
    {"Corva Refinery", 2, "transport", 1.5e6f},
    {"Corva Refinery", 3, "transport", 1.5e6f},
    {"Corva Refinery", 6, "fighter", 3.0e4f},
}};

constexpr std::string_view kPlating = "materials/hull.material.yaml";
constexpr glm::vec3 kDeckColour{0.55f, 0.56f, 0.6f};
constexpr glm::vec3 kPadColour{0.16f, 0.17f, 0.2f};
constexpr glm::vec3 kHullColour{0.62f, 0.64f, 0.7f};
// How thick a pad's plate is drawn, half above the deck and half in it: nothing a hull resting on
// the deck sinks into far enough to see, and apart from the deck's face, so the two do not fight.
constexpr float kPadThickness = 0.05f;

entt::entity Part(
    entt::registry &scene, entt::entity parent, const glm::dvec3 &at, const glm::vec3 &rotation,
    const glm::vec3 &halfSize, const glm::vec3 &colour, std::string_view material)
{
	const entt::entity entity = scene.create();
	scene.emplace<Scene::BuiltComponent>(entity);
	if (parent != entt::null)
		scene.emplace<Scene::ParentComponent>(entity, parent);
	// A box shape spans its scale either side of its middle.
	scene.emplace<Scene::TransformComponent>(entity, at, rotation, halfSize);
	Scene::MeshComponent mesh;
	mesh.shape = Scene::Shape::Box;
	mesh.color = colour;
	mesh.material = std::string{material};
	scene.emplace<Scene::MeshComponent>(entity, std::move(mesh));
	return entity;
}
} // namespace

entt::entity FindStation(const entt::registry &world, std::uint64_t system, std::string_view body)
{
	for (const auto [entity, station, rails, inSystem] :
	     world.view<const Dock::Station, const Physics::OnRails, const Sim::InSystem>().each())
		if (inSystem.system == system && rails.body == body)
			return entity;
	return entt::null;
}

void MakeShip(
    entt::registry &world, entt::entity ship, std::string_view hull, float mass, bool continuous)
{
	Physics::Body body{Physics::BodyKind::Dynamic, Dock::Docks::HullShape(hull), mass};
	body.friction = 0.4f;
	body.restitution = 0.2f;
	body.continuous = continuous;
	world.emplace_or_replace<Physics::Body>(ship, body);
	world.emplace_or_replace<Physics::Velocity>(ship);
	world.emplace_or_replace<Physics::Thrusters>(ship);
	world.emplace_or_replace<Physics::FlightControls>(ship);
	world.emplace_or_replace<Dock::Hull>(ship, std::string{hull});
}

std::vector<entt::entity> OpenStations(
    Sim::SimWorld &world, Dock::Docks &docks, std::span<const Celestial::StarSystem> systems)
{
	std::vector<entt::entity> opened;
	entt::registry &registry = world.Registry();
	for (const Celestial::StarSystem &system : systems)
		for (const Celestial::Body &body : system.bodies)
		{
			if (body.kind != Celestial::BodyKind::Station || body.layout.empty())
				continue;
			if (!docks.FindLayout(body.layout))
			{
				Core::Log(
				    Core::Severity::Warning, kStations,
				    "'{}' is built to '{}', which is not a station layout; it has no pads", body.name,
				    body.layout);
				continue;
			}
			const std::uint64_t id = Sim::SystemId(system.name);
			const entt::entity station = docks.MakeStation(world, id, body.name, body.layout);
			opened.push_back(station);
			for (const Parked &parked : kParked)
			{
				if (parked.station != body.name || !docks.FindHull(parked.hull))
					continue;
				const entt::entity ship = registry.create();
				registry.emplace<Sim::Transform>(ship, registry.get<Sim::Transform>(station));
				registry.emplace<Sim::InSystem>(ship, id);
				registry.emplace<Physics::LocalFrame>(ship, body.name);
				MakeShip(registry, ship, parked.hull, parked.mass, false);
				docks.Park(world, ship, registry.get<Dock::Station>(station).pads.at(parked.pad - 1));
			}
		}
	return opened;
}

void DrawLayout(entt::registry &scene, entt::entity station, const Dock::StationLayout &layout)
{
	for (const std::vector<Dock::Box> *boxes : {&layout.decks, &layout.piers})
		for (const Dock::Box &box : *boxes)
			Part(
			    scene, station, box.centre, glm::vec3{0.0f}, glm::vec3{box.size * 0.5}, kDeckColour,
			    kPlating);
	for (const Dock::PadLayout &pad : layout.pads)
	{
		// The scene turns an entity about X, then Y, then Z (scene/SceneSystems.cpp).
		glm::vec3 rotation{0.0f};
		glm::extractEulerAngleXYZ(
		    glm::mat4{glm::mat4_cast(glm::quat{Dock::PadOrientation(pad)})}, rotation.x, rotation.y,
		    rotation.z);
		const glm::vec2 size = Dock::PadSize(pad.padClass);
		Part(
		    scene, station, pad.position, rotation,
		    {size.x * 0.5f, kPadThickness * 0.5f, size.y * 0.5f}, kPadColour, {});
	}
}

entt::entity MakeHullProxy(entt::registry &scene, const glm::vec3 &size)
{
	const entt::entity root = scene.create();
	scene.emplace<Scene::TransformComponent>(root);
	Part(scene, root, glm::dvec3{0.0}, glm::vec3{0.0f}, size * 0.5f, kHullColour, kPlating);
	return root;
}
} // namespace Jettison::App
