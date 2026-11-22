#include "Fixture.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <jettison/serialise/Migration.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace Jettison;
using DockTests::Fixture;

namespace
{
// Ostra Highport built to its layout, and a shuttle 10 km below it, on the far side from its berths,
// which face up its +Y, and off to one side.
struct Approach : Fixture
{
	entt::entity platform{entt::null};
	entt::entity vehicle{entt::null};


	Approach()
	{
		platform = docks.MakePlatform(world, DockTests::kHarbour, DockTests::kFerry, "ostra-highport");
		const glm::dquat axes = world.Registry().get<Sim::Transform>(platform).orientation;
		vehicle = Vehicle(platform, "shuttle", axes * glm::dvec3{1500.0, -9800.0, 1200.0});
		world.Commands().Push(Dock::RequestBerth{{}, vehicle, platform, true});
	}

	// Where each contact the vehicle began was, in berth 1's axes from the middle of its face, as the
	// berth was when they touched.
	std::vector<glm::dvec3> touched;


	void Advance()
	{
		const std::size_t before = collisions.size();
		Step();
		const Sim::Transform &berth = world.Registry().get<Sim::Transform>(BerthNumbered(platform, 1));
		for (std::size_t index = before; index < collisions.size(); ++index)
			touched.push_back(
			    glm::conjugate(berth.orientation) * (collisions[index].point - berth.position));
	}


	// Whether every contact was the vehicle on its berth: on the berth's face, within its 80 by 180 m.
	bool OnlyOnTheBerth() const
	{
		return std::ranges::all_of(
		    touched, [](const glm::dvec3 &at)
		    { return std::abs(at.y) < 0.1 && std::abs(at.x) < 40.0 && std::abs(at.z) < 90.0; });
	}


	const Dock::Docking *Docking() const
	{
		return world.Registry().try_get<Dock::Docking>(vehicle);
	}


	// Steps until the vehicle's stage is the one asked for, or the time is up: the seconds it took.
	std::optional<double> RunUntil(Dock::Stage stage, double seconds)
	{
		for (long step = 0; step < std::lround(seconds / DockTests::kStep); ++step)
		{
			Advance();
			if (Docking() && Docking()->stage == stage)
				return static_cast<double>(step + 1) * DockTests::kStep;
		}
		return std::nullopt;
	}


	// Where the vehicle is in the berth's axes, from where its vehicle rests on it.
	glm::dvec3 FromRest(double height) const
	{
		const entt::registry &registry = world.Registry();
		const Sim::Transform &berth = registry.get<Sim::Transform>(BerthNumbered(platform, 1));
		return glm::conjugate(berth.orientation) *
		           (registry.get<Sim::Transform>(vehicle).position - berth.position) -
		       glm::dvec3{0.0, height * 0.5, 0.0};
	}
};

constexpr double kShuttleHeight = 1.8;
} // namespace


TEST_CASE(
    "Docks: a ship flown from 10 km lands on a pad on the far side of Highport, touching nothing first")
{
	Approach approach;
	approach.Step();
	REQUIRE(approach.Events<Dock::BerthGiven>().size() == 1);
	CHECK(approach.NumberOf(approach.vehicle) == 1);

	const std::optional<double> above = approach.RunUntil(Dock::Stage::Descending, 300.0);
	REQUIRE(above);
	// Round the platform's sphere, not through it: no contact on the way.
	CHECK(approach.collisions.empty());
	const std::optional<double> landed = approach.RunUntil(Dock::Stage::Docked, 120.0);
	REQUIRE(landed);
	MESSAGE("Over the pad after ", *above, " s, clamped ", *landed, " s later");
	CHECK(approach.collisions.empty());
	const std::vector<Dock::Landed> events = approach.Events<Dock::Landed>();
	REQUIRE(events.size() == 1);
	CHECK(events[0].number == 1);

	// Clamped where its vehicle rests, turned to the berth, kinematic on the platform's rails.
	entt::registry &registry = approach.world.Registry();
	CHECK(registry.get<Physics::Body>(approach.vehicle).kind == Physics::BodyKind::Kinematic);
	approach.Run(5.0);
	CHECK(glm::length(approach.FromRest(kShuttleHeight)) < 1.0e-3);
	const Sim::Transform &berth =
	    registry.get<Sim::Transform>(approach.BerthNumbered(approach.platform, 1));
	CHECK(
	    glm::dot(
	        registry.get<Sim::Transform>(approach.vehicle).orientation * glm::dvec3{0.0, 0.0, -1.0},
	        berth.orientation * glm::dvec3{0.0, 0.0, -1.0}) > 0.999999);
	CHECK(
	    approach.BerthOf(approach.BerthNumbered(approach.platform, 1)).state ==
	    Dock::BerthState::Holding);
	CHECK(approach.collisions.empty());
}


