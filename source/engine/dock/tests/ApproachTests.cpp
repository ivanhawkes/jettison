#include "Fixture.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

using namespace Jettison;
using DockTests::Fixture;

namespace
{
// Ostra Highport built to its layout, and a shuttle 10 km below it, on the far side from its pads,
// which face up its +Y, and off to one side.
struct Approach : Fixture
{
	entt::entity station{entt::null};
	entt::entity ship{entt::null};

	Approach()
	{
		station = docks.MakeStation(world, DockTests::kHalcyon, DockTests::kHighport, "ostra-highport");
		const glm::dquat axes = world.Registry().get<Sim::Transform>(station).orientation;
		ship = Ship(station, "shuttle", axes * glm::dvec3{1500.0, -9800.0, 1200.0});
		world.Commands().Push(Dock::RequestPad{{}, ship, station, true});
	}

	// Where each contact the ship began was, in pad 1's axes from the middle of its face, as the pad
	// was when they touched.
	std::vector<glm::dvec3> touched;

	void Advance()
	{
		const std::size_t before = collisions.size();
		Step();
		const Sim::Transform &pad = world.Registry().get<Sim::Transform>(PadNumbered(station, 1));
		for (std::size_t index = before; index < collisions.size(); ++index)
			touched.push_back(
			    glm::conjugate(pad.orientation) * (collisions[index].point - pad.position));
	}

	// Whether every contact was the hull on its pad: on the pad's face, within its 80 by 180 m.
	bool OnlyOnThePad() const
	{
		return std::ranges::all_of(
		    touched, [](const glm::dvec3 &at)
		    { return std::abs(at.y) < 0.1 && std::abs(at.x) < 40.0 && std::abs(at.z) < 90.0; });
	}

	const Dock::Docking *Docking() const
	{
		return world.Registry().try_get<Dock::Docking>(ship);
	}

	// Steps until the ship's stage is the one asked for, or the time is up: the seconds it took.
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

