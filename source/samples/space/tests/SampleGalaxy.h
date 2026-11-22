#pragma once

#include "../src/Fleet.h"
#include "../src/Stations.h"
#include "../src/Traders.h"
#include "../src/Trading.h"
#include <jettison/app/Mounts.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/UtilityAi.h>
#include <jettison/celestial/Galaxy.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sample/SpaceGame.h>
#include <jettison/script/ScriptHost.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/space/Cruise.h>
#include <jettison/space/Gates.h>
#include <jettison/space/Lanes.h>
#include <jettison/space/StarSystemFrame.h>
#include <jettison/spatial/FrameIndices.h>
#include <jettison/vfs/FileSystem.h>

// The sample galaxy the traders' and the fleet's tests run in.
namespace Jettison::Testing
{
inline constexpr double kStep = 1.0 / 60.0;

inline std::string ReadAsset(const std::string &path)
{
	std::ifstream in{std::string{JETTISON_SAMPLE_ASSET_DIR} + "/" + path};
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
	Space::LaneTravel &lanes{Space::LaneTravel::Attach(world, physics)};
	Space::CruiseTravel &cruises{Space::CruiseTravel::Attach(world, physics)};
	Space::Gates &gates{Space::Gates::Attach(world, physics, lanes)};
	Spatial::FrameIndices &indices{Spatial::FrameIndices::Attach(world)};
	Ai::Steering &steering{Ai::Steering::Attach(world, physics, indices)};
	Dock::Docks &docks{Dock::Docks::Attach(world, physics)};
	Ai::Behaviours &behaviours{Ai::Behaviours::Attach(world)};
	Vfs::FileSystem files;
	std::unique_ptr<Script::ScriptHost> pScripts;
	std::vector<Celestial::StarSystem> systems;
	std::vector<entt::entity> traders;
	Sample::Drives drives;
	std::vector<Physics::ContactEvent> contacts;
	std::vector<Sim::SimEvent> events;

	explicit Galaxy(bool open = true, const std::filesystem::path &userData = {})
	{
		world.RegisterComponent<Sim::ControlledBy>();
		Sample::RegisterTrading(world);
		if (userData.empty())
			files.AddMount({"base", JETTISON_SAMPLE_ASSET_DIR, false});
		else
			App::MountAssets(files, JETTISON_ASSET_SOURCE_DIR, JETTISON_SAMPLE_ASSET_DIR, userData);
		Sim::DefinitionRegistry definitions{files};
		Sample::LoadTradeDefinitions(definitions);
		Sample::LoadTraderDefinitions(definitions);
		if (!userData.empty())
		{
			Script::LoadDefinitions(definitions);
			pScripts = std::make_unique<Script::ScriptHost>(files);
			pScripts->SendCommandsTo(world);
			pScripts->LoadAll(definitions);
		}
		Sample::LoadStationDefinitions(definitions);
		docks.Define(definitions);
		drives = Sample::ReadDrives(definitions);
		Sample::LoadShipyard(definitions);
		Sample::RegisterFleet(world, docks, definitions);

		Celestial::Galaxy galaxy = Celestial::ReadGalaxy(ReadAsset("systems/galaxy.yaml"));
		for (const Celestial::GalaxySystem &entry : galaxy.systems)
		{
			systems.push_back(Celestial::ReadStarSystem(ReadAsset(entry.system)));
			lanes.DefineStarSystem(systems.back());
			cruises.DefineStarSystem(systems.back());
			gates.DefineStarSystem(systems.back());
			Space::DefineStarSystem(physics, systems.back());
		}
		lanes.SetGalaxy(std::move(galaxy));
		Sample::ChargeTolls(gates);
		Sample::RegisterTraders(
		    world, behaviours, physics, lanes, systems,
		    *definitions.Find<Ai::DecisionDefinition>(Sample::kTradeDecision), pScripts.get());
		behaviours.Define(definitions);
		if (!open)
			return;

		// The sample's ships parked as each station is built, as the shell has the game park them.
		Sample::SpaceGame game;
		Sample::OpenStations(
		    world, docks, systems, [&](entt::entity station, std::string_view body)
		    { game.StationOpened(world, docks, definitions, station, body); });
		Sample::OpenGates(world, gates, systems);
		for (const entt::entity entity : Sample::OpenMarkets(world, definitions))
		{
			Sample::Market &market = world.Registry().get<Sample::Market>(entity);
			market.stationEntity = Sample::FindStation(world.Registry(), market.system, market.station);
		}
		traders = Sample::OpenTraders(world, docks, drives);
	}

	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InFrame>();
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

	const Sample::Trader &TraderOf(entt::entity ship) const
	{
		return world.Registry().get<Sample::Trader>(ship);
	}

	entt::entity MarketAt(std::string_view station) const
	{
		for (const auto [entity, market] : world.Registry().view<const Sample::Market>().each())
			if (market.station == station)
				return entity;
		return entt::null;
	}
};
} // namespace Jettison::Testing