TEST_CASE("Docks: undocking lifts a ship clear of its pad, and gives the pad up once it is")
{
	Approach approach;
	REQUIRE(approach.RunUntil(Dock::Stage::Docked, 420.0));
	approach.events.clear();
	approach.world.Commands().Push(Dock::ReleaseBerth{{}, approach.vehicle});
	approach.Advance();
	REQUIRE(approach.Events<Dock::Left>().size() == 1);
	CHECK(approach.Docking()->stage == Dock::Stage::Leaving);
	// Held on its berth until it is clear: no one else is given it.
	CHECK(
	    approach.BerthOf(approach.BerthNumbered(approach.platform, 1)).state ==
	    Dock::BerthState::Holding);

	double nearest = std::numeric_limits<double>::infinity();
	for (int step = 0; step < 60 * 120 && approach.Docking(); ++step)
	{
		approach.Advance();
		nearest = std::min(nearest, approach.FromRest(kShuttleHeight).y);
	}
	REQUIRE_FALSE(approach.Docking());
	CHECK(approach.Events<Dock::BerthReleased>().size() == 1);
	CHECK(
	    approach.BerthOf(approach.BerthNumbered(approach.platform, 1)).state == Dock::BerthState::Free);
	CHECK(nearest > -0.01);
	// Straight up over the berth, outside the platform's sphere, and held there.
	const glm::dvec3 at = approach.FromRest(kShuttleHeight);
	CHECK(std::hypot(at.x, at.z) < 5.0);
	CHECK(
	    glm::distance(
	        approach.world.Registry().get<Sim::Transform>(approach.vehicle).position,
	        approach.world.Registry().get<Sim::Transform>(approach.platform).position) > 762.4);
	// It lay on the deck as it was let go, and touched nothing else.
	MESSAGE(approach.touched.size(), " contacts, as it was let go");
	CHECK(approach.OnlyOnTheBerth());
}


TEST_CASE(
    "Docks: a ship its pilot flies is lifted off its pad by its thrusters, and is the pilot's in a few "
    "seconds")
{
	Approach approach;
	REQUIRE(approach.RunUntil(Dock::Stage::Docked, 420.0));
	approach.events.clear();
	approach.world.Commands().Push(Dock::ReleaseBerth{{}, approach.vehicle, false});
	approach.Advance();
	REQUIRE(approach.Events<Dock::Left>().size() == 1);
	CHECK(approach.Docking()->stage == Dock::Stage::Leaving);
	entt::registry &registry = approach.world.Registry();
	// No point to fly to, only the thrusters firing up, against the platform.
	CHECK_FALSE(registry.all_of<Ai::Destination>(approach.vehicle));
	CHECK(registry.get<Physics::FlightControls>(approach.vehicle).linear.y > 0.0f);
	CHECK(
	    approach.BerthOf(approach.BerthNumbered(approach.platform, 1)).state ==
	    Dock::BerthState::Holding);

	int steps = 1;
	for (; steps < 60 * 30 && approach.Docking(); ++steps)
		approach.Advance();
	REQUIRE_FALSE(approach.Docking());
	MESSAGE("The pad given up ", steps / 60.0, " s after the clamps opened");
	CHECK(steps < 60 * 4);
	CHECK(approach.Events<Dock::BerthReleased>().size() == 1);
	CHECK(
	    approach.BerthOf(approach.BerthNumbered(approach.platform, 1)).state == Dock::BerthState::Free);
	// Straight up off the berth, the lift's height, held for the pilot, whose controls then fly it.
	const glm::dvec3 at = approach.FromRest(kShuttleHeight);
	CHECK(std::hypot(at.x, at.z) < 1.0);
	CHECK(at.y >= Dock::kLiftHeight);
	CHECK(at.y < Dock::kLiftHeight + 5.0);
	CHECK(registry.get<Physics::FlightControls>(approach.vehicle) == Physics::FlightControls{});
	approach.world.Commands().Push(
	    Physics::SetFlightControls{
	        {}, approach.vehicle, {{0.0f, 0.0f, -1.0f}, {}, true, approach.platform}});
	approach.Run(2.0);
	CHECK(registry.get<Physics::Velocity>(approach.vehicle).linear.z != 0.0f);
	CHECK(approach.OnlyOnTheBerth());
}


