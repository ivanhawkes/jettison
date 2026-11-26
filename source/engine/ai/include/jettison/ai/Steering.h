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

#include <jettison/serialise/Migration.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/Reason.h>

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
class FrameIndices;
}

namespace Jettison::Physics
{
struct Handling;
}

namespace Jettison::Ai
{
// Steering for local flight (docs/ai.md, "Steering"): seek, arrive, avoidance and
// separation. Each is a velocity, not an acceleration: flight assist (Physics::FlightControls)
// holds a demanded velocity, reaching it as fast as the thrusters allow, so a vehicle's steering is
// the velocity its behaviours sum to, held to its speed, and the flight model is what limits how
// fast it gets there. Positions are in the frame (Sim::InFrame); velocities are in the frame the agent
// flies in (Physics::LocalFrame), which everything it steers about shares, since bodies in two
// frames never meet.

// The vehicle being steered.
struct Agent
{
	glm::dvec3 position{0.0};
	glm::dvec3 velocity{0.0};
	double maxSpeed{0.0};

	// The acceleration it can count on to slow down with, whichever way it faces: what it brakes
	// for an arrival and before an obstacle with.
	double maxAcceleration{0.0};

	// Of a sphere about its position holding its shape.
	double radius{0.0};
};

// Something to keep clear of, a platform or another vehicle: a sphere about its position holding
// its shape, moving at its velocity.
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

// Down the line from start to the target, as a corridor into a berth is flown: along the line, the
// fastest speed it can still stop at the target from, as Arrive's; across it, back towards the line
// at the speed it can still stop on the line from. So a vehicle that comes onto the line fast and
// sideways is drawn onto it, its velocity across it damped, rather than pursuing the target round
// in circles. The target's velocity is added. A line of no length is Arrive.
glm::dvec3 ArriveAlong(
    const Agent &agent, const glm::dvec3 &start, const glm::dvec3 &target,
    const glm::dvec3 &targetVelocity = glm::dvec3{0.0});

// The acceleration steering brakes a vehicle at, Arrive's: a share of its weakest thruster, the rest
// left for turning and avoiding while it does.
double BrakingAcceleration(const Physics::Handling &handling);

// The desired velocity, turned and slowed to keep the agent's shape the clearance from every
// obstacle's:
//   - of the obstacles the agent, flying at the desired velocity, would pass within the clearance
//     of in the lookahead's seconds, the one it would reach first turns it aside, at the speed that
//     makes up the shortfall by the time it would reach it: straight away from where it would pass
//     closest, or, aimed at the centre, to its right as it sees its motion relative to the
//     obstacle, so two vehicles meeting head on each turn their own way;
//   - and towards each obstacle it is no faster, relative to it, than it can stop from before the
//     clearance, braking at its acceleration: none at all within it, but away.
// Then held to the agent's speed. The obstacles' order decides only a tie in what the agent would
// reach first, which goes to the earlier.
glm::dvec3 Avoid(
    const Agent &agent, const glm::dvec3 &desired, std::span<const Obstacle> obstacles,
    double lookahead, double clearance);

// Away from each neighbour whose shape is nearer the agent's than the separation, at the shortfall a
// second, summed: a gentle push that keeps vehicles holding position apart, which Avoid alone would let
// settle at its clearance. Nothing from a neighbour at the agent's own position.
glm::dvec3 Separate(const Agent &agent, std::span<const Obstacle> neighbours, double separation);

// A vehicle flown by steering, and how it keeps clear. With Physics::Handling, FlightControls, a
// Velocity, a Body (whose shape's ShapeRadius is its shape's) and a Sim::Transform and InFrame, the
// system "steering" flies it to its Destination. Registered with Reflect, every field Saved, by
// RegisterTypes.
struct Steered
{
	// Seconds ahead it looks for what it would meet.
	float lookahead{5.0f};

	// Metres kept between its shape and any obstacle's, a platform's or a vehicle's.
	float clearance{20.0f};

	// Metres it keeps from other vehicles' shapes as it holds position.
	float separation{40.0f};

	bool operator==(const Steered &) const = default;
};

// Where a steered vehicle is flown to: with an entity, the offset from it in its own axes, so a place
// off a platform's dock turns with the platform; without one, the offset is a point in the vehicle's
// frame, metres from the frame's body in the frame's axes (Physics::PhysicsWorld::FrameOrigin), so
// a holding point by a platform moves with it. The waypoints, in the vehicle's frame as a
// point is, are flown through in order on the way, each passed within the pass radius: a course
// (Courses.h, PlotCourse) made relative to the frame; or, with waypointsOnEntity, in the entity's
// own axes as the offset is, so a course in among a platform's decks turns with it. It has arrived
// within the arrival radius of the destination, moving no faster than the arrival speed relative to it,
// and from then holds there. Registered with Reflect, every field Saved, by RegisterTypes;
// Steering::Attach registers it with its chain (Migrations).
struct Destination
{
	entt::entity entity{entt::null};
	glm::dvec3 offset{0.0};
	std::vector<glm::dvec3> waypoints;
	bool waypointsOnEntity{false};

	// Metres a second; none is the thrusters' maxSpeed, which is also the most.
	float speed{0.0f};
	float passRadius{40.0f};
	float arrivalRadius{5.0f};
	float arrivalSpeed{1.0f};