	// Where the ship is in the pad's axes, from where its hull rests on it.
	glm::dvec3 FromRest(double height) const
	{
		const entt::registry &registry = world.Registry();
		const Sim::Transform &pad = registry.get<Sim::Transform>(PadNumbered(station, 1));
		return glm::conjugate(pad.orientation) *
		           (registry.get<Sim::Transform>(ship).position - pad.position) -
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
	REQUIRE(approach.Events<Dock::PadGiven>().size() == 1);
	CHECK(approach.NumberOf(approach.ship) == 1);

	const std::optional<double> above = approach.RunUntil(Dock::Stage::Descending, 300.0);
	REQUIRE(above);
	// Round the station's sphere, not through it: no contact on the way.
	CHECK(approach.collisions.empty());
	const std::optional<double> landed = approach.RunUntil(Dock::Stage::Docked, 120.0);
	REQUIRE(landed);
	MESSAGE("Over the pad after ", *above, " s, clamped ", *landed, " s later");
	CHECK(approach.collisions.empty());
	const std::vector<Dock::Landed> events = approach.Events<Dock::Landed>();
	REQUIRE(events.size() == 1);
	CHECK(events[0].number == 1);

	// Clamped where its hull rests, turned to the pad, kinematic on the station's rails.
	entt::registry &registry = approach.world.Registry();
	CHECK(registry.get<Physics::Body>(approach.ship).kind == Physics::BodyKind::Kinematic);
	approach.Run(5.0);
	CHECK(glm::length(approach.FromRest(kShuttleHeight)) < 1.0e-3);
	const Sim::Transform &pad = registry.get<Sim::Transform>(approach.PadNumbered(approach.station, 1));
	CHECK(
	    glm::dot(
	        registry.get<Sim::Transform>(approach.ship).orientation * glm::dvec3{0.0, 0.0, -1.0},
	        pad.orientation * glm::dvec3{0.0, 0.0, -1.0}) > 0.999999);
	CHECK(approach.PadOf(approach.PadNumbered(approach.station, 1)).state == Dock::PadState::Holding);
	CHECK(approach.collisions.empty());
}

TEST_CASE("Docks: undocking lifts a ship clear of its pad, and gives the pad up once it is")
{
	Approach approach;
	REQUIRE(approach.RunUntil(Dock::Stage::Docked, 420.0));
	approach.events.clear();
	approach.world.Commands().Push(Dock::ReleasePad{{}, approach.ship});
	approach.Advance();
	REQUIRE(approach.Events<Dock::Left>().size() == 1);
	CHECK(approach.Docking()->stage == Dock::Stage::Leaving);
	// Held on its pad until it is clear: no one else is given it.
	CHECK(approach.PadOf(approach.PadNumbered(approach.station, 1)).state == Dock::PadState::Holding);

	double nearest = std::numeric_limits<double>::infinity();
	for (int step = 0; step < 60 * 120 && approach.Docking(); ++step)
	{
		approach.Advance();
		nearest = std::min(nearest, approach.FromRest(kShuttleHeight).y);
	}
	REQUIRE_FALSE(approach.Docking());
	CHECK(approach.Events<Dock::PadReleased>().size() == 1);
	CHECK(approach.PadOf(approach.PadNumbered(approach.station, 1)).state == Dock::PadState::Free);
	CHECK(nearest > -0.01);
	// Straight up over the pad, outside the station's sphere, and held there.
	const glm::dvec3 at = approach.FromRest(kShuttleHeight);
	CHECK(std::hypot(at.x, at.z) < 5.0);
	CHECK(
	    glm::distance(
	        approach.world.Registry().get<Sim::Transform>(approach.ship).position,
	        approach.world.Registry().get<Sim::Transform>(approach.station).position) > 762.4);
	// It lay on the deck as it was let go, and touched nothing else.
	MESSAGE(approach.touched.size(), " contacts, as it was let go");
	CHECK(approach.OnlyOnThePad());
}

TEST_CASE(
    "Docks: a ship its pilot flies is lifted off its pad by its thrusters, and is the pilot's in a few "
    "seconds")
{
	Approach approach;
	REQUIRE(approach.RunUntil(Dock::Stage::Docked, 420.0));
	approach.events.clear();
	approach.world.Commands().Push(Dock::ReleasePad{{}, approach.ship, false});
	approach.Advance();
	REQUIRE(approach.Events<Dock::Left>().size() == 1);
	CHECK(approach.Docking()->stage == Dock::Stage::Leaving);
	entt::registry &registry = approach.world.Registry();
	// No point to fly to, only the thrusters firing up, against the station.
	CHECK_FALSE(registry.all_of<Ai::Destination>(approach.ship));
	CHECK(registry.get<Physics::FlightControls>(approach.ship).linear.y > 0.0f);
	CHECK(approach.PadOf(approach.PadNumbered(approach.station, 1)).state == Dock::PadState::Holding);

	int steps = 1;
	for (; steps < 60 * 30 && approach.Docking(); ++steps)
		approach.Advance();
	REQUIRE_FALSE(approach.Docking());
	MESSAGE("The pad given up ", steps / 60.0, " s after the clamps opened");
	CHECK(steps < 60 * 4);
	CHECK(approach.Events<Dock::PadReleased>().size() == 1);
	CHECK(approach.PadOf(approach.PadNumbered(approach.station, 1)).state == Dock::PadState::Free);
	// Straight up off the pad, the lift's height, held for the pilot, whose controls then fly it.
	const glm::dvec3 at = approach.FromRest(kShuttleHeight);
	CHECK(std::hypot(at.x, at.z) < 1.0);
	CHECK(at.y >= Dock::kLiftHeight);
	CHECK(at.y < Dock::kLiftHeight + 5.0);
	CHECK(registry.get<Physics::FlightControls>(approach.ship) == Physics::FlightControls{});
	approach.world.Commands().Push(
	    Physics::SetFlightControls{
	        {}, approach.ship, {{0.0f, 0.0f, -1.0f}, {}, true, approach.station}});
	approach.Run(2.0);
	CHECK(registry.get<Physics::Velocity>(approach.ship).linear.z != 0.0f);
	CHECK(approach.OnlyOnThePad());
}

TEST_CASE(
    "Docks: the same approach from the same start, and from a save part-way, reaches the same state")
{
	constexpr int kSaveAt = 60 * 120;
	constexpr int kSteps = 60 * 140;
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
	// Saved on the way down onto the pad, kept clear of the station by its hull, and clamped by the
	// end.
	CHECK(stage == Dock::Stage::Docked);
	CHECK(run(unused, -1).first == first);
	// Recorded from a Debug build and checked by the Release suite and under the sanitisers too, as
	// steering's is: a change here is a change to how a ship is flown in, or to what a save holds.
	// Re-pinned in 9d for Highport's display wing, six more pads and a deck more in its shape, with
	// the approach's times unchanged (111 s to the point above the pad, 23.75 s down); and in 9e for
	// the empty storages of transfers and buggies a save now holds, 9d's hash without them.
	CHECK(first == 0x639e63ab264aa8b9ull);

	Approach loaded;
	loaded.world.Load(*save);
	for (int step = kSaveAt; step < kSteps; ++step)
		loaded.Step();
	CHECK(loaded.world.StateHash() == first);
}

TEST_CASE("Docks: a ship made again from a save of another Jolt docks as it would have, and stays")
{
	// A docked ship is a kinematic body on its station's rails, and the clamps are presentation's,
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
	again.world.Commands().Push(Dock::ReleasePad{{}, again.ship});
	for (int step = 0; step < 60 * 120 && again.Docking(); ++step)
		again.Advance();
	CHECK_FALSE(again.Docking());
	CHECK(again.OnlyOnThePad());
}