TEST_CASE(
    "Docks: the same approach from the same start, and from a save part-way, reaches the same state")
{
	constexpr int kSaveAt = 60 * 125;
	constexpr int kSteps = 60 * 145;
	const auto run = [](std::optional<std::vector<std::byte>> &saved, int saveAt)
	{
		Approach approach;
		for (int step = 0; step < kSteps; ++step)
		{
			if (step == saveAt)
			{
				REQUIRE(approach.Docking());
				CHECK(approach.Docking()->stage == Dock::Stage::Descending);
				saved = approach.world.Save();
			}
			approach.Step();
		}
		return std::pair{
		    approach.world.StateHash(),
		    approach.Docking() ? approach.Docking()->stage : Dock::Stage::Queued};
	};
	std::optional<std::vector<std::byte>> save;
	std::optional<std::vector<std::byte>> unused;
	const auto [first, stage] = run(save, kSaveAt);
	// Saved on the way down onto the berth, kept clear of the platform by its shape, and clamped by
	// the end.
	CHECK(stage == Dock::Stage::Docked);
	CHECK(run(unused, -1).first == first);
	// Recorded from a Debug build and checked by the Release suite and under the sanitisers too, as
	// steering's is: a change here is a change to how a vehicle is flown in, or to what a save holds.
	// Re-pinned in 9d for Highport's display wing, six more berths and a deck more in its shape, with
	// the approach's times unchanged (111 s to the point above the berth, 23.75 s down); and in 9e for
	// the empty storages of transfers and buggies a save now holds, 9d's hash without them; and in
	// W01 for a berth's class saved as its name, the old build's saves loaded and saved again the same;
	// and for the sample's classes under their vehicle names and the feature a docking asked for, each
	// by its bytes alone: the old names written back, and then the feature left unsaved, gave the
	// hash before each; and for the fast-forward, a kinematic body put at rest where it is while
	// nothing dynamic shares its world, the traders' galaxy the same to the bit over half an hour; and
	// for the way in stopped out along the berth's normal first, then flown straight down it (117 s to
	// the point above the berth, 23.6 s down); and in the space extraction's 5b for the platform on
	// the suite's own ferry rather than on the sample's orbit, the times the same; and in slice 7 for
	// the saved names alone: with the old names, the world's version 4 and the transfers' and buggies'
	// storages put back, it hashes as before.
	CHECK(first == 0x6fad9fdf840ea5d5ull);

	Approach loaded;
	loaded.world.Load(*save);
	for (int step = kSaveAt; step < kSteps; ++step)
		loaded.Step();
	CHECK(loaded.world.StateHash() == first);
}


TEST_CASE("Docks: a ship made again from a save of another Jolt docks as it would have, and stays")
{
	// A docked vehicle is a kinematic body on its platform's rails, and the clamps are presentation's,
	// so Jolt holds nothing of a docking that the records do not: the rebuild loses none of it.
	constexpr int kSaveAt = 60 * 120;
	const Physics::JoltVersion other{5, 5, 0, Physics::BuiltJoltVersion().features};
	Approach original;
	for (int step = 0; step < kSaveAt; ++step)
		original.Step();
	REQUIRE(original.Docking()->stage == Dock::Stage::Descending);
	original.physics.SaveAsJoltVersion(other);
	const std::vector<std::byte> descending = original.world.Save();

	Approach loaded;
	loaded.world.Load(descending);
	const std::optional<double> unbroken = original.RunUntil(Dock::Stage::Docked, 60.0);
	REQUIRE(unbroken);
	CHECK(loaded.RunUntil(Dock::Stage::Docked, 60.0) == unbroken);
	CHECK(loaded.FromRest(kShuttleHeight) == original.FromRest(kShuttleHeight));
	CHECK(loaded.touched.empty());

	// Docked, saved and made again, it is where it would have been ten seconds on, and undocks.
	const std::vector<std::byte> docked = original.world.Save();
	Approach again;
	again.world.Load(docked);
	for (int step = 0; step < 600; ++step)
	{
		original.Advance();
		again.Advance();
	}
	REQUIRE(again.Docking());
	CHECK(again.Docking()->stage == Dock::Stage::Docked);
	CHECK(again.FromRest(kShuttleHeight) == original.FromRest(kShuttleHeight));
	again.world.Commands().Push(Dock::ReleaseBerth{{}, again.vehicle});
	for (int step = 0; step < 60 * 120 && again.Docking(); ++step)
		again.Advance();
	CHECK_FALSE(again.Docking());
	CHECK(again.OnlyOnTheBerth());
}


