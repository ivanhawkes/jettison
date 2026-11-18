#include "SampleGalaxy.h"

#include "../src/Fleet.h"
#include "../src/Repair.h"
#include "../src/Traders.h"
#include "../src/Trading.h"
#include <jettison/app/Game.h>
#include <jettison/app/Stations.h>
#include <jettison/sample/SpaceGame.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <jettison/dock/Docks.h>
#include <jettison/input/Actions.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/script/ScriptHost.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Reason.h>

using namespace Jettison;

namespace
{
constexpr Sim::Controller kPlayer{Sim::ControllerKind::Player, 0};


// The sample as the shell composes it (Application's constructor, InitResources and InitScene):
// the shell's own systems attached in its order, and everything of the game's through
// Sample::SpaceGame's hooks, in the order the shell calls them. The other suites' Galaxy registers
// the sample's parts itself; this is the game's own registration, which a ship then runs on.
struct Composed
{
	Sim::SimWorld world{Testing::kStep, 11};
	Physics::PhysicsWorld &physics{Testing::Galaxy::Setup(world)};
	Space::LaneTravel &lanes{Space::LaneTravel::Attach(world, physics)};
	Space::Gates &gates{Space::Gates::Attach(world, physics, lanes)};
	Space::CruiseTravel &cruises{Space::CruiseTravel::Attach(world, physics)};
	Spatial::SystemIndices &indices{Spatial::SystemIndices::Attach(world)};
	Ai::Steering &steering{Ai::Steering::Attach(world, physics, indices)};
	Dock::Docks &docks{Dock::Docks::Attach(world, physics)};
	Ai::Behaviours &behaviours{Ai::Behaviours::Attach(world)};
	Vfs::FileSystem files;
	Input::InputSystem input;
	Sample::SpaceGame game;
	std::unique_ptr<Sim::DefinitionRegistry> pDefinitions;
	std::unique_ptr<Script::ScriptHost> pScripts;
	std::vector<Celestial::StarSystem> systems;
	std::vector<Sim::SimEvent> events;

	Composed()
	{
		const App::ShellWorld shell{world, physics, lanes, gates, docks, behaviours};
		world.RegisterComponent<Sim::ControlledBy>();
		input.DefineContext("shell");
		game.Register(shell, input);

		files.AddMount({"base", JETTISON_SAMPLE_ASSET_DIR, false});
		pDefinitions = std::make_unique<Sim::DefinitionRegistry>(files);
		game.LoadDefinitions(*pDefinitions);
		Script::LoadDefinitions(*pDefinitions);
		pScripts = std::make_unique<Script::ScriptHost>(files);
		pScripts->SendCommandsTo(world);
		pScripts->LoadAll(*pDefinitions);
		pDefinitions->Load<Dock::PadClassDefinition>(
		    Dock::kPadClassDirectory, {Dock::kDefinitionVersion});
		pDefinitions->Load<Dock::HullDefinition>(Dock::kHullDirectory, {Dock::kDefinitionVersion});
		pDefinitions->Load<Dock::StationLayout>(Dock::kStationDirectory, {Dock::kDefinitionVersion});
		docks.Define(*pDefinitions);
		game.Define(shell, *pDefinitions);

		Celestial::Galaxy galaxy = Celestial::ReadGalaxy(Testing::ReadAsset("systems/galaxy.yaml"));
		for (const Celestial::GalaxySystem &entry : galaxy.systems)
		{
			systems.push_back(Celestial::ReadStarSystem(Testing::ReadAsset(entry.system)));
			lanes.DefineStarSystem(systems.back());
			cruises.DefineStarSystem(systems.back());
			gates.DefineStarSystem(systems.back());
			Space::DefineStarSystem(physics, systems.back());
		}
		lanes.SetGalaxy(std::move(galaxy));
		game.OpenGalaxy(shell, systems, *pDefinitions, *pScripts);
		App::OpenStations(
		    world, docks, systems, [&](entt::entity station, std::string_view body)
		    { game.StationOpened(world, docks, station, body); });
		App::OpenGates(world, gates, systems);
		game.StationsOpened(shell, *pDefinitions);
	}


