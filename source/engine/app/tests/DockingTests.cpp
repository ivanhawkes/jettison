#include "../src/Docking.h"
#include "../src/Trading.h"

#include <doctest/doctest.h>

#include <vector>

#include <jettison/audio/AudioSystem.h>
#include <jettison/audio/EventSounds.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison;
using App::Docked;
using App::DockingPort;
using App::ShipDocked;
using App::ShipUndocked;

namespace
{
constexpr double kStep = 1.0 / 60.0;
// A system no definition names: no gravity, no rails, so the station is still in its frame.
const std::uint64_t kSystem = Sim::SystemId("Dockyard");

struct Dockyard
{
	Dockyard() : physics{Setup(world)}
	{
		entt::registry &registry = world.Registry();
		station = registry.create();
		registry.emplace<Sim::Transform>(station);
		registry.emplace<Sim::InSystem>(station, kSystem);
		registry.emplace<Physics::Body>(station, Physics::Body{Physics::BodyKind::Static, "hull"});

		// The port before the hull's -Z end, at -10 m, out to -50 m.
		port = registry.create();
		registry.emplace<Sim::Transform>(port, Sim::Transform{{0.0, 0.0, -30.0}});
		registry.emplace<Sim::InSystem>(port, kSystem);
		Physics::Body sensor{Physics::BodyKind::Static, "port"};
		sensor.sensor = true;
		registry.emplace<Physics::Body>(port, sensor);
		registry.emplace<DockingPort>(port, station, 2.0f);

		ship = registry.create();
		registry.emplace<Sim::Transform>(ship, Sim::Transform{{0.0, 0.0, -80.0}});
		registry.emplace<Sim::InSystem>(ship, kSystem);
		registry.emplace<Physics::Body>(
		    ship, Physics::Body{Physics::BodyKind::Dynamic, "shuttle", 20000.0f});
		registry.emplace<Physics::Velocity>(ship);
		registry.emplace<Physics::Thrusters>(ship);
		registry.emplace<Physics::FlightControls>(ship);
	}

	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InSystem>();
		Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
		App::RegisterDocking(world, physics);
		Physics::ShapeDefinition box;
		box.kind = Physics::ShapeKind::Box;
		box.halfExtents = {6.0f, 6.0f, 10.0f};
		physics.DefineShape("hull", box);
		box.halfExtents = {20.0f, 20.0f, 20.0f};
		physics.DefineShape("port", box);
		box.halfExtents = {3.2f, 0.9f, 4.9f};
		physics.DefineShape("shuttle", box);
		return physics;
	}

	// Flies the ship with its assist, along +Z at that share of its top speed, for that long, and
	// keeps what the steps raised.
	void Fly(float throttle, int steps)
	{
		world.Commands().Push(
		    Physics::SetFlightControls{ship, {{0.0f, 0.0f, throttle}, {}, true, station}});
		for (int step = 0; step < steps; ++step)
		{
			world.Step();
			for (Sim::SimEvent &event : world.Events().Take())
				events.push_back(std::move(event));
		}
	}
	template <typename Event> std::vector<Event> Raised() const
	{
		std::vector<Event> raised;
		for (const Sim::SimEvent &event : events)
			if (const auto *found = event.event.try_cast<Event>())
				raised.push_back(*found);
		return raised;
	}
	double Z() const
	{
		return world.Registry().get<Sim::Transform>(ship).position.z;
	}

	Sim::SimWorld world{kStep, 3};
	Physics::PhysicsWorld &physics;
	entt::entity station{entt::null};
	entt::entity port{entt::null};
	entt::entity ship{entt::null};
	std::vector<Sim::SimEvent> events;
};
} // namespace