TEST_CASE("Docks: a ship flown in from any side, at rest or going across, comes over its pad in time")
{
	// From all round Highport 2 km out, still or at its top speed across: one that came in fast and
	// sideways overshot the point above its berth into the platform's sphere and circled it, steered
	// out each time, until its berth lapsed, as one came round the platform from below at rest did.
	for (int side = 0; side < 12; ++side)
		for (const double across : {0.0, 150.0})
		{
			CAPTURE(side);
			CAPTURE(across);
			Fixture fixture;
			const entt::entity platform = fixture.docks.MakePlatform(
			    fixture.world, DockTests::kHarbour, DockTests::kFerry, "ostra-highport");
			entt::registry &registry = fixture.world.Registry();
			const glm::dquat axes = registry.get<Sim::Transform>(platform).orientation;
			const double angle = side * glm::two_pi<double>() / 12.0;
			const entt::entity vehicle = fixture.Vehicle(
			    platform, "shuttle",
			    axes * glm::dvec3{2000.0 * std::sin(angle), 2000.0 * std::cos(angle), 0.0});
			registry.get<Physics::Velocity>(vehicle).linear =
			    glm::vec3{axes * glm::dvec3{0.0, 0.0, across}};
			fixture.world.Commands().Push(Dock::RequestBerth{{}, vehicle, platform, true});
			bool above = false;
			for (int step = 0; step < 60 * 120 && !above; ++step)
			{
				fixture.Step();
				const auto *docking = registry.try_get<Dock::Docking>(vehicle);
				above = docking && docking->stage == Dock::Stage::Descending;
			}
			CHECK(above);
		}
}


namespace
{
// A harbour mid-way, the same in every build: Corva Refinery's four frigate berths given to four of
// five transports, flown in by the docks and kept clear by their shapes, the fifth waiting, and a
// shuttle's berth and a transport's controls asked for in commands not yet applied.
void Harbour(Fixture &fixture)
{
	Sim::SimWorld &world = fixture.world;
	const entt::entity platform =
	    fixture.docks.MakePlatform(world, DockTests::kHarbour, DockTests::kFerry, "corva-refinery");
	entt::entity last{entt::null};
	for (int index = 0; index < 5; ++index)
	{
		last = fixture.Vehicle(platform, "transport", {-1200.0 + 600.0 * index, 900.0, 2500.0});
		world.Commands().Push(Dock::RequestBerth{{}, last, platform, true});
	}
	fixture.Run(40.0);
	const entt::entity shuttle = fixture.Vehicle(platform, "shuttle", {0.0, 700.0, -1500.0});
	world.Commands().Push(Dock::RequestBerth{{}, shuttle, platform, true});
	world.Commands().Push(
	    Physics::SetFlightControls{{}, last, {{0.0f, 0.0f, -1.0f}, {}, true, platform}});
}
} // namespace


TEST_CASE("Docks: a harbour saved before the space extraction's renames loads as this build runs it")
{
	// Written by the build before them (b7d4580), at the world's version 4: its types and fields in the
	// space setting's words, the shapes the docks named "hulls/" and "stations/", a transfer of the
	// retired loading deck on an entity of its own, and the shuttle's berth and the transport's
	// controls in commands still queued.
	std::ifstream in{
	    std::string{JETTISON_DOCK_FIXTURE_DIR} + "/saves/harbour-version-4.world", std::ios::binary};
	const std::vector<char> read{std::istreambuf_iterator<char>{in}, {}};
	std::vector<std::byte> save(read.size());
	std::memcpy(save.data(), read.data(), read.size());
	REQUIRE(Serialise::PeekVersion(save, "Jettison.World") == 4);
	Fixture loaded;
	loaded.world.Load(save);

	Fixture live;
	Harbour(live);
	// The transfer's entity, which loads with nothing on it.
	(void)live.world.Registry().create();
	CHECK(loaded.world.StateHash() == live.world.StateHash());
	CHECK(loaded.world.Registry().view<Dock::Docking>().size() == 5);
	loaded.Run(30.0);
	live.Run(30.0);
	CHECK(loaded.world.StateHash() == live.world.StateHash());
	CHECK(loaded.world.Registry().view<Dock::Docking>().size() == 6);
}