	void Step()
	{
		world.Step();
		for (Sim::SimEvent &event : world.Events().Take())
			events.push_back(std::move(event));
	}


	template <typename T> std::vector<T> Events() const
	{
		std::vector<T> found;
		for (const Sim::SimEvent &event : events)
			if (const T *value = event.event.try_cast<T>())
				found.push_back(*value);
		return found;
	}


	entt::entity Highport() const
	{
		return App::FindStation(world.Registry(), Sim::FrameId("Halcyon"), "Ostra Highport");
	}


	// A shuttle of the player's parked on that pad of Highport, with those credits.
	entt::entity ShipOn(std::uint32_t pad, std::int64_t credits)
	{
		entt::registry &registry = world.Registry();
		const entt::entity station = Highport();
		const entt::entity ship = registry.create();
		registry.emplace<Sim::Transform>(ship, registry.get<Sim::Transform>(station));
		registry.emplace<Sim::InFrame>(ship, Sim::FrameId("Halcyon"));
		registry.emplace<Physics::LocalFrame>(ship, std::string{"Ostra Highport"});
		App::MakeShip(registry, docks, ship, "shuttle", 2.0e4f, true);
		registry.emplace<Sim::ControlledBy>(ship, kPlayer);
		if (Sample::FindOwner(registry, kPlayer) == entt::null)
			Sample::MakeOwner(registry, kPlayer, credits);
		docks.Park(world, ship, registry.get<Dock::Station>(station).pads.at(pad - 1));
		return ship;
	}
};
} // namespace


TEST_CASE("Game: the sample registered through its own hooks has traders that run a route and sell")
{
	Composed sample;
	const entt::registry &registry = sample.world.Registry();
	// Markets and traders are the game's to open (StationsOpened), from what it registered
	// (Register, Define) and the systems it was given (OpenGalaxy): with no systems, no trader finds
	// a route.
	REQUIRE(registry.view<Sample::Market>().size() == 3);
	std::vector<entt::entity> traders;
	for (const auto [ship, trader] : registry.view<const Sample::Trader>().each())
		traders.push_back(ship);
	REQUIRE(!traders.empty());

	// A trader buys where it starts, flies to another station and sells there: half an hour is
	// several runs' worth.
	const auto sold = [&](const Sample::Traded &traded)
	{ return traded.crates < 0 && std::ranges::find(traders, traded.ship) != traders.end(); };
	std::size_t seen = 0;
	bool sale = false;
	for (int step = 0; step < 60 * 60 * 30 && !sale; ++step)
	{
		sample.Step();
		for (; seen < sample.events.size() && !sale; ++seen)
			if (const auto *traded = sample.events[seen].event.try_cast<Sample::Traded>())
				sale = sold(*traded);
	}
	CHECK(sale);
}


TEST_CASE("Repair: hard contacts damage a hull by their speed, and soft ones and sensors do not")
{
	Composed sample;
	entt::registry &registry = sample.world.Registry();
	const entt::entity ship = sample.ShipOn(1, 1'000'000);
	const entt::entity rock = registry.create();
	using Physics::ContactPhase;
	const std::vector<Physics::ContactEvent> contacts{
	    {ContactPhase::Began, ship, rock, false, {}, {}, 12.9f},
	    {ContactPhase::Began, rock, ship, false, {}, {}, 7.0f},
	    {ContactPhase::Began, ship, rock, false, {}, {}, 4.9f},
	    {ContactPhase::Began, ship, rock, true, {}, {}, 50.0f},
	    {ContactPhase::Ended, ship, rock, false, {}, {}, 50.0f},
	};
	Sample::TakeDamage(registry, contacts);
	// 12 and 7: whole metres a second of the two that began hard. The rock has no hull.
	CHECK(registry.get<Sample::Damage>(ship).points == 19);
	CHECK_FALSE(registry.all_of<Sample::Damage>(rock));

	// Parked ships ride their pads through the steps, and take none.
	for (int step = 0; step < 120; ++step)
		sample.Step();
	CHECK(registry.get<Sample::Damage>(ship).points == 19);
	CHECK(registry.view<Sample::Damage>().size() == 1);
}


