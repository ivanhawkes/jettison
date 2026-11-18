#include "../src/Repair.h"
#include "SampleGalaxy.h"
#include "ShellState.h"

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <jettison/celestial/Galaxy.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/Snapshot.h>

using namespace Jettison;


TEST_CASE("Shell state: the layer's state reads back, and a bad section is refused")
{
	const Sample::ShellState state{entt::entity{3}, entt::entity{1}, 0x1234, "Highport"};
	const Sample::ShellState read = Sample::ReadShellState(Sample::WriteShellState(state));
	CHECK(read.ship == state.ship);
	CHECK(read.station == state.station);
	CHECK(read.shipSystem == 0x1234);
	CHECK(read.stationBody == "Highport");
	CHECK_THROWS_AS(Sample::ReadShellState({}), Serialise::FormatError);
}


TEST_CASE("Shell state: the systems a world's entities are in that its galaxy does not have are named")
{
	Sim::RegisterTypes();
	Celestial::Galaxy galaxy;
	galaxy.systems = {{"Halcyon", "systems/halcyon.yaml", {}}, {"Vesper", "systems/vesper.yaml", {}}};
	entt::registry world;
	world.emplace<Sim::InFrame>(world.create(), Sim::FrameId("Halcyon"));
	world.emplace<Sim::InFrame>(world.create(), Sim::kNoFrame);
	CHECK(Sample::UnknownSystems(world, galaxy).empty());
	world.emplace<Sim::InFrame>(world.create(), Sim::FrameId("Gone"));
	world.emplace<Sim::InFrame>(world.create(), Sim::FrameId("Gone"));
	CHECK(Sample::UnknownSystems(world, galaxy) == std::vector<std::uint64_t>{Sim::FrameId("Gone")});
}


TEST_CASE(
    "Shell state: a save the shell wrote before the sample took its space code loads in the sample")
{
	// Written by render_test at the commit before G01b, in the orbit scene as it opens: its section
	// is the shell's, its details the galaxy and the system, and its world the shell's and the game's.
	std::ifstream in{
	    std::string{JETTISON_SAMPLE_FIXTURE_DIR} + "/shell-before-g01.save", std::ios::binary};
	REQUIRE(in);
	const std::vector<char> text{std::istreambuf_iterator<char>{in}, {}};
	std::vector<std::byte> bytes(text.size());
	std::ranges::transform(text, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
	const Sim::SaveGame save = Sim::SaveGame::Read(bytes);
	CHECK(save.info.Detail(Sample::kGalaxyDetail) == "systems/galaxy.yaml");
	CHECK(save.info.Detail(Sample::kSystemDetail) == "Halcyon");

	const Sim::SaveSection *section = save.FindSection(Sample::kShellSection);
	REQUIRE(section != nullptr);
	const Sample::ShellState state = Sample::ReadShellState(section->data);
	CHECK(state.ship != entt::null);
	CHECK(state.station != entt::null);
	CHECK(state.shipSystem == Sim::FrameId("Halcyon"));
	CHECK(state.stationBody == "Ostra Highport");

	// The world loads into the sample's, its ship and station among it.
	Testing::Galaxy galaxy{false};
	Sample::RegisterRepair(galaxy.world, galaxy.physics, galaxy.docks);
	galaxy.world.Load(save.world);
	const entt::registry &world = galaxy.world.Registry();
	CHECK(world.valid(state.ship));
	CHECK(world.valid(state.station));
	CHECK(world.all_of<Dock::Hull>(state.ship));
}
