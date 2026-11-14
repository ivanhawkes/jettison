#pragma once

#include "../src/Fleet.h"
#include "../src/Mounts.h"
#include "../src/Stations.h"
#include "../src/Traders.h"
#include "../src/Trading.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Cruise.h>
#include <jettison/ai/Gates.h>
#include <jettison/ai/Lanes.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/UtilityAi.h>
#include <jettison/celestial/Galaxy.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/script/ScriptHost.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/spatial/SystemIndices.h>
#include <jettison/vfs/FileSystem.h>

// The sample galaxy the traders' and the fleet's tests run in.
namespace Jettison::Testing
{
inline constexpr double kStep = 1.0 / 60.0;

inline std::string ReadAsset(const std::string &path)
{
	std::ifstream in{std::string{JETTISON_ASSET_SOURCE_DIR} + "/" + path};
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

// The sample galaxy as the shell runs it: physics, the lanes, the spatial index, steering, the
// docks, the markets and the behaviour trees, given the galaxy, its systems and every definition
// the sample ships, with its stations, markets and traders open, and its ships for sale. Given a user's
// data directory, its mods are mounted as the shell mounts them and their scripts loaded, given every
// event and answering the traders' inputs.
struct Galaxy
{
	Sim::SimWorld world{kStep, 11};
	Physics::PhysicsWorld &physics{Setup(world)};
	Ai::LaneTravel &lanes{Ai::LaneTravel::Attach(world, physics)};
	Ai::CruiseTravel &cruises{Ai::CruiseTravel::Attach(world, physics)};
	Ai::Gates &gates{Ai::Gates::Attach(world, physics, lanes)};
	Spatial::SystemIndices &indices{Spatial::SystemIndices::Attach(world)};
	Ai::Steering &steering{Ai::Steering::Attach(world, physics, indices)};
	Dock::Docks &docks{Dock::Docks::Attach(world, physics)};
	Ai::Behaviours &behaviours{Ai::Behaviours::Attach(world)};
	Vfs::FileSystem files;
	std::unique_ptr<Script::ScriptHost> pScripts;
	std::vector<Celestial::StarSystem> systems;
	std::vector<entt::entity> traders;
	std::vector<Physics::ContactEvent> contacts;
	std::vector<Sim::SimEvent> events;

	explicit Galaxy(bool open = true, const std::filesystem::path &userData = {})
	{
		world.RegisterComponent<Sim::ControlledBy>();
		App::RegisterTrading(world);
		if (userData.empty())
			files.AddMount({"base", JETTISON_ASSET_SOURCE_DIR, false});
		else
			App::MountAssets(files, JETTISON_ASSET_SOURCE_DIR, userData);
		Sim::DefinitionRegistry definitions{files};
		App::LoadTradeDefinitions(definitions);
		App::LoadTraderDefinitions(definitions);
		if (!userData.empty())
		{
			Script::LoadDefinitions(definitions);
			pScripts = std::make_unique<Script::ScriptHost>(files);
			pScripts->SendCommandsTo(world);
			pScripts->LoadAll(definitions);
		}
		definitions.Load<Dock::HullDefinition>(Dock::kHullDirectory, {Dock::kDefinitionVersion});
		definitions.Load<Dock::StationLayout>(Dock::kStationDirectory, {Dock::kDefinitionVersion});
		docks.Define(definitions);
		App::LoadShipyard(definitions);
		App::RegisterFleet(world, docks, definitions);

		Celestial::Galaxy galaxy = Celestial::ReadGalaxy(ReadAsset("systems/galaxy.yaml"));
		for (const Celestial::GalaxySystem &entry : galaxy.systems)
		{
			systems.push_back(Celestial::ReadStarSystem(ReadAsset(entry.system)));
			lanes.DefineStarSystem(systems.back());
			cruises.DefineStarSystem(systems.back());
			gates.DefineStarSystem(systems.back());
			physics.DefineStarSystem(systems.back());
		}
		lanes.SetGalaxy(std::move(galaxy));
		App::ChargeTolls(gates);
		App::RegisterTraders(
		    world, behaviours, physics, lanes, systems,
		    *definitions.Find<Ai::DecisionDefinition>(App::kTradeDecision), pScripts.get());
		behaviours.Define(definitions);
		if (!open)
			return;

		App::OpenStations(world, docks, systems);
		App::OpenGates(world, gates, systems);
		for (const entt::entity entity : App::OpenMarkets(world, definitions))
		{
			App::Market &market = world.Registry().get<App::Market>(entity);
			market.stationEntity = App::FindStation(world.Registry(), market.system, market.station);
		}
		traders = App::OpenTraders(world, docks);
	}

	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InSystem>();
		return Physics::PhysicsWorld::Attach(world);
	}

	void Step()
	{
		world.Step();
		for (const Physics::ContactEvent &contact : physics.Contacts())
			if (contact.phase == Physics::ContactPhase::Began && !contact.sensor)
				contacts.push_back(contact);
		for (Sim::SimEvent &event : world.Events().Take())
		{
			if (pScripts)
				pScripts->Dispatch(event);
			events.push_back(std::move(event));
		}
	}

	template <typename T> std::vector<T> Events() const
	{
		std::vector<T> found;
		for (const Sim::SimEvent &event : events)
			if (const T *value = event.event.try_cast<T>())
				found.push_back(*value);
		return found;
	}

	const App::Trader &TraderOf(entt::entity ship) const
	{
		return world.Registry().get<App::Trader>(ship);
	}

	entt::entity MarketAt(std::string_view station) const
	{
		for (const auto [entity, market] : world.Registry().view<const App::Market>().each())
			if (market.station == station)
				return entity;
		return entt::null;
	}
};
} // namespace Jettison::Testing
