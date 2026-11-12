#pragma once

#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::Sim
{
class SimWorld;
struct Transform;
} // namespace Jettison::Sim

namespace Jettison::Spatial
{
class SystemIndices;
}

namespace Jettison::Ai
{
// Steering for local flight (docs/architecture.md, "Steering"): seek, arrive, avoidance and
// separation. Each is a velocity, not an acceleration: flight assist (Physics::FlightControls)
// holds a demanded velocity, reaching it as fast as the thrusters allow, so a ship's steering is
// the velocity its behaviours sum to, held to its speed, and the flight model is what limits how
// fast it gets there. Positions are in the system frame; velocities are in the frame the agent
// flies in (Physics::LocalFrame), which everything it steers about shares, since bodies in two
// frames never meet.

// The ship being steered.
struct Agent
{
	glm::dvec3 position{0.0};
	glm::dvec3 velocity{0.0};
	double maxSpeed{0.0};
	// The acceleration it can count on to slow down with, whichever way it faces: what it brakes
	// for an arrival and before an obstacle with.
	double maxAcceleration{0.0};
	// Of a sphere about its position holding its hull.
	double radius{0.0};
};

// Something to keep clear of, a station or another ship: a sphere about its position holding its
// hull, moving at its velocity.
struct Obstacle
{
	glm::dvec3 position{0.0};
	glm::dvec3 velocity{0.0};
	double radius{0.0};
};

// Towards the target at the agent's speed; nothing at the target.
glm::dvec3 Seek(const Agent &agent, const glm::dvec3 &target);

// Towards the target at the fastest speed the agent can still stop at it from, braking at its
// acceleration, √(2ad); within the last metres no faster than the distance a second, which keeps
// the final approach from oscillating between steps; and never faster than its speed. The target's
// velocity is added, so the agent arrives at rest relative to a target that moves.
glm::dvec3 Arrive(
    const Agent &agent, const glm::dvec3 &target, const glm::dvec3 &targetVelocity = glm::dvec3{0.0});

// The desired velocity, turned and slowed to keep the agent's hull the clearance from every
// obstacle's:
//   - of the obstacles the agent, flying at the desired velocity, would pass within the clearance
//     of in the lookahead's seconds, the one it would reach first turns it aside, at the speed that
//     makes up the shortfall by the time it would reach it: straight away from where it would pass
//     closest, or, aimed at the centre, to its right as it sees its motion relative to the
//     obstacle, so two ships meeting head on each turn their own way;
//   - and towards each obstacle it is no faster, relative to it, than it can stop from before the
//     clearance, braking at its acceleration: none at all within it, but away.
// Then held to the agent's speed. The obstacles' order decides only a tie in what the agent would
// reach first, which goes to the earlier.
glm::dvec3 Avoid(
    const Agent &agent, const glm::dvec3 &desired, std::span<const Obstacle> obstacles,
    double lookahead, double clearance);

// Away from each neighbour whose hull is nearer the agent's than the separation, at the shortfall a
// second, summed: a gentle push that keeps ships holding station apart, which Avoid alone would let
// settle at its clearance. Nothing from a neighbour at the agent's own position.
glm::dvec3 Separate(const Agent &agent, std::span<const Obstacle> neighbours, double separation);

// A ship flown by steering, and how it keeps clear. With Physics::Thrusters, FlightControls, a
// Velocity, a Body (whose shape's ShapeRadius is its hull's) and a Sim::Transform and InSystem, the
// system "steering" flies it to its Destination. Registered with Reflect, every field Saved, by
// RegisterTypes.
struct Steered
{
	// Seconds ahead it looks for what it would meet.
	float lookahead{5.0f};
	// Metres kept between its hull and any obstacle's, a station's or a ship's.
	float clearance{20.0f};
	// Metres it keeps from other ships' hulls as it holds station.
	float separation{40.0f};

