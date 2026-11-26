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
#include <limits>
#include <optional>
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
	constexpr int kSaveAt = 60 * 100;
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
	// storages put back, it hashes as before; and in J09 for the way in flown as a corridor without
	// stopping, a reservation put off while the vehicle comes nearer (87 s to the corridor, 30 s down,
	// against the stopgap's 117 and 24).
	CHECK(first == 0x7ec1f1e21c5561b0ull);

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
	constexpr int kSaveAt = 60 * 100;
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


TEST_CASE("Docks: a ship flown in from any side, at rest or going across, lands on its pad in time")
{
	// From all round Highport 2 km out, still or at its top speed across, down a corridor without
	// stopping: one that came in fast and sideways once overshot the point above its berth into the
	// platform's sphere and circled it until its berth lapsed, and the stopgap that cured it, a stop
	// out along the normal, took up to 92 s to the clamp from here; the corridor takes no more than
	// 80, touching nothing.
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
			bool docked = false;
			for (int step = 0; step < 60 * 80 && !docked; ++step)
			{
				fixture.Step();
				const auto *docking = registry.try_get<Dock::Docking>(vehicle);
				docked = docking && docking->stage == Dock::Stage::Docked;
			}
			CHECK(docked);
			CHECK(fixture.collisions.empty());
		}
}


TEST_CASE("Docks: a ship flown in under a pier lands on its covered pad and is flown out from under it")
{
	// A deck 400 m square with its top at y = 10, a corvette berth on it, and a pier 40 m over the
	// berth reaching out past it on every side, so it is come to and left from among them; from
	// 500 m off, and from resting on the deck 140 m away, which it lifts off straight first.
	for (const glm::dvec3 &start : {glm::dvec3{300.0, 400.0, 300.0}, glm::dvec3{100.0, 0.9, 100.0}})
	{
		CAPTURE(start.y);
		Fixture fixture;
		Dock::PlatformLayout layout;
		layout.decks = {{{0.0, 0.0, 0.0}, {400.0, 20.0, 400.0}}};
		layout.piers = {{{0.0, 55.0, -50.0}, {200.0, 10.0, 200.0}}};
		layout.berths = {{"corvette", {0.0, 10.0, 0.0}}};
		fixture.docks.DefineLayout("covered", layout);
		const entt::entity platform = fixture.docks.MakePlatform(
		    fixture.world, DockTests::kHarbour, DockTests::kFerry, "covered");
		entt::registry &registry = fixture.world.Registry();
		const entt::entity vehicle =
		    fixture.Vehicle(platform, "shuttle", start + glm::dvec3{0.0, 10.0, 0.0});
		fixture.world.Commands().Push(Dock::RequestBerth{{}, vehicle, platform, true});

		// Each contact the vehicle began, in the berth's axes as the berth was then.
		std::vector<glm::dvec3> touched;
		const auto step = [&]
		{
			const std::size_t before = fixture.collisions.size();
			fixture.Step();
			const Sim::Transform &berth =
			    registry.get<Sim::Transform>(fixture.BerthNumbered(platform, 1));
			for (std::size_t index = before; index < fixture.collisions.size(); ++index)
				if (fixture.collisions[index].first == vehicle ||
				    fixture.collisions[index].second == vehicle)
					touched.push_back(
					    glm::conjugate(berth.orientation) *
					    (fixture.collisions[index].point - berth.position));
		};
		const auto stage = [&] { return registry.get<Dock::Docking>(vehicle).stage; };
		step();
		REQUIRE(registry.all_of<Dock::Docking>(vehicle));
		for (int index = 0; index < 60 * 600 && stage() != Dock::Stage::Docked; ++index)
			step();
		REQUIRE(stage() == Dock::Stage::Docked);

		// It came in under the pier touching nothing on the way but its berth, within the corvette
		// berth's 35 by 40 m; one resting on the deck at the start lifted off it where it lay.
		for (const glm::dvec3 &at : touched)
		{
			CAPTURE(at);
			const bool onBerth = std::abs(at.y) < 0.1 && std::abs(at.x) < 17.5 && std::abs(at.z) < 20.0;
			const bool whereItLay =
			    start.y < 1.0 && glm::distance(at, start - glm::dvec3{0.0, 0.9, 0.0}) < 10.0;
			CHECK((onBerth || whereItLay));
		}

		// Flown out from under the pier by the shortest way out, touching nothing once it is let go,
		// and its berth given up beyond the platform's sphere.
		fixture.world.Commands().Push(Dock::ReleaseBerth{{}, vehicle});
		step();
		const std::size_t released = touched.size();
		for (int index = 0; index < 60 * 300 && registry.all_of<Dock::Docking>(vehicle); ++index)
			step();
		CHECK_FALSE(registry.all_of<Dock::Docking>(vehicle));
		CHECK(touched.size() == released);
		CHECK(
		    glm::length(
		        registry.get<Sim::Transform>(vehicle).position -
		        registry.get<Sim::Transform>(platform).position) > Dock::Radius(layout));
	}
}


