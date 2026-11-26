#include "Ground.h"

#include <memory>

#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/physics/Shape.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Sample
{
namespace
{
constexpr glm::vec3 kGroundHalfExtents{200.0f, 0.5f, 200.0f};

class GroundModel final : public Physics::FrameModel
{
  public:
	bool HasBody(std::string_view /*body*/) const override
	{
		return false;
	}


	Sim::Transform BodyAt(std::string_view /*body*/, double /*simSeconds*/) const override
	{
		return {};
	}


	std::function<glm::dvec3(const glm::dvec3 &)> GravityAt(double /*simSeconds*/) const override
	{
		return [](const glm::dvec3 &) { return glm::dvec3{0.0, -9.81, 0.0}; };
	}
};
} // namespace


std::uint64_t GroundFrame()
{
	return Sim::FrameId("ground");
}


void DefineGround(Physics::PhysicsWorld &physics)
{
	Physics::ShapeDefinition box;
	box.halfExtents = kGroundHalfExtents;
	physics.DefineShape("ground", box);
	box.halfExtents = kRoverHalfExtents;
	physics.DefineShape("rover", box);
	physics.DefineFrame(GroundFrame(), std::make_unique<GroundModel>());
}


entt::entity MakeGround(Sim::SimWorld &world, Sim::Controller player)
{
	entt::registry &registry = world.Registry();
	const entt::entity ground = registry.create();
	registry.emplace<Sim::Transform>(ground, Sim::Transform{{0.0, -kGroundHalfExtents.y, 0.0}});
	registry.emplace<Sim::InFrame>(ground, GroundFrame());
	registry.emplace<Physics::Body>(ground, Physics::Body{Physics::BodyKind::Static, "ground"});

	const entt::entity rover = registry.create();
	registry.emplace<Sim::Transform>(rover, Sim::Transform{{0.0, kRoverHalfExtents.y, 0.0}});
	registry.emplace<Sim::InFrame>(rover, GroundFrame());
	Physics::Body body{Physics::BodyKind::Dynamic, "rover"};
	body.mass = 800.0f;
	registry.emplace<Physics::Body>(rover, body);
	registry.emplace<Physics::Velocity>(rover);

	// Driven ahead and astern and turned, never lifted or rolled: gravity and the ground hold the
	// rest. The ground's grip resists a turn, so 2 rad/s² turned it not at all, and 8 does.
	registry.emplace<Physics::Handling>(
	    rover, Physics::Handling{
	               .forward = 6.0f,
	               .reverse = 3.0f,
	               .lateral = 0.0f,
	               .vertical = 0.0f,
	               .angularAcceleration = {0.0f, 8.0f, 0.0f},
	               .maxSpeed = 12.0f,
	               .maxAngularRate = {0.0f, 1.0f, 0.0f}});
	registry.emplace<Physics::FlightControls>(rover);
	registry.emplace<Sim::Helm>(rover, player);
	return rover;
}


entt::entity FindRover(const entt::registry &world)
{
	const auto rovers = world.view<const Physics::Handling>();
	return rovers.empty() ? entt::entity{entt::null} : rovers.front();
}
} // namespace Jettison::Sample
