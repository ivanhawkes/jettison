#include <jettison/app/Stations.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/ai/Steering.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/scene/Components.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/spatial/SystemIndices.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;


Celestial::StarSystem System(const std::string &file)
{
	std::ifstream in{std::string{JETTISON_SAMPLE_ASSET_DIR} + "/systems/" + file};
	std::stringstream text;
	text << in.rdbuf();
	return Celestial::ReadStarSystem(text.str());
}

// The two systems with stations the assets ship, defined in physics, and the docks given the hulls
// and layouts, as the shell has them.
struct Sample
{
	Sim::SimWorld world{kStep, 5};
	Physics::PhysicsWorld &physics{Setup(world)};
	Spatial::SystemIndices &indices{Spatial::SystemIndices::Attach(world)};
	Ai::Steering &steering{Ai::Steering::Attach(world, physics, indices)};
	Dock::Docks &docks{Dock::Docks::Attach(world, physics)};
	std::vector<Celestial::StarSystem> systems{System("halcyon.yaml"), System("vesper.yaml")};
	Vfs::FileSystem files;


	Sample()
	{
		for (const Celestial::StarSystem &system : systems)
			physics.DefineStarSystem(system);
		files.AddMount({"base", JETTISON_SAMPLE_ASSET_DIR, false});
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
		return Physics::PhysicsWorld::Attach(world);
	}
};


std::size_t Holding(const entt::registry &world, entt::entity station)
{
	return static_cast<std::size_t>(std::ranges::count_if(
	    world.get<Dock::Station>(station).pads,
	    [&](entt::entity pad) { return world.get<Dock::Pad>(pad).state == Dock::PadState::Holding; }));
}
} // namespace


TEST_CASE("Stations: every station built to a layout opens with its pads, and the shell parks nothing")
{
	Sample sample;
	std::vector<std::string> opened;
	const std::vector<entt::entity> stations = App::OpenStations(
	    sample.world, sample.docks, sample.systems,
	    [&](entt::entity station, std::string_view body)
	    {
		    CHECK(sample.world.Registry().all_of<Dock::Station>(station));
		    opened.emplace_back(body);
	    });
	REQUIRE(stations.size() == 3);
	// Each told to the game as it is built, in the systems' order.
	CHECK(opened == std::vector<std::string>{"Ostra Highport", "Ostra Anchorage", "Corva Refinery"});
	const entt::registry &world = sample.world.Registry();
	const entt::entity highport = App::FindStation(world, Sim::SystemId("Halcyon"), "Ostra Highport");
	const entt::entity corva = App::FindStation(world, Sim::SystemId("Vesper"), "Corva Refinery");
	const entt::entity anchorage = App::FindStation(world, Sim::SystemId("Halcyon"), "Ostra Anchorage");
	CHECK(highport == stations[0]);
	CHECK(anchorage == stations[1]);
	CHECK(corva == stations[2]);
	CHECK(App::FindStation(world, Sim::SystemId("Vesper"), "Ostra Highport") == entt::null);

	// Highport's twelve L pads and its display wing's six; six at Corva and at the Anchorage. No
	// ship is parked on any: that is a game's to do.
	CHECK(world.get<Dock::Station>(highport).pads.size() == 18);
	CHECK(world.get<Dock::Station>(corva).pads.size() == 6);
	CHECK(world.get<Dock::Station>(anchorage).pads.size() == 6);
	for (const entt::entity station : stations)
		CHECK(Holding(world, station) == 0);
	CHECK(world.view<const Dock::Hull>().empty());
}
