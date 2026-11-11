#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/physics/Components.h>
#include <jettison/physics/Shape.h>
#include <jettison/sim/SimWorld.h>

namespace Jettison::Sim
{
class DefinitionRegistry;
}

namespace Jettison::Physics
{
// Jolt's capacities for one star system's bodies, fixed when that system's physics is made.
struct PhysicsSettings
{
	std::uint32_t maxBodies{8192};
	std::uint32_t maxBodyPairs{32768};
	std::uint32_t maxContactConstraints{16384};
	// The fastest a body moves in its system's frame. Jolt's default, 500 m/s, is far below a
	// station's 7.7 km/s on its orbit, and a kinematic body following one would be held to it.
	float maxLinearVelocity{1.0e5f};
};

struct RayHit
{
	entt::entity entity{entt::null};
	double distance{0.0};
	glm::dvec3 normal{0.0};
};

// Jolt in double precision, stepped inside the simulation's fixed step (docs/architecture.md,
// "Simulation and presentation"). Its state is the simulation's: the world owns it as a service
// (Sim::WorldService), a save holds its bodies and their contacts, and a world loaded from a save
// steps on exactly as the one saved did. Jolt's types stay inside this module; the engine sees
// bodies as entities with a Body, a Sim::Transform and a Sim::InSystem (Components.h).
//
// Each star system's bodies are a Jolt world of their own, made when the first body enters the
// system and gone when the last leaves: every system's positions are metres from its own primary,
// so two systems' bodies at the same coordinates are nowhere near each other. A body that changes
// system is made again in the other's.
//
// One fixed step, as the simulation system "physics" (kSystemName), which a game's systems order
// themselves against with `after`:
//   1. Bodies are made for entities that gained what makes one, and removed for those that lost it
//      or whose Body changed, which are made again. Every body to be made is checked first, and a
//      step that cannot make one throws std::invalid_argument naming the entity before it changes
//      anything (SimWorld::Step says what a throwing step leaves).
//   2. What the game wrote is applied: a static or dynamic body whose Transform was written is
//      placed there, a dynamic body whose Velocity was written takes it, and every kinematic body
//      is moved towards its Transform over the step.
//   3. Each system's world steps, in identifier order, on one thread.
//   4. Each dynamic body's Transform and Velocity are written from it.
// Bodies are visited in entity order throughout, so the same state and commands give the same
// calls into Jolt, and with Jolt built CROSS_PLATFORM_DETERMINISTIC, the same results in every
// build. There is no gravity: orbits are on rails (celestial), and what moves a ship is its own
// thrust.
class PhysicsWorld final : public Sim::WorldService
{
  public:
	static constexpr std::string_view kSystemName = "physics";
	static constexpr std::string_view kServiceName = "physics";

	// Makes a physics world and gives it to the simulation world, which owns it from then on:
	// registers Body and Velocity as components, the service, and the "physics" system. The game
	// registers Sim::Transform and Sim::InSystem itself, as it always does. Throws std::logic_error
	// as SimWorld's registrations do, for one already made.
	static PhysicsWorld &Attach(Sim::SimWorld &world, const PhysicsSettings &settings = {});

	~PhysicsWorld() override;

	// A shape bodies can name. Built here, so a shape Jolt refuses throws std::invalid_argument
	// naming it, as does a name already defined. Definitions are the game's start-up, the same
	// every run: a save names shapes, and a world it is loaded into has to define them.
	void DefineShape(std::string name, const ShapeDefinition &shape);
	// Every ShapeDefinition the registry has loaded, under its definition name.
	void DefineShapes(const Sim::DefinitionRegistry &definitions);
	bool HasShape(std::string_view name) const;

	std::size_t BodyCount() const;
	std::size_t BodyCount(std::uint64_t system) const;
	// The star systems that have bodies, in identifier order.
	std::vector<std::uint64_t> Systems() const;

	// Ray and shape casts are phase 7c's.
	std::optional<RayHit> CastRay(
	    std::uint64_t system, const glm::dvec3 &origin, const glm::dvec3 &direction,
	    double maxDistance) const;

	// Every system's bodies, each with what it was made from and was last placed at, and Jolt's own
	// state: positions, velocities, sleep, and the contacts each step starts from.
	void Save(Serialise::BinaryWriter &writer) const override;
	std::function<void()> Load(Serialise::BinaryReader &reader, const entt::registry &loaded) override;

  private:
	struct Impl;

	explicit PhysicsWorld(const PhysicsSettings &settings);
	void Step(entt::registry &registry, double fixedStep);

	std::unique_ptr<Impl> m_pImpl;
};
} // namespace Jettison::Physics
