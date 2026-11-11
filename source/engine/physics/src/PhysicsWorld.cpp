#include <jettison/physics/PhysicsWorld.h>

#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>

#include <algorithm>
#include <cmath>
#include <compare>
#include <format>
#include <map>
#include <stdexcept>
#include <utility>

#include <jettison/celestial/Orbit.h>
#include <jettison/core/Assert.h>
#include <jettison/core/Log.h>
#include <jettison/physics/Flight.h>
#include <jettison/profile/Profile.h>
#include <jettison/serialise/Binary.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Snapshot.h>

#include "Convert.h"
#include "JoltRuntime.h"
#include "Shapes.h"
#include "StarSystemWorld.h"

namespace Jettison::Physics
{
namespace
{
constexpr Core::Category kPhysics{"physics"};

// The layout Save writes inside the world's save. A change to it is a new number here, which a
// world's own version does not see. Layout 2 keys each Jolt world by its frame too, and holds where
// the frame was (phase 7b).
constexpr std::uint64_t kLayout = 2;

// Jolt's scratch memory for a step: what a step of a busy system needs, with the heap behind it
// for one that needs more, rather than Jolt's fixed block failing.
constexpr JPH::uint kTempAllocatorBytes = 8u * 1024u * 1024u;

struct ShapeEntry
{
	JPH::RefConst<JPH::Shape> shape;
	bool mesh{false};
};

// Which Jolt world a body is in: its star system, and the celestial body whose frame it is in, or
// none for the system's own. Ordered, system first, which is the order worlds step and save in.
struct WorldKey
{
	std::uint64_t system{Sim::kNoSystem};
	std::string frame;

