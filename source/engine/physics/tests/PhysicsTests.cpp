#include <jettison/physics/PhysicsWorld.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Physics: an empty world has no bodies; stepping fails loudly until phase 7")
{
	Jettison::Physics::PhysicsWorld world;
	CHECK(world.BodyCount() == 0);
	CHECK_THROWS_AS(world.Step(1.0 / 60.0), Jettison::Core::NotImplementedError);
}
