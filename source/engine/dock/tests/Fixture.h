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
#include <jettison/space/StarSystemFrame.h>
#include <jettison/spatial/FrameIndices.h>
#include <jettison/vfs/FileSystem.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// What the dock suites share: a world with physics, the spatial index, steering and the docks, in
// the retired space sample's Halcyon, read from its file, and its vehicles and platforms from theirs
// (tests/fixtures).
namespace Jettison::DockTests
{
inline constexpr double kStep = 1.0 / 60.0;
inline const std::uint64_t kHalcyon = Sim::FrameId("Halcyon");
inline constexpr std::string_view kHighport = "Ostra Highport";

inline std::string ReadAsset(std::string_view path)
{
	std::ifstream file{std::string{JETTISON_DOCK_FIXTURE_DIR} + "/" + std::string{path}};
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

// The space sample's berth classes (defs/berths), as a suite that defines its own vehicles has them.
inline const Dock::BerthClasses &SampleClasses()
{
	static const Dock::BerthClasses classes{
	    {"corvette", {0, {25.0f, 20.0f, 8.0f}, {35.0f, 40.0f}}},
	    {"frigate", {1, {60.0f, 30.0f, 15.0f}, {50.0f, 90.0f}}},
	    {"destroyer", {2, {140.0f, 50.0f, 30.0f}, {80.0f, 180.0f}}},
	    {"cruiser", {3, {300.0f, 80.0f, 60.0f}, {120.0f, 360.0f}}},
	};
	return classes;
}

struct Fixture
{
	Sim::SimWorld world{kStep, 9};
	Physics::PhysicsWorld &physics{Setup(world)};
	Spatial::FrameIndices &indices{Spatial::FrameIndices::Attach(world)};
	Ai::Steering &steering{Ai::Steering::Attach(world, physics, indices)};
	Dock::Docks &docks{Dock::Docks::Attach(world, physics)};
	std::vector<Physics::ContactEvent> collisions;
	std::vector<Sim::SimEvent> events;

	// With the sample's definitions loaded, or with none, for a suite that defines its own.
	explicit Fixture(bool sample = true)
	{
		Space::DefineStarSystem(physics, Celestial::ReadStarSystem(ReadAsset("systems/halcyon.yaml")));
		if (!sample)
			return;
		Vfs::FileSystem files;
		files.AddMount({"base", JETTISON_DOCK_FIXTURE_DIR, false});
		Sim::DefinitionRegistry definitions{files};
		definitions.Load<Dock::BerthClassDefinition>(
		    Dock::kBerthClassDirectory, {Dock::kDefinitionVersion});
		definitions.Load<Dock::VehicleDefinition>(Dock::kVehicleDirectory, Dock::VehicleMigrations());
		definitions.Load<Dock::PlatformLayout>(Dock::kPlatformDirectory, {Dock::kDefinitionVersion});
		docks.Define(definitions);
	}

	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InFrame>();
		world.RegisterComponent<Sim::ControlledBy>();
		return Physics::PhysicsWorld::Attach(world);
	}

	// A vehicle of the definition at rest, that far from the platform's centre in its frame, nose
	// along -Z.
	entt::entity Vehicle(entt::entity platform, std::string_view definition, const glm::dvec3 &offset)
	{
		entt::registry &registry = world.Registry();
		const entt::entity vehicle = registry.create();
		registry.emplace<Sim::Transform>(
		    vehicle, Sim::Transform{registry.get<Sim::Transform>(platform).position + offset});
		registry.emplace<Sim::InFrame>(vehicle, kHalcyon);
		registry.emplace<Physics::LocalFrame>(vehicle, std::string{kHighport});
		Physics::Body body{Physics::BodyKind::Dynamic, Dock::Docks::VehicleShape(definition), 20000.0f};
		body.continuous = true;
		registry.emplace<Physics::Body>(vehicle, body);
		registry.emplace<Physics::Velocity>(vehicle);
		registry.emplace<Physics::Handling>(vehicle);
		registry.emplace<Physics::FlightControls>(vehicle);
		registry.emplace<Dock::Vehicle>(vehicle, std::string{definition});
		return vehicle;
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

	const Dock::Berth &BerthOf(entt::entity berth) const
	{
		return world.Registry().get<Dock::Berth>(berth);
	}

	entt::entity BerthNumbered(entt::entity platform, std::uint32_t number) const
	{
		return world.Registry().get<Dock::Platform>(platform).berths.at(number - 1);
	}

	// The number of the berth the vehicle holds or is on its way to, or none.
	std::uint32_t NumberOf(entt::entity vehicle) const
	{
		const auto *docking = world.Registry().try_get<Dock::Docking>(vehicle);
		return docking && docking->berth != entt::null ? BerthOf(docking->berth).number : 0;
	}
};
} // namespace Jettison::DockTests