	friend auto operator<=>(const WorldKey &, const WorldKey &) = default;
};

// A body as physics made it. Saved, but for the world, which is its key's and found again.
struct Record
{
	WorldKey key;
	JPH::BodyID id;
	// What the body was made from, so a change to the entity's Body is seen.
	Body body;
	// Where physics last put the body, or last saw the game put it, in the system frame: a Transform
	// that differs was written by the game. Exact, and compared with what the game wrote rather than
	// with Jolt's position, which comes back through the body's centre of mass and need not round to
	// it.
	Sim::Transform placed;
	// The same, for a moving body's velocity, relative to its frame.
	Velocity velocity;
	Detail::StarSystemWorld *world{nullptr};
};

// Where a frame is over one step: its origin as the step begins and ends, the velocity that
// carries it from one to the other, and, for a world made this step, the velocity it had over the
// step before, which its bodies' velocities are relative to as they are made.
struct FrameMotion
{
	glm::dvec3 start{0.0};
	glm::dvec3 end{0.0};
	glm::dvec3 velocity{0.0};
	glm::dvec3 before{0.0};
};

std::string Describe(entt::entity entity)
{
	return std::format("entity {:#x}", entt::to_integral(entity));
}

// Why a body of this description cannot be made, or nothing.
std::optional<std::string> Unmakeable(const Body &body, const ShapeEntry *shape, const WorldKey &key)
{
	if (!shape)
		return std::format("names shape '{}', which is not defined", body.shape);
	// Also what keeps a body from being made again every step: a NaN is unequal to itself, so a
	// Body holding one would never match the one its body was made from.
	for (const float value : {body.friction, body.restitution, body.linearDamping, body.angularDamping})
	{
		if (!std::isfinite(value) || value < 0.0f)
			return std::format(
			    "has a friction, restitution or damping of {}, which is not a finite amount of none or "
			    "more",
			    value);
	}
	if (body.kind == BodyKind::Dynamic)
	{
		if (shape->mesh)
			return std::format(
			    "is dynamic, and its shape '{}' is a mesh, which has no inside", body.shape);
		if (!std::isfinite(body.mass) || body.mass <= 0.0f)
			return std::format("is dynamic with a mass of {} kg", body.mass);
	}
	if (body.kind == BodyKind::Static && !key.frame.empty())
		return std::format(
		    "is static in the frame of '{}', which carries it: what a frame carries is kinematic",
		    key.frame);
	return std::nullopt;
}

JPH::BodyCreationSettings MakeSettings(
    entt::entity entity, const Body &body, const ShapeEntry &shape, const Sim::Transform &transform,
    const glm::dvec3 &origin, const Velocity &velocity, const PhysicsSettings &settings)
{
	static constexpr JPH::EMotionType kMotion[] = {
	    JPH::EMotionType::Static, JPH::EMotionType::Kinematic, JPH::EMotionType::Dynamic};
	JPH::BodyCreationSettings creation{
	    shape.shape, Detail::ToJolt(transform.position - origin),
	    Detail::ToJolt(transform.orientation).Normalized(),
	    kMotion[static_cast<std::size_t>(body.kind)],
	    body.kind == BodyKind::Static ? Detail::Layers::kStatic : Detail::Layers::kMoving};
	creation.mFriction = body.friction;
	creation.mRestitution = body.restitution;
	creation.mLinearDamping = body.linearDamping;
	creation.mAngularDamping = body.angularDamping;
	creation.mMaxLinearVelocity = settings.maxLinearVelocity;
	creation.mUserData = entt::to_integral(entity);
	switch (body.kind)
	{
		case BodyKind::Static: break;
		case BodyKind::Kinematic:
			// Nothing pushes a kinematic body, so its mass is never used; a mesh has none Jolt can
			// work out, so one is given rather than computed.
			creation.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
			creation.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox(JPH::Vec3::sOne(), 1.0f);
			break;
		case BodyKind::Dynamic:
			creation.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			creation.mMassPropertiesOverride.mMass = body.mass;
			creation.mLinearVelocity = Detail::ToJolt(velocity.linear);
			creation.mAngularVelocity = Detail::ToJolt(velocity.angular);
			if (body.continuous)
				creation.mMotionQuality = JPH::EMotionQuality::LinearCast;
			break;
	}
	return creation;
}

std::uint64_t SystemOf(const entt::registry &registry, entt::entity entity)
{
	const auto *inSystem = registry.try_get<Sim::InSystem>(entity);
	return inSystem ? inSystem->system : Sim::kNoSystem;
}

WorldKey KeyOf(const entt::registry &registry, entt::entity entity)
{
	const auto *frame = registry.try_get<LocalFrame>(entity);
	return {SystemOf(registry, entity), frame ? frame->body : std::string{}};
}

// Where a body on rails is at a time: its position, and its spin.
Sim::Transform RailsTransform(const Celestial::StarSystem &system, std::size_t body, double simSeconds)
{
	return {
	    Celestial::EvaluateBody(system, body, simSeconds).metres,
	    Celestial::Orientation(system.bodies[body].rotation, simSeconds)};
}

// Each body that pulls, where it is at a time: the system's bodies with a gravitational parameter.
struct Well
{
	glm::dvec3 position;
	double gravitationalParameter;
	double radius;
};

std::vector<Well> Wells(const Celestial::StarSystem &system, double simSeconds)
{
	std::vector<Well> wells;
	const std::vector<Celestial::SystemPosition> positions =
	    Celestial::EvaluateSystem(system, simSeconds);
	for (std::size_t body = 0; body < system.bodies.size(); ++body)
		if (system.bodies[body].gravitationalParameter > 0.0)
			wells.push_back(
			    {positions[body].metres, system.bodies[body].gravitationalParameter,
			     system.bodies[body].radius});
	return wells;
}

glm::dvec3 Pull(const std::vector<Well> &wells, const glm::dvec3 &position)
{
	glm::dvec3 acceleration{0.0};
	for (const Well &well : wells)
	{
		const glm::dvec3 towards = well.position - position;
		const double distance = glm::length(towards);
		if (distance <= 0.0)
			continue;
		// Inside the body, the mass within the distance pulls, as a uniform sphere's does, so the
		// pull falls to nothing at the centre rather than growing without bound.
		const double reach = std::max(distance, well.radius);
		acceleration += towards * (well.gravitationalParameter / (reach * reach * reach));
	}
	return acceleration;
}
} // namespace

struct PhysicsWorld::Impl
{
	explicit Impl(const PhysicsSettings &physicsSettings) : settings{physicsSettings}
	{
	}

	const ShapeEntry *FindShape(std::string_view name) const
	{
		const auto found = shapes.find(name);
		return found == shapes.end() ? nullptr : &found->second;
	}