TEST_CASE(
    "Docks: a long, low ship under a low roof is lifted, flown out and flown in without meeting it")
{
	// A berth 8 m under a pier, the least its class allows a vehicle 20 m long and 2 m high, whose
	// sphere would not fit under it. Lifted off by its own thrusters, it slows to the point over the
	// berth and is let go there; flown out and in by the docks, it goes level under the pier, slowing
	// for the corner beside the deck's face it comes down to.
	Fixture fixture{false};
	fixture.docks.DefineBerthClass("small", {0, {20.0f, 4.0f, 2.0f}, {10.0f, 30.0f}});
	fixture.docks.DefineVehicle("long", {"long", 20.0f, 4.0f, 2.0f, "small", {}});
	Dock::PlatformLayout layout;
	layout.decks = {{{0.0, 0.0, 0.0}, {400.0, 20.0, 400.0}}};
	layout.piers = {{{0.0, 23.0, 0.0}, {200.0, 10.0, 200.0}}};
	layout.berths = {{"small", {0.0, 10.0, 0.0}}};
	fixture.docks.DefineLayout("roof", layout);
	const entt::entity platform =
	    fixture.docks.MakePlatform(fixture.world, DockTests::kHarbour, DockTests::kFerry, "roof");
	entt::registry &registry = fixture.world.Registry();

	// Let go, it is a body on the deck again; what it meets after that is the roof, if anything.
	const auto leaves = [&](bool fly)
	{
		const entt::entity vehicle = fixture.Vehicle(platform, "long", {0.0, 200.0, 0.0});
		fixture.docks.Park(fixture.world, vehicle, fixture.BerthNumbered(platform, 1));
		fixture.Step();
		fixture.world.Apply(Dock::ReleaseBerth{{}, vehicle, fly});
		fixture.Step();
		const std::size_t before = fixture.collisions.size();
		for (int step = 0; step < 60 * 60 && registry.all_of<Dock::Docking>(vehicle); ++step)
			fixture.Step();
		CHECK_FALSE(registry.all_of<Dock::Docking>(vehicle));
		CHECK(fixture.collisions.size() == before);
	};
	SUBCASE("lifted by its pilot")
	{
		leaves(false);
	}
	SUBCASE("flown out")
	{
		leaves(true);
	}
	SUBCASE("flown in")
	{
		const entt::entity vehicle = fixture.Vehicle(platform, "long", {300.0, 400.0, 300.0});
		fixture.world.Apply(Dock::RequestBerth{{}, vehicle, platform, true});
		const auto docked = [&]
		{
			const auto *docking = registry.try_get<Dock::Docking>(vehicle);
			return docking && docking->stage == Dock::Stage::Docked;
		};
		for (int step = 0; step < 60 * 180 && !docked(); ++step)
			fixture.Step();
		REQUIRE(docked());

		// Touching nothing but its berth, 20 by 4 m, its top 10 m up.
		const glm::dvec3 berth =
		    registry.get<Sim::Transform>(fixture.BerthNumbered(platform, 1)).position;
		for (const auto &contact : fixture.collisions)
		{
			const glm::dvec3 at = contact.point - berth;
			CAPTURE(at);
			CHECK(std::abs(at.y) < 0.1);
			CHECK(std::hypot(at.x, at.z) < 10.5);
		}
	}
}