	// The entity is kept clear of by its shape rather than its sphere: it, and every body on the
	// rails of the same body (what is fixed to a platform: the platform, and the vehicles
	// clamped to it), are left out of the obstacles, and the vehicle's own shape is swept along its way
	// instead (Physics::PhysicsWorld::CastShape), closing on what the sweep meets no faster than it
	// can stop from before its clearance, and coming to each waypoint no faster than its turn there
	// allows, as a corridor does. A platform's sphere holds every berth on it, so a vehicle
	// flown to one flies inside the sphere; what the sweep meets at the end of the way down is the
	// deck it lands on, short of which the destination is, and that is not held off.
	bool byShape{false};

	// Flown as a corridor (docs/ai.md, "Steering"): the course at a speed that falls with the whole
	// way left, through its waypoints to the destination, and past them down the line from the last
	// waypoint to the destination (ArriveAlong), so it runs in without stopping and is drawn onto the
	// line rather than circling the point.
	bool corridor{false};

	// The way the vehicle turns to, in the entity's axes (the frame's, without one), rather than to
	// the way it is going: a berth's, as it lands on it, rolled too.
	bool face{false};
	glm::dquat facing{1.0, 0.0, 0.0, 0.0};

	// The waypoint it is flying to, or the waypoints' count once it is past them.
	std::uint32_t next{0};
	bool arrived{false};

	bool operator==(const Destination &) const = default;

	// Its storage's chain: the current version alone until player testing (docs/decisions.md, "Formats
	// change in place until player testing").
	static const Serialise::Migrations<Serialise::BinaryRecord> &Migrations();
};

// The commands that set a steered vehicle's destination, from its first waypoint, and take it away,
// which leaves the vehicle holding still where it is. A vehicle that is no longer in the world is not
// one to fly, and either does nothing. Refused, with a DestinationRefused, from a sender that does not
// command the vehicle (Sim::MayCommand). Registered with Reflect, every field Saved, by RegisterTypes.
struct SetDestination
{
	Sim::Controller sender;
	entt::entity vehicle{entt::null};
	Destination destination;
};
struct ClearDestination
{
	Sim::Controller sender;
	entt::entity vehicle{entt::null};
};

// Raised as a vehicle arrives, once for each destination, and as its destination is taken from it
// because it cannot be flown to: its entity has gone, or is in another frame or local frame. Registered
// with Reflect, every field Saved, by RegisterTypes.
struct Arrived
{
	entt::entity vehicle{entt::null};
	entt::entity entity{entt::null};
};
struct DestinationLost
{
	entt::entity vehicle{entt::null};
	Sim::Reason reason;
};

// Raised as a SetDestination or a ClearDestination is refused, saying why. Registered with Reflect,
// every field Saved, by RegisterTypes.
struct DestinationRefused
{
	Sim::Controller sender;
	entt::entity vehicle{entt::null};
	Sim::Reason reason;
};

// Steering in a simulation world: the Steered and Destination components, the commands, and the
// system "steering" (kSystemName), which runs after the spatial index's ("spatial") and before the
// flight model ("flight"). Each step, for each steered vehicle with a destination, in entity order:
//   1. Where it is flying to: its next waypoint, or the destination, where it is as the step
//      begins. A vehicle within the pass radius of its waypoint goes on to the next.
//   2. What is near, from its frame's spatial index: every body in its frame within the distance
//      two vehicles at its speed close in the lookahead, so one faster than it is seen later, widened
//      by the largest shape (Physics::PhysicsWorld::LargestShapeRadius) and the larger of its
//      clearance and separation. Bodies but sensors are obstacles, each the sphere of its shape's
//      ShapeRadius, but for what a destination kept clear of by its shape leaves out (byShape); the
//      dynamic bodies among them, vehicles, are its neighbours.
//   3. Its velocity: Seek to a waypoint, Arrive at the destination, then Avoid and Separate; and,
//      kept clear by its shape, slowed to stop short of what its shape swept along the way meets.
//   4. Its controls, with assist on and taken against its frame: the velocity in its own axes as
//      a share of its maxSpeed, and a turn towards it, nose (-Z) first, at the rate it can still
//      stop the turn from at half its angular acceleration, rolling none; or, past its waypoints
//      to a destination with a facing, a turn to the facing, about all three axes.
// A vehicle with no destination is left alone. It brakes at half its weakest thruster, which leaves
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
	    const Spatial::FrameIndices &indices);

  private:
	Steering(const Physics::PhysicsWorld &physics, const Spatial::FrameIndices &indices)
	    : m_physics{physics}, m_indices{indices}
	{
	}

	void Step(Sim::SimWorld &world);

	double Radius(std::string_view shape);

	// The velocity, slowed along its way to stop the clearance short of what the vehicle's shape, swept
	// along it, would meet before its target (Destination::byShape).
	glm::dvec3 ShapeLimited(
	    std::uint64_t frame, entt::entity vehicle, std::string_view shape, const Sim::Transform &at,
	    const Agent &agent, const glm::dvec3 &velocity, const glm::dvec3 &target,
	    double clearance) const;

	const Physics::PhysicsWorld &m_physics;
	const Spatial::FrameIndices &m_indices;

	// Each shape's ShapeRadius, as it is asked for: shapes are definitions, the same every run.
	std::map<std::string, double, std::less<>> m_radii;
};
} // namespace Jettison::Ai