	// Why a body cannot be in the frame its key names, or nothing: the system's own frame always
	// takes one, and a local frame has to be a body of a defined system.
	std::optional<std::string> UnknownFrame(const WorldKey &key) const
	{
		if (key.frame.empty())
			return std::nullopt;
		const auto system = systems.find(key.system);
		if (system == systems.end())
			return std::format(
			    "is in the frame of '{}' in system {:#x}, which is not defined", key.frame, key.system);
		if (system->second.Find(key.frame) < 0)
			return std::format(
			    "is in the frame of '{}', which system '{}' has no body of", key.frame,
			    system->second.name);
		return std::nullopt;
	}

	// The frame's origin at a time, in the system frame: none for the system's own frame. The key
	// has been checked (UnknownFrame).
	glm::dvec3 Origin(const WorldKey &key, double simSeconds) const
	{
		if (key.frame.empty())
			return glm::dvec3{0.0};
		const Celestial::StarSystem &system = systems.at(key.system);
		return Celestial::EvaluateBody(
		           system, static_cast<std::size_t>(system.Find(key.frame)), simSeconds)
		    .metres;
	}

	// A world made for a key on this step, its frame where it is as the step begins and moving as it
	// did over the step before: a body made still in a station's frame is moving with the station,
	// and falls with it, gaining the step's gravity as the frame gains its change of speed.
	Detail::StarSystemWorld &World(const WorldKey &key, const FrameMotion &motion)
	{
		std::unique_ptr<Detail::StarSystemWorld> &world = worlds[key];
		if (!world)
		{
			world = std::make_unique<Detail::StarSystemWorld>(layers, settings);
			world->frameOrigin = motion.start;
			world->frameVelocity = motion.before;
		}
		return *world;
	}

	// Makes the body in its world, with the identifier the world gives it or, from a save, the one
	// it had, at its place in the world's frame. Its record is the caller's to keep.
	void Make(
	    Detail::StarSystemWorld &world, const JPH::BodyID &id, entt::entity entity,
	    const Record &record, const ShapeEntry &shape) const
	{
		const JPH::BodyCreationSettings creation = MakeSettings(
		    entity, record.body, shape, record.placed, world.frameOrigin, record.velocity, settings);
		const JPH::Body *made = world.Bodies().CreateBodyWithID(id, creation);
		// Capacity was checked, and the identifier is one the world holds free for it.
		JETTISON_VERIFY(made != nullptr);
		world.Bodies().AddBody(
		    id, record.body.kind == BodyKind::Static ? JPH::EActivation::DontActivate
		                                             : JPH::EActivation::Activate);
	}

	PhysicsSettings settings;
	Detail::Layers layers;
	JPH::TempAllocatorImplWithMallocFallback scratch{kTempAllocatorBytes};
	// The simulation's one thread, as the threading rules have it (docs/architecture.md).
	JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
	// Before the worlds, whose bodies hold the shapes, so it goes after them.
	std::map<std::string, ShapeEntry, std::less<>> shapes;
	std::map<WorldKey, std::unique_ptr<Detail::StarSystemWorld>> worlds;
	// Ordered by entity, which is the order every step visits bodies in.
	std::map<entt::entity, Record> records;
	// By Sim::SystemId: definitions, not state, so neither saved nor hashed.
	std::map<std::uint64_t, Celestial::StarSystem> systems;
	bool reportedUpdateError{false};
};

PhysicsWorld::PhysicsWorld(const PhysicsSettings &settings)
{
	if (settings.maxBodies == 0 || settings.maxBodies > JPH::BodyID::cMaxBodyIndex + 1 ||
	    settings.maxBodyPairs == 0 || settings.maxContactConstraints == 0)
		throw std::invalid_argument{std::format(
		    "Physics: capacities of {} bodies, {} pairs and {} contacts are not capacities",
		    settings.maxBodies, settings.maxBodyPairs, settings.maxContactConstraints)};
	if (!std::isfinite(settings.maxLinearVelocity) || settings.maxLinearVelocity <= 0.0f)
		throw std::invalid_argument{
		    std::format("Physics: a speed limit of {} m/s is not a limit", settings.maxLinearVelocity)};
	Detail::EnsureJolt();
	m_pImpl = std::make_unique<Impl>(settings);
}

PhysicsWorld::~PhysicsWorld() = default;

PhysicsWorld &PhysicsWorld::Attach(Sim::SimWorld &world, const PhysicsSettings &settings)
{
	RegisterTypes();
	// Held before it is handed over, so the world's WorldService need not be cast back down.
	std::unique_ptr<PhysicsWorld> owned{new PhysicsWorld{settings}};
	PhysicsWorld &physics = *owned;
	world.AddService(std::string{kServiceName}, std::move(owned));
	world.RegisterComponent<Body>();
	world.RegisterComponent<Velocity>();
	world.RegisterComponent<OnRails>();
	world.RegisterComponent<LocalFrame>();
	world.RegisterComponent<Thrusters>();
	world.RegisterComponent<FlightControls>();
	world.RegisterCommand<SetFlightControls>(
	    [](Sim::SimWorld &target, const SetFlightControls &command)
	    {
		    entt::registry &registry = target.Registry();
		    if (registry.valid(command.ship))
			    registry.emplace_or_replace<FlightControls>(command.ship, command.controls);
	    });
	world.RegisterSystem(
	    {std::string{kFlightSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Thrusters, FlightControls, Sim::Transform>().Writing<Velocity>(),
	     {},
	     [](entt::registry &registry, double fixedStep) { StepFlight(registry, fixedStep); }});
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Body, Sim::InSystem, OnRails, LocalFrame>()
	         .Writing<Sim::Transform, Velocity>(),
	     {std::string{kFlightSystemName}},
	     [&physics, &world](entt::registry &registry, double fixedStep)
	     { physics.Step(registry, world.Clock().Now(), fixedStep); }});
	return physics;
}

