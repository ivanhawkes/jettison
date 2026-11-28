#include <jettison/physics/CollisionFile.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/SimWorld.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "Fuzz.h"
#include "World.h"

using namespace Jettison;


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A collision file read, and its shape built by Jolt in a world of its own, as a model's collision
// is defined for its bodies.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	Physics::ShapeDefinition shape;
	if (Fuzz::Refuses<std::runtime_error>(
	        [&] { shape = Physics::ReadCollisionFile(Fuzz::Bytes(pData, size), "fuzz.collision"); }))
		return 0;
	Sim::SimWorld world{Fuzz::kStep, Fuzz::kSeed};
	Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
	Fuzz::Refuses<std::invalid_argument>([&] { physics.DefineShape("fuzz", shape); });
	return 0;
}