TEST_CASE("Docking: a ship slow enough in a station's port docks, and undocks as it leaves")
{
	Dockyard yard;
	entt::registry &registry = yard.world.Registry();

	// In at 15 m/s astern, a tenth of its top speed: in the port, too fast to dock. Its stern, 4.9 m
	// from its centre, crosses the port's edge at -50 m after two and a half seconds.
	yard.Fly(0.1f, 150);
	REQUIRE(yard.Z() + 4.9 > -50.0);
	CHECK(yard.physics.Touching(yard.port) == std::vector{yard.ship});
	CHECK_FALSE(registry.all_of<Docked>(yard.ship));
	CHECK(yard.Raised<ShipDocked>().empty());

	// Let go, the assist stops it inside the port, and it docks as it falls below 2 m/s.
	yard.Fly(0.0f, 120);
	REQUIRE(registry.all_of<Docked>(yard.ship));
	CHECK(registry.get<Docked>(yard.ship).port == yard.port);
	REQUIRE(yard.Raised<ShipDocked>().size() == 1);
	CHECK(yard.Raised<ShipDocked>().front().station == yard.station);
	CHECK(yard.Z() < -10.0);

	// Docked is state: a save holds it, and a world loaded from it is docked too.
	Dockyard loaded;
	loaded.world.Load(yard.world.Save());
	CHECK(loaded.world.Registry().get<Docked>(yard.ship).port == yard.port);

	// Backed out at 30 m/s, it leaves the port and undocks, once.
	yard.Fly(-0.2f, 240);
	CHECK(yard.Z() < -50.0 - 4.9);
	CHECK_FALSE(registry.all_of<Docked>(yard.ship));
	REQUIRE(yard.Raised<ShipUndocked>().size() == 1);
	CHECK(yard.Raised<ShipUndocked>().front().ship == yard.ship);
	CHECK(yard.Raised<ShipDocked>().size() == 1);

	// A ship docked at a port that is taken away is undocked. Put back in the port, at rest.
	registry.get<Sim::Transform>(yard.ship).position = {0.0, 0.0, -30.0};
	registry.get<Physics::Velocity>(yard.ship) = {};
	yard.Fly(0.0f, 2);
	REQUIRE(registry.all_of<Docked>(yard.ship));
	registry.destroy(yard.port);
	yard.Fly(0.0f, 1);
	CHECK_FALSE(registry.all_of<Docked>(yard.ship));
	CHECK(yard.Raised<ShipUndocked>().size() == 2);
}

TEST_CASE(
    "Docking: the shipped sounds play a docking and an undocking on the effects bus, from the station")
{
	Vfs::FileSystem files;
	files.AddMount({"base", JETTISON_ASSET_SOURCE_DIR, false});
	Audio::RegisterTypes();
	Sim::DefinitionRegistry definitions(files);
	definitions.Load<Audio::EventSound>(Audio::kEventSoundDirectory, {Audio::kEventSoundVersion});

	Dockyard yard;
	// The definitions name the trading events too, which registering trading on a world of its own
	// gives Reflect without a market in the yard.
	Sim::SimWorld traders{kStep, 3};
	App::RegisterTrading(traders);
	const Audio::EventSounds sounds(definitions);
	Audio::AudioSystem audio(files, {.output = Audio::Output::None});
	// The station is at the origin, and the listener 60 m to its +X looking down -Z: the station is
	// on the listener's left.
	audio.SetListener({60.0, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0});
	const Audio::PositionOf positionOf = [&](entt::entity entity) -> std::optional<glm::dvec3>
	{
		const auto *at = yard.world.Registry().try_get<Sim::Transform>(entity);
		return at ? std::optional{at->position} : std::nullopt;
	};
	const auto heard = [&]
	{
		std::vector<float> buffer(std::size_t{2} * 4800);
		float left = 0.0f;
		float right = 0.0f;
		for (int chunk = 0; chunk < 20; ++chunk)
		{
			audio.Mix(buffer);
			for (std::size_t i = 0; i < buffer.size(); i += 2)
			{
				left = std::max(left, std::abs(buffer[i]));
				right = std::max(right, std::abs(buffer[i + 1]));
			}
			audio.Update();
		}
		return std::pair{left, right};
	};
	const auto effects = [&]
	{ return audio.Counters().started[static_cast<std::size_t>(Audio::Bus::Effects)]; };

	yard.Fly(0.1f, 150);
	yard.Fly(0.0f, 120);
	for (const Sim::SimEvent &event : yard.events)
		sounds.Play(event, audio, positionOf);
	CHECK(effects() == 1);
	const auto [dockLeft, dockRight] = heard();
	CHECK(dockLeft > 0.1f);
	CHECK(dockLeft > 1.5f * dockRight);

	yard.events.clear();
	yard.Fly(-0.2f, 240);
	for (const Sim::SimEvent &event : yard.events)
		sounds.Play(event, audio, positionOf);
	CHECK(effects() == 2);
	const auto [undockLeft, undockRight] = heard();
	CHECK(undockLeft > 1.5f * undockRight);
	CHECK(audio.Problems() == 0);
}