void PhysicsWorld::DefineShape(std::string name, const ShapeDefinition &shape)
{
	if (name.empty())
		throw std::invalid_argument{"Physics: a shape needs a name"};
	if (m_pImpl->shapes.contains(name))
		throw std::invalid_argument{std::format("Physics: shape '{}' is already defined", name)};
	ShapeEntry entry{Detail::BuildShape(name, shape), shape.kind == ShapeKind::Mesh};
	m_pImpl->shapes.emplace(std::move(name), std::move(entry));
}

void PhysicsWorld::DefineShapes(const Sim::DefinitionRegistry &definitions)
{
	for (const std::string &name : definitions.Names<ShapeDefinition>())
		DefineShape(name, *definitions.Find<ShapeDefinition>(name));
}

bool PhysicsWorld::HasShape(std::string_view name) const
{
	return m_pImpl->FindShape(name) != nullptr;
}

void PhysicsWorld::DefineStarSystem(Celestial::StarSystem system)
{
	if (system.name.empty() || system.bodies.empty())
		throw std::invalid_argument{"Physics: a star system needs a name and bodies"};
	for (std::size_t body = 1; body < system.bodies.size(); ++body)
		if (system.bodies[body].parentIndex < 0)
			throw std::invalid_argument{std::format(
			    "Physics: star system '{}' is not resolved: '{}' has no parent", system.name,
			    system.bodies[body].name)};
	const std::uint64_t id = Sim::SystemId(system.name);
	if (m_pImpl->systems.contains(id))
		throw std::invalid_argument{
		    std::format("Physics: star system '{}' is already defined", system.name)};
	m_pImpl->systems.emplace(id, std::move(system));
}

bool PhysicsWorld::HasStarSystem(std::uint64_t system) const
{
	return m_pImpl->systems.contains(system);
}

glm::dvec3 PhysicsWorld::Gravity(
    std::uint64_t system, const glm::dvec3 &position, double simSeconds) const
{
	const auto found = m_pImpl->systems.find(system);
	return found == m_pImpl->systems.end() ? glm::dvec3{0.0}
	                                       : Pull(Wells(found->second, simSeconds), position);
}

std::size_t PhysicsWorld::BodyCount() const
{
	return m_pImpl->records.size();
}

std::size_t PhysicsWorld::BodyCount(std::uint64_t system) const
{
	std::size_t count = 0;
	for (const auto &[key, world] : m_pImpl->worlds)
		if (key.system == system)
			count += world->BodyCount();
	return count;
}