TEST_CASE("Docks: two ships flown in together land on the next pads, each down its own corridor")
{
	// Side by side 2 km off Highport, given pads 1 and 2, 210 m apart along one pier.
	Fixture fixture;
	const entt::entity platform = fixture.docks.MakePlatform(
	    fixture.world, DockTests::kHarbour, DockTests::kFerry, "ostra-highport");
	entt::registry &registry = fixture.world.Registry();
	const glm::dquat axes = registry.get<Sim::Transform>(platform).orientation;
	const entt::entity first =
	    fixture.Vehicle(platform, "shuttle", axes * glm::dvec3{1200.0, 1500.0, 0.0});
	const entt::entity second =
	    fixture.Vehicle(platform, "shuttle", axes * glm::dvec3{1260.0, 1500.0, 0.0});
	fixture.world.Commands().Push(Dock::RequestBerth{{}, first, platform, true});
	fixture.world.Commands().Push(Dock::RequestBerth{{}, second, platform, true});
	fixture.Step();
	CHECK(fixture.NumberOf(first) == 1);
	CHECK(fixture.NumberOf(second) == 2);
	const auto docked = [&](entt::entity vehicle)
	{ return registry.get<Dock::Docking>(vehicle).stage == Dock::Stage::Docked; };
	for (int step = 0; step < 60 * 120 && !(docked(first) && docked(second)); ++step)
		fixture.Step();
	CHECK(docked(first));
	CHECK(docked(second));
	CHECK(fixture.collisions.empty());
}


// The way in measured from all round Highport, 2 and 4 km out, still or going across at 150 m/s: the
// seconds to the clamp, and the contacts on the way. Skipped in the gate; run it from a Release tree
// with
//
//   build/clang-release/source/engine/dock/jettison_dock_tests --no-skip -tc='Docks benchmark*'
TEST_CASE("Docks benchmark: the way in from every side" * doctest::skip())
{
	for (const double distance : {2000.0, 4000.0})
		for (int side = 0; side < 12; ++side)
			for (const double across : {0.0, 150.0})
			{
				Fixture fixture;
				const entt::entity platform = fixture.docks.MakePlatform(
				    fixture.world, DockTests::kHarbour, DockTests::kFerry, "ostra-highport");
				entt::registry &registry = fixture.world.Registry();
				const glm::dquat axes = registry.get<Sim::Transform>(platform).orientation;
				const double angle = side * glm::two_pi<double>() / 12.0;
				const entt::entity vehicle = fixture.Vehicle(
				    platform, "shuttle",
				    axes * glm::dvec3{distance * std::sin(angle), distance * std::cos(angle), 0.0});
				registry.get<Physics::Velocity>(vehicle).linear =
				    glm::vec3{axes * glm::dvec3{0.0, 0.0, across}};
				fixture.world.Commands().Push(Dock::RequestBerth{{}, vehicle, platform, true});
				std::optional<double> docked;
				for (int step = 0; step < 60 * 400 && !docked; ++step)
				{
					fixture.Step();
					const auto *docking = registry.try_get<Dock::Docking>(vehicle);
					if (docking && docking->stage == Dock::Stage::Docked)
						docked = static_cast<double>(step + 1) * DockTests::kStep;
				}
				MESSAGE(
				    distance, " m, side ", side, ", across ", across, " m/s: ", docked ? *docked : -1.0,
				    " s, ", fixture.collisions.size(), " contacts");
			}
}
