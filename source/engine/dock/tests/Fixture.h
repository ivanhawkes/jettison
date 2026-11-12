#pragma once

#include <jettison/ai/Steering.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/spatial/SystemIndices.h>
#include <jettison/vfs/FileSystem.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// What the dock suites share: a world with physics, the spatial index, steering and the docks, in
// the sample's Halcyon, read from its file, and the sample's hulls and stations from theirs.
namespace Jettison::DockTests
{
inline constexpr double kStep = 1.0 / 60.0;
inline const std::uint64_t kHalcyon = Sim::SystemId("Halcyon");
inline constexpr std::string_view kHighport = "Ostra Highport";

inline std::string ReadAsset(std::string_view path)
{
	std::ifstream file{std::string{JETTISON_ASSET_SOURCE_DIR} + "/" + std::string{path}};
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

struct Fixture
{
	Sim::SimWorld world{kStep, 9};
	Physics::PhysicsWorld &physics{Setup(world)};
	Spatial::SystemIndices &indices{Spatial::SystemIndices::Attach(world)};
	Ai::Steering &steering{Ai::Steering::Attach(world, physics, indices)};
	Dock::Docks &docks{Dock::Docks::Attach(world, physics)};
	std::vector<Physics::ContactEvent> collisions;
	std::vector<Sim::SimEvent> events;

	// With the sample's definitions loaded, or with none, for a suite that defines its own.
	explicit Fixture(bool sample = true)
	{
		physics.DefineStarSystem(Celestial::ReadStarSystem(ReadAsset("systems/halcyon.yaml")));
		if (!sample)
			return;
		Vfs::FileSystem files;
		files.AddMount({"base", JETTISON_ASSET_SOURCE_DIR, false});
		Sim::DefinitionRegistry definitions{files};
		definitions.Load<Dock::HullDefinition>(Dock::kHullDirectory, {Dock::kDefinitionVersion});
		definitions.Load<Dock::StationLayout>(Dock::kStationDirectory, {Dock::kDefinitionVersion});
		docks.Define(definitions);
	}

	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InSystem>();
		world.RegisterComponent<Sim::ControlledBy>();
		return Physics::PhysicsWorld::Attach(world);
	}

	// A ship of the hull at rest, that far from the station's centre in its frame, nose along -Z.
	entt::entity Ship(entt::entity station, std::string_view hull, const glm::dvec3 &offset)
	{
		entt::registry &registry = world.Registry();
		const entt::entity ship = registry.create();
		registry.emplace<Sim::Transform>(
		    ship, Sim::Transform{registry.get<Sim::Transform>(station).position + offset});
		registry.emplace<Sim::InSystem>(ship, kHalcyon);
		registry.emplace<Physics::LocalFrame>(ship, std::string{kHighport});
		Physics::Body body{Physics::BodyKind::Dynamic, Dock::Docks::HullShape(hull), 20000.0f};
		body.continuous = true;
		registry.emplace<Physics::Body>(ship, body);
		registry.emplace<Physics::Velocity>(ship);
		registry.emplace<Physics::Thrusters>(ship);
		registry.emplace<Physics::FlightControls>(ship);
		registry.emplace<Dock::Hull>(ship, std::string{hull});
		return ship;
	}

	void Step()
	{
		world.Step();
		for (const Physics::ContactEvent &contact : physics.Contacts())
			if (contact.phase == Physics::ContactPhase::Began && !contact.sensor)
				collisions.push_back(contact);
		for (Sim::SimEvent &event : world.Events().Take())
			events.push_back(std::move(event));
	}

	void Run(double seconds)
	{
		for (long step = 0; step < std::lround(seconds / kStep); ++step)
			Step();
	}

	template <typename T> std::vector<T> Events() const
	{
		std::vector<T> found;
		for (const Sim::SimEvent &event : events)
			if (const T *value = event.event.try_cast<T>())
				found.push_back(*value);
		return found;
	}

	const Dock::Pad &PadOf(entt::entity pad) const
	{
		return world.Registry().get<Dock::Pad>(pad);
	}
	entt::entity PadNumbered(entt::entity station, std::uint32_t number) const
	{
		return world.Registry().get<Dock::Station>(station).pads.at(number - 1);
	}
	// The number of the pad the ship holds or is on its way to, or none.
	std::uint32_t NumberOf(entt::entity ship) const
	{
		const auto *docking = world.Registry().try_get<Dock::Docking>(ship);
		return docking && docking->pad != entt::null ? PadOf(docking->pad).number : 0;
	}
};
} // namespace Jettison::DockTests