TEST_CASE(
    "Repair: a damaged ship is repaired on a dry-dock pad for its owner's credits, and only there")
{
	Composed sample;
	entt::registry &registry = sample.world.Registry();
	// Pad 12 of Highport offers a dry dock, and pad 1 does not (defs/stations/ostra-highport.yaml).
	const entt::entity station = sample.Highport();
	const std::vector<entt::entity> pads = registry.get<Dock::Station>(station).pads;
	REQUIRE(sample.docks.Offers(registry, pads[11], Sample::kDryDock));
	REQUIRE_FALSE(sample.docks.Offers(registry, pads[0], Sample::kDryDock));
	const entt::entity plain = sample.ShipOn(1, 5'000);
	const entt::entity docked = sample.ShipOn(12, 5'000);
	const auto refusal = [&]
	{
		const std::vector<Sample::RepairRefused> refused = sample.Events<Sample::RepairRefused>();
		return refused.empty() ? std::string{} : refused.back().reason.key;
	};
	const auto ask = [&](entt::entity ship, const Sim::Controller &sender = kPlayer)
	{
		sample.world.Commands().Push(Sample::Repair{sender, ship});
		sample.Step();
	};

	ask(docked);
	CHECK(refusal() == "reason.shell.not_damaged");
	registry.emplace<Sample::Damage>(plain, 30);
	registry.emplace<Sample::Damage>(docked, 30);
	ask(plain);
	CHECK(refusal() == "reason.shell.not_in_dry_dock");
	ask(docked, Sim::Controller{Sim::ControllerKind::Player, 7});
	CHECK(refusal() == "reason.shell.not_owner");
	CHECK(sample.Events<Sample::Repaired>().empty());

	// Thirty points at a hundred credits each, from the owner's five thousand.
	ask(docked);
	REQUIRE(sample.Events<Sample::Repaired>().size() == 1);
	const Sample::Repaired repaired = sample.Events<Sample::Repaired>()[0];
	CHECK(repaired.ship == docked);
	CHECK(repaired.points == 30);
	CHECK(repaired.cost == 3'000);
	CHECK(repaired.credits == 2'000);
	CHECK_FALSE(registry.all_of<Sample::Damage>(docked));
	CHECK(Sample::PurseOf(registry, docked)->credits == 2'000);

	// What is left does not pay for thirty more, and the damage stays.
	registry.emplace<Sample::Damage>(docked, 30);
	ask(docked);
	CHECK(refusal() == "reason.shell.short_of_repair");
	CHECK(registry.get<Sample::Damage>(docked).points == 30);
	CHECK(Sample::PurseOf(registry, docked)->credits == 2'000);

	// Damage is state: a save holds it.
	Composed loaded;
	loaded.world.Load(sample.world.Save());
	CHECK(loaded.world.Registry().get<Sample::Damage>(docked).points == 30);
	CHECK(loaded.world.StateHash() == sample.world.StateHash());
}


TEST_CASE("Repair: a ship asking Highport for a dry dock is given one of the two pads that offer it")
{
	Composed sample;
	entt::registry &registry = sample.world.Registry();
	const entt::entity station = sample.Highport();
	const entt::entity ship = sample.ShipOn(1, 5'000);
	sample.world.Commands().Push(Dock::ReleasePad{kPlayer, ship, false});
	for (int step = 0; step < 60 * 60 && registry.all_of<Dock::Docking>(ship); ++step)
		sample.Step();
	REQUIRE_FALSE(registry.all_of<Dock::Docking>(ship));
	sample.world.Commands().Push(
	    Dock::RequestPad{kPlayer, ship, station, false, std::string{Sample::kDryDock}});
	sample.Step();
	const auto *docking = registry.try_get<Dock::Docking>(ship);
	REQUIRE(docking);
	REQUIRE(docking->pad != entt::null);
	// Pad 1, which it left, is the lowest free pad a shuttle fits; it is given a dry dock instead.
	CHECK(registry.get<Dock::Pad>(docking->pad).number >= 11);
	CHECK(sample.docks.Offers(registry, docking->pad, Sample::kDryDock));
}
