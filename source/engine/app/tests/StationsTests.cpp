#include "../src/Stations.h"
#include "../src/Trading.h"

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <jettison/ai/Steering.h>
#include <jettison/audio/AudioSystem.h>
#include <jettison/audio/EventSounds.h>
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
	std::ifstream in{std::string{JETTISON_ASSET_SOURCE_DIR} + "/systems/" + file};
	std::stringstream text;
	text << in.rdbuf();
	return Celestial::ReadStarSystem(text.str());
}

// The sample's two systems with stations, defined in physics, and the docks given the sample's hulls
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

TEST_CASE("Stations: every station built to a layout opens with its pads, ships parked on a few")
{
	Sample sample;
	const std::vector<entt::entity> stations =
	    App::OpenStations(sample.world, sample.docks, sample.systems);
	REQUIRE(stations.size() == 2);
	const entt::registry &world = sample.world.Registry();
	const entt::entity highport = App::FindStation(world, Sim::SystemId("Halcyon"), "Ostra Highport");
	const entt::entity corva = App::FindStation(world, Sim::SystemId("Vesper"), "Corva Refinery");
	CHECK(highport == stations[0]);
	CHECK(corva == stations[1]);
	CHECK(App::FindStation(world, Sim::SystemId("Vesper"), "Ostra Highport") == entt::null);

	// Three heavy transports on Highport's twelve L pads, and its display wing's six; two transports
	// and a fighter at Corva's six. At least two pads of each are left free.
	CHECK(world.get<Dock::Station>(highport).pads.size() == 18);
	CHECK(Holding(world, highport) == 3);
	CHECK(world.get<Dock::Station>(corva).pads.size() == 6);
	CHECK(Holding(world, corva) == 3);
	for (const auto [ship, docking, hull] : world.view<const Dock::Docking, const Dock::Hull>().each())
	{
		CHECK(docking.stage == Dock::Stage::Docked);
		CHECK(world.get<Physics::Body>(ship).kind == Physics::BodyKind::Kinematic);
		CHECK(world.get<Physics::Body>(ship).shape == Dock::Docks::HullShape(hull.definition));
	}

	// They stay where they are parked as the stations go round their orbits, touching nothing.
	for (int step = 0; step < 120; ++step)
		sample.world.Step();
	CHECK(sample.physics.Contacts().empty());
	CHECK(Holding(world, highport) == 3);
}

TEST_CASE(
    "Stations: the shipped sounds play a pad's clamps closing and letting go on the effects bus, from "
    "the pad")
{
	Vfs::FileSystem files;
	files.AddMount({"base", JETTISON_ASSET_SOURCE_DIR, false});
	Audio::RegisterTypes();
	Dock::RegisterTypes();
	// The definitions name the trading events too, which registering trading on a world of its own
	// gives Reflect.
	Sim::SimWorld traders{kStep, 3};
	App::RegisterTrading(traders);
	Sim::DefinitionRegistry definitions(files);
	definitions.Load<Audio::EventSound>(Audio::kEventSoundDirectory, {Audio::kEventSoundVersion});
	const Audio::EventSounds sounds(definitions);
	Audio::AudioSystem audio(files, {.output = Audio::Output::None});

	// The pad is at the origin, and the listener 60 m to its +X looking down -Z: the pad is on the
	// listener's left.
	const glm::dvec3 pad{0.0};
	audio.SetListener({60.0, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0});
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

	CHECK(sounds.PlayClipEvent("clamp.closed", pad, audio) == 1);
	CHECK(effects() == 1);
	const auto [closeLeft, closeRight] = heard();
	CHECK(closeLeft > 0.1f);
	CHECK(closeLeft > 1.5f * closeRight);

	CHECK(sounds.PlayClipEvent("clamp.released", pad, audio) == 1);
	CHECK(effects() == 2);
	const auto [releaseLeft, releaseRight] = heard();
	CHECK(releaseLeft > 1.5f * releaseRight);

	// The docks' own events are the clamps' to sound now, which close as a ship is clamped and open
	// as it is let go; the events themselves play nothing.
	const entt::entity ship{1};
	const entt::entity station{2};
	CHECK(sounds.Play({1, entt::meta_any{Dock::Landed{ship, station, entt::null, 1}}}, audio, {}) == 0);
	CHECK(sounds.Play({2, entt::meta_any{Dock::Left{ship, station, entt::null, 1}}}, audio, {}) == 0);
	CHECK(audio.Problems() == 0);
}
