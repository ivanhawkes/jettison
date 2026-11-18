#include "Ground.h"

#include <doctest/doctest.h>

#include <cmath>

#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;
constexpr Sim::Controller kPlayer{Sim::ControllerKind::Player, 0};

struct Fixture
{
	Sim::SimWorld world{kStep, 1};
	entt::entity rover{entt::null};

	Fixture()
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InFrame>();
		Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
		world.RegisterComponent<Sim::Helm>();
		Sample::DefineGround(physics);
		rover = Sample::MakeGround(world, kPlayer);
	}

	void Drive(const Physics::FlightControls &controls, int steps)
	{
		world.Commands().Push(Physics::SetFlightControls{kPlayer, rover, controls});
		for (int step = 0; step < steps; ++step)
			world.Step();
	}

	const Sim::Transform &Rover() const
	{
		return world.Registry().get<Sim::Transform>(rover);
	}
};
} // namespace


TEST_CASE("Ground: the rover drives ahead and turns on the ground, under gravity, at its helm")
{
	Fixture fixture;
	CHECK(Sample::FindRover(fixture.world.Registry()) == fixture.rover);

	// Let go, it stays where it stands.
	fixture.Drive({}, 60);
	CHECK(fixture.Rover().position.y == doctest::Approx(0.4).epsilon(0.02));
	CHECK(std::abs(fixture.Rover().position.z) < 0.01);

	// Five seconds ahead, along its -Z, held on the ground: 6 m/s² towards 12 m/s.
	Physics::FlightControls ahead;
	ahead.linear.z = -1.0f;
	fixture.Drive(ahead, 300);
	CHECK(fixture.Rover().position.z < -30.0);
	CHECK(fixture.Rover().position.y == doctest::Approx(0.4).epsilon(0.02));

	// Turning left as it drives swings its nose towards -X, and keeps it upright.
	Physics::FlightControls turning = ahead;
	turning.angular.y = 1.0f;
	const double x = fixture.Rover().position.x;
	fixture.Drive(turning, 120);
	CHECK(fixture.Rover().position.x < x - 1.0);
	const glm::dvec3 up = fixture.Rover().orientation * glm::dvec3{0.0, 1.0, 0.0};
	CHECK(up.y > 0.99);
}