std::size_t PhysicsWorld::FrameCount(std::uint64_t system) const
{
	return static_cast<std::size_t>(std::ranges::count(
	    m_pImpl->worlds, system, [](const auto &entry) { return entry.first.system; }));
}

std::vector<std::uint64_t> PhysicsWorld::Systems() const
{
	std::vector<std::uint64_t> systems;
	for (const auto &[key, world] : m_pImpl->worlds)
		if (systems.empty() || systems.back() != key.system)
			systems.push_back(key.system);
	return systems;
}

std::optional<RayHit> PhysicsWorld::CastRay(
    std::uint64_t /*unused*/, const glm::dvec3 & /*unused*/, const glm::dvec3 & /*unused*/,
    double /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}

void PhysicsWorld::Step(entt::registry &registry, double simSeconds, double fixedStep)
{
	JETTISON_PROFILE_ZONE("Physics::Step");
	Impl &impl = *m_pImpl;
	const double stepEnd = simSeconds + fixedStep;

	// 1. Where the rails put their entities as the step ends, every one found before any is moved.
	struct Placement
	{
		entt::entity entity;
		const Celestial::StarSystem *system;
		std::size_t body;
	};
	std::vector<Placement> placements;
	for (const auto [entity, rails, transform] :
	     registry.view<const OnRails, const Sim::Transform>().each())
	{
		const std::uint64_t system = SystemOf(registry, entity);
		if (system == Sim::kNoSystem)
			continue;
		const auto found = impl.systems.find(system);
		const int body = found == impl.systems.end() ? -1 : found->second.Find(rails.body);
		if (body < 0)
			throw std::invalid_argument{std::format(
			    "Physics: {} cannot be on rails: system {:#x} {}", Describe(entity), system,
			    found == impl.systems.end() ? "is not defined"
			                                : std::format("has no body '{}'", rails.body))};
		placements.push_back({entity, &found->second, static_cast<std::size_t>(body)});
	}

	// 2. What changes: the bodies whose entity no longer makes them what they were, then the
	// entities that should have a body and will not, every one checked before anything changes.
	std::vector<entt::entity> removals;
	for (const auto &[entity, record] : impl.records)
	{
		const Body *body = registry.valid(entity) ? registry.try_get<Body>(entity) : nullptr;
		if (!body || !registry.all_of<Sim::Transform>(entity) ||
		    KeyOf(registry, entity) != record.key || *body != record.body)
			removals.push_back(entity);
	}
	struct Creation
	{
		entt::entity entity;
		WorldKey key;
		const ShapeEntry *shape;
	};
	std::vector<Creation> creations;
	const auto candidates = registry.view<const Body, const Sim::Transform>();
	for (const entt::entity entity : candidates)
	{
		const Body &body = candidates.get<const Body>(entity);
		WorldKey key = KeyOf(registry, entity);
		if (key.system == Sim::kNoSystem ||
		    (impl.records.contains(entity) && !std::ranges::binary_search(removals, entity)))
			continue;
		const ShapeEntry *shape = impl.FindShape(body.shape);
		std::optional<std::string> problem = Unmakeable(body, shape, key);
		if (!problem)
			problem = impl.UnknownFrame(key);
		if (problem)
			throw std::invalid_argument{
			    std::format("Physics: {} cannot be a body: it {}", Describe(entity), *problem)};
		creations.push_back({entity, std::move(key), shape});
	}
	std::ranges::sort(creations, std::less{}, &Creation::entity);
	{
		std::map<std::uint64_t, std::size_t> counts;
		for (const auto &[key, world] : impl.worlds)
			counts[key.system] += world->BodyCount();
		for (const entt::entity entity : removals)
			--counts[impl.records.at(entity).key.system];
		for (const Creation &creation : creations)
		{
			if (++counts[creation.key.system] > impl.settings.maxBodies)
				throw std::invalid_argument{std::format(
				    "Physics: {} cannot be a body: system {:#x} has its {} already",
				    Describe(creation.entity), creation.key.system, impl.settings.maxBodies)};
		}
	}

	for (const Placement &placement : placements)
		registry.get<Sim::Transform>(placement.entity) =
		    RailsTransform(*placement.system, placement.body, stepEnd);

	// Every frame's motion over the step: a world's frame starts where the last step left it, and a
	// new one's where its body is now.
	std::map<WorldKey, FrameMotion> motions;
	const auto motionOf = [&](const WorldKey &key) -> const FrameMotion &
	{
		auto [found, made] = motions.try_emplace(key);
		if (made)
		{
			const auto world = impl.worlds.find(key);
			FrameMotion &motion = found->second;
			motion.start =
			    world != impl.worlds.end() ? world->second->frameOrigin : impl.Origin(key, simSeconds);
			motion.end = impl.Origin(key, stepEnd);
			motion.velocity = (motion.end - motion.start) / fixedStep;
			motion.before = world != impl.worlds.end()
			                    ? world->second->frameVelocity
			                    : (motion.start - impl.Origin(key, simSeconds - fixedStep)) / fixedStep;
		}
		return found->second;
	};

	// A body made again in another frame of its system keeps its velocity, relative to the new
	// frame as it moved over the last step: the world it joins takes its own change of speed off
	// every body in it below.
	std::map<entt::entity, glm::dvec3> leftFrames;
	for (const entt::entity entity : removals)
	{
		const auto found = impl.records.find(entity);
		Detail::StarSystemWorld &world = *found->second.world;
		if (registry.valid(entity) && SystemOf(registry, entity) == found->second.key.system)
			leftFrames.emplace(entity, world.frameVelocity);
		world.Bodies().RemoveBody(found->second.id);
		world.Bodies().DestroyBody(found->second.id);
		world.Release(found->second.id);
		impl.records.erase(found);
	}
	for (Creation &creation : creations)
	{
		Record record;
		record.body = registry.get<Body>(creation.entity);
		record.placed = registry.get<Sim::Transform>(creation.entity);
		record.world = &impl.World(creation.key, motionOf(creation.key));
		if (record.body.kind == BodyKind::Kinematic)
			if (const auto *rails = registry.try_get<OnRails>(creation.entity))
			{
				const Celestial::StarSystem &system = impl.systems.at(creation.key.system);
				record.placed = RailsTransform(
				    system, static_cast<std::size_t>(system.Find(rails->body)), simSeconds);
			}
		if (record.body.kind != BodyKind::Static)
		{
			Velocity &velocity = registry.get_or_emplace<Velocity>(creation.entity);
			if (const auto left = leftFrames.find(creation.entity); left != leftFrames.end())
				velocity.linear =
				    glm::vec3{glm::dvec3{velocity.linear} + left->second - record.world->frameVelocity};
			record.velocity = velocity;
		}
		record.key = std::move(creation.key);
		record.id = record.world->Allocate();
		impl.Make(*record.world, record.id, creation.entity, record, *creation.shape);
		impl.records.emplace(creation.entity, std::move(record));
	}
	std::erase_if(impl.worlds, [](const auto &entry) { return entry.second->BodyCount() == 0; });
	for (const auto &[key, world] : impl.worlds)
		motionOf(key);

	// 3. What the game wrote since the last step, then what changes a dynamic body's velocity
	// besides: its system's gravity, from where the bodies are as the step begins (semi-implicit
	// Euler, as Jolt integrates, whose error over a low orbit's revolution is centimetres), less its
	// frame's change of speed since the last step. A frame on rails falls with its body, so beside a
	// station that leaves the tide, a few ten-thousandths of a metre a second squared.
	const auto step = static_cast<float>(fixedStep);
	std::map<std::uint64_t, std::vector<Well>> wells;
	for (auto &[entity, record] : impl.records)
	{
		JPH::BodyInterface &bodies = record.world->Bodies();
		const FrameMotion &motion = motions.at(record.key);
		const Sim::Transform &transform = registry.get<Sim::Transform>(entity);
		switch (record.body.kind)
		{
			case BodyKind::Static:
			case BodyKind::Dynamic:
				if (!Detail::SameTransform(transform, record.placed))
				{
					bodies.SetPositionAndRotation(
					    record.id, Detail::ToJolt(transform.position - motion.start),
					    Detail::ToJolt(transform.orientation).Normalized(),
					    record.body.kind == BodyKind::Static ? JPH::EActivation::DontActivate
					                                         : JPH::EActivation::Activate);
					record.placed = transform;
				}
				break;
			case BodyKind::Kinematic:
				bodies.MoveKinematic(
				    record.id, Detail::ToJolt(transform.position - motion.end),
				    Detail::ToJolt(transform.orientation).Normalized(), step);
				record.placed = transform;
				break;
		}
		if (record.body.kind != BodyKind::Dynamic)
			continue;

		// A velocity the game wrote is relative to the frame as the last step moved it, as the one
		// physics wrote back is: the flight model changes what it read.
		glm::dvec3 change = record.world->frameVelocity - motion.velocity;
		const Velocity &velocity = registry.get_or_emplace<Velocity>(entity);
		if (velocity != record.velocity)
		{
			bodies.SetLinearAndAngularVelocity(
			    record.id, Detail::ToJolt(velocity.linear), Detail::ToJolt(velocity.angular));
			record.velocity = velocity;
		}
		if (const auto system = impl.systems.find(record.key.system); system != impl.systems.end())
		{
			auto [found, made] = wells.try_emplace(record.key.system);
			if (made)
				found->second = Wells(system->second, simSeconds);
			change += Pull(found->second, record.placed.position) * fixedStep;
		}
		if (change != glm::dvec3{0.0})
			bodies.AddLinearVelocity(record.id, Detail::ToJolt(glm::vec3{change}));
	}
	for (const auto &[key, world] : impl.worlds)
	{
		const FrameMotion &motion = motions.at(key);
		world->frameOrigin = motion.end;
		world->frameVelocity = motion.velocity;
	}

	// 4. The step.
	for (const auto &[key, world] : impl.worlds)
	{
		const JPH::EPhysicsUpdateError errors =
		    world->System().Update(step, 1, &impl.scratch, &impl.jobs);
		if (errors != JPH::EPhysicsUpdateError::None && !impl.reportedUpdateError)
		{
			// Jolt carries on with what fitted, as deterministically as ever; the step's contacts
			// were incomplete, and the capacities are too small for this system.
			Core::Log(
			    Core::Severity::Warning, kPhysics,
			    "system {:#x} outgrew its capacities ({:#x}: manifolds 1, pairs 2, contacts 4); "
			    "said once",
			    key.system, static_cast<unsigned>(errors));
			impl.reportedUpdateError = true;
		}
	}

	// 5. Where it left the moving bodies: a kinematic body's Transform is already where it was
	// moved to, and its velocity is what moving it there took.
	for (auto &[entity, record] : impl.records)
	{
		if (record.body.kind == BodyKind::Static)
			continue;
		const JPH::BodyInterface &bodies = record.world->Bodies();
		record.velocity = {
		    Detail::ToGlmFloat(bodies.GetLinearVelocity(record.id)),
		    Detail::ToGlmFloat(bodies.GetAngularVelocity(record.id))};
		registry.get<Velocity>(entity) = record.velocity;
		if (record.body.kind != BodyKind::Dynamic)
			continue;
		JPH::RVec3 position;
		JPH::Quat rotation;
		bodies.GetPositionAndRotation(record.id, position, rotation);
		record.placed = {record.world->frameOrigin + Detail::ToGlm(position), Detail::ToGlm(rotation)};
		registry.get<Sim::Transform>(entity) = record.placed;
	}
}

