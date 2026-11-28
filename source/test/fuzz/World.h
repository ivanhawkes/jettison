#pragma once

#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

// The world the save target loads into and its seeds are saved from: the shell's own composition
// (Application.cpp's EngineWorld), physics and a shape its bodies name, so a save's every engine
// component is read back as a game's would be.
namespace Jettison::Fuzz
{
inline constexpr double kStep = 1.0 / 60.0;
inline constexpr std::uint64_t kSeed = 1;
inline constexpr const char *kCrate = "crate";


inline void Compose(Sim::SimWorld &world)
{
	Sim::RegisterTypes();
	world.RegisterComponent<Sim::Transform>();
	world.RegisterComponent<Sim::InFrame>();
	Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
	world.RegisterComponent<Sim::ControlledBy>();
	physics.DefineShape(kCrate, Physics::ShapeDefinition{});
}
} // namespace Jettison::Fuzz