	bool operator==(const Steered &) const = default;
};

// Where a steered ship is flown to: with an entity, the offset from it in its own axes, so a place
// off a station's dock turns with the station; without one, the offset is a point in the ship's
// frame, metres from the frame's body in the system's axes (Physics::PhysicsWorld::FrameOrigin), so
// a holding point by a station moves with it on its orbit. The waypoints, in the ship's frame as a
// point is, are flown through in order on the way, each passed within the pass radius: a course
// (Routes.h, PlotCourse) made relative to the frame. It has arrived within the arrival radius of
// the destination, moving no faster than the arrival speed relative to it, and from then holds
// there. Registered with Reflect, every field Saved, by RegisterTypes.
struct Destination
{
	entt::entity entity{entt::null};
	glm::dvec3 offset{0.0};
	std::vector<glm::dvec3> waypoints;
	// Metres a second; none is the thrusters' maxSpeed, which is also the most.
	float speed{0.0f};
	float passRadius{40.0f};
	float arrivalRadius{5.0f};
	float arrivalSpeed{1.0f};
	// The entity is kept clear of by its hull rather than its sphere: it, and every body on the
	// rails of the same celestial body (what is fixed to a station: the station, and the ships
	// clamped to it), are left out of the obstacles, and the ship's own shape is swept along its way
	// instead (Physics::PhysicsWorld::CastShape), closing on what the sweep meets no faster than it
	// can stop from before its clearance. A station's sphere holds every pad on it, so a ship
	// flown to one flies inside the sphere; what the sweep meets at the end of the way down is the
	// deck it lands on, short of which the destination is, and that is not held off.
	bool byHull{false};
	// The way the ship turns to, in the entity's axes (the system's, without one), rather than to
	// the way it is going: a pad's, as it lands on it, rolled too.
	bool face{false};
	glm::dquat facing{1.0, 0.0, 0.0, 0.0};
	// The waypoint it is flying to, or the waypoints' count once it is past them.
	std::uint32_t next{0};
	bool arrived{false};

	bool operator==(const Destination &) const = default;
};

// The commands that set a steered ship's destination, from its first waypoint, and take it away,
// which leaves the ship holding still where it is. A ship that is no longer in the world is not one
// to fly, and either does nothing. Registered with Reflect, every field Saved, by RegisterTypes.
struct SetDestination
{
	entt::entity ship{entt::null};
	Destination destination;
};
struct ClearDestination
{
	entt::entity ship{entt::null};
};

// Raised as a ship arrives, once for each destination, and as its destination is taken from it
// because it cannot be flown to: its entity has gone, or is in another system or frame. Registered
// with Reflect, every field Saved, by RegisterTypes.
struct Arrived
{
	entt::entity ship{entt::null};
	entt::entity entity{entt::null};
};
struct DestinationLost
{
	entt::entity ship{entt::null};
	std::string reason;
};

// Steering in a simulation world: the Steered and Destination components, the commands, and the
// system "steering" (kSystemName), which runs after the spatial index's ("spatial") and before the
// flight model ("flight"). Each step, for each steered ship with a destination, in entity order:
//   1. Where it is flying to: its next waypoint, or the destination, where it is as the step
//      begins. A ship within the pass radius of its waypoint goes on to the next.
//   2. What is near, from its system's spatial index: every body in its frame within the distance
//      two ships at its speed close in the lookahead, so one faster than it is seen later, widened
//      by the largest hull (Physics::PhysicsWorld::LargestShapeRadius) and the larger of its
//      clearance and separation. Bodies but sensors are obstacles, each the sphere of its shape's
//      ShapeRadius, but for what a destination kept clear of by its hull leaves out (byHull); the
//      dynamic bodies among them, ships, are its neighbours.
//   3. Its velocity: Seek to a waypoint, Arrive at the destination, then Avoid and Separate; and,
//      kept clear by its hull, slowed to stop short of what its shape swept along the way meets.
//   4. Its controls, with assist on and taken against its frame: the velocity in its own axes as
//      a share of its maxSpeed, and a turn towards it, nose (-Z) first, at the rate it can still
//      stop the turn from at half its angular acceleration, rolling none; or, past its waypoints
//      to a destination with a facing, a turn to the facing, about all three axes.
// A ship with no destination is left alone. It brakes at half its weakest thruster, which leaves
// the rest for turning and avoiding; its speed is the destination's, or its thrusters' maxSpeed.
//
// The world holds it from Attach on, through the systems and commands it registers.
class Steering
{
  public:
	static constexpr std::string_view kSystemName = "steering";

	// Registers the components, the commands and the system, which reads the physics world and the
	// spatial index given, both the same world's. Throws std::logic_error as SimWorld's
	// registrations do, for one already made.
	static Steering &Attach(
	    Sim::SimWorld &world, const Physics::PhysicsWorld &physics,
	    const Spatial::SystemIndices &indices);

  private:
	Steering(const Physics::PhysicsWorld &physics, const Spatial::SystemIndices &indices)
	    : m_physics{physics}, m_indices{indices}
	{
	}

	void Step(Sim::SimWorld &world);
	double Radius(std::string_view shape);
	// The velocity, slowed along its way to stop the clearance short of what the ship's hull, swept
	// along it, would meet before its target (Destination::byHull).
	glm::dvec3 HullLimited(
	    std::uint64_t system, entt::entity ship, std::string_view shape, const Sim::Transform &at,
	    const Agent &agent, const glm::dvec3 &velocity, const glm::dvec3 &target,
	    double clearance) const;

	const Physics::PhysicsWorld &m_physics;
	const Spatial::SystemIndices &m_indices;
	// Each shape's ShapeRadius, as it is asked for: shapes are definitions, the same every run.
	std::map<std::string, double, std::less<>> m_radii;
};
} // namespace Jettison::Ai