void PhysicsWorld::Save(Serialise::BinaryWriter &writer) const
{
	const Impl &impl = *m_pImpl;
	std::map<WorldKey, std::vector<const std::pair<const entt::entity, Record> *>> byWorld;
	for (const auto &entry : impl.records)
		byWorld[entry.second.key].push_back(&entry);

	writer.WriteUnsigned(kLayout, 2);
	writer.WriteUnsigned(impl.worlds.size(), 4);
	for (const auto &[key, world] : impl.worlds)
	{
		writer.Write(key.system);
		writer.Write(key.frame);
		writer.Write(world->frameOrigin);
		writer.Write(world->frameVelocity);
		writer.Write(world->Sequences());
		const auto &bodies = byWorld.at(key);
		writer.WriteUnsigned(bodies.size(), 4);
		for (const auto *entry : bodies)
		{
			const Record &record = entry->second;
			writer.Write(entry->first);
			writer.WriteUnsigned(record.id.GetIndex(), 4);
			writer.Write(record.body);
			writer.Write(record.placed);
			writer.Write(record.velocity);
		}
		Detail::ByteRecorder recorder;
		world->System().SaveState(recorder);
		writer.Write(recorder.Bytes());
	}
}

std::function<void()> PhysicsWorld::Load(
    Serialise::BinaryReader &reader, const entt::registry & /*loaded*/)
{
	// The records may name entities the save no longer has, or whose Body, system or frame changed
	// after physics ran in the step the save was taken after: the next step deals with them as it
	// would have in the world saved, so they are read as they are.
	const Impl &impl = *m_pImpl;
	if (const std::uint64_t layout = reader.ReadUnsigned(2); layout != kLayout)
		reader.Fail(std::format("the physics state is layout {}, not {}", layout, kLayout));

	struct Pending
	{
		std::map<WorldKey, std::unique_ptr<Detail::StarSystemWorld>> worlds;
		std::map<entt::entity, Record> records;
	};
	auto pending = std::make_shared<Pending>();

	const auto worldCount = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
	for (std::uint32_t worldIndex = 0; worldIndex < worldCount; ++worldIndex)
	{
		WorldKey key;
		reader.Read(key.system);
		reader.Read(key.frame);
		if (key.system == Sim::kNoSystem ||
		    (!pending->worlds.empty() && key <= pending->worlds.rbegin()->first))
			reader.Fail(
			    std::format(
			        "the physics state's worlds are not in order at {:#x} '{}'", key.system,
			        key.frame));
		if (const std::optional<std::string> problem = impl.UnknownFrame(key))
			reader.Fail(std::format("a world {}", *problem));
		auto world = std::make_unique<Detail::StarSystemWorld>(impl.layers, impl.settings);
		reader.Read(world->frameOrigin);
		reader.Read(world->frameVelocity);
		std::vector<std::uint8_t> sequences;
		reader.Read(sequences);
		if (sequences.size() > impl.settings.maxBodies)
			reader.Fail(
			    std::format(
			        "system {:#x} has {} body identifiers, more than its {}", key.system,
			        sequences.size(), impl.settings.maxBodies));

		const auto bodyCount = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
		if (bodyCount == 0 || bodyCount > sequences.size())
			reader.Fail(
			    std::format(
			        "system {:#x} has {} bodies for {} identifiers", key.system, bodyCount,
			        sequences.size()));
		std::vector<std::uint32_t> held;
		std::vector<std::pair<entt::entity, const ShapeEntry *>> made;
		for (std::uint32_t bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex)
		{
			entt::entity entity = entt::null;
			reader.Read(entity);
			const auto index = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
			Record record;
			reader.Read(record.body);
			reader.Read(record.placed);
			reader.Read(record.velocity);
			if (index >= sequences.size() || std::ranges::find(held, index) != held.end())
				reader.Fail(
				    std::format("{} has body index {}, which is not free", Describe(entity), index));
			const ShapeEntry *shape = impl.FindShape(record.body.shape);
			if (const std::optional<std::string> problem = Unmakeable(record.body, shape, key))
				reader.Fail(std::format("{} cannot be a body: it {}", Describe(entity), *problem));
			record.key = key;
			record.id = JPH::BodyID{index, sequences[index]};
			record.world = world.get();
			if (!pending->records.emplace(entity, record).second)
				reader.Fail(std::format("{} is a body twice", Describe(entity)));
			held.push_back(index);
			made.emplace_back(entity, shape);
		}
		world->RestoreIdentifiers(std::move(sequences), held);
		for (const auto &[entity, shape] : made)
		{
			const Record &record = pending->records.at(entity);
			impl.Make(*world, record.id, entity, record, *shape);
		}

		std::vector<std::uint8_t> state;
		reader.Read(state);
		Detail::ByteRecorder recorder{std::move(state)};
		if (!world->System().RestoreState(recorder) || recorder.IsFailed() || !recorder.IsEOF())
			reader.Fail(
			    std::format(
			        "system {:#x} '{}''s Jolt state does not fit its bodies", key.system, key.frame));
		pending->worlds.emplace(std::move(key), std::move(world));
	}

	return [this, pending]
	{
		m_pImpl->records = std::move(pending->records);
		m_pImpl->worlds = std::move(pending->worlds);
	};
}
} // namespace Jettison::Physics
