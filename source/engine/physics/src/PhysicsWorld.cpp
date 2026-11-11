#include <jettison/physics/PhysicsWorld.h>

#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <stdexcept>
#include <utility>

#include <jettison/core/Assert.h>
#include <jettison/core/Log.h>
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
// world's own version does not see.
constexpr std::uint64_t kLayout = 1;

// Jolt's scratch memory for a step: what a step of a busy system needs, with the heap behind it
// for one that needs more, rather than Jolt's fixed block failing.
constexpr JPH::uint kTempAllocatorBytes = 8u * 1024u * 1024u;

struct ShapeEntry
{
	JPH::RefConst<JPH::Shape> shape;
	bool mesh{false};
};

// A body as physics made it. Saved, but for the world, which is the system's and found again.
struct Record
{
	std::uint64_t system{Sim::kNoSystem};
	JPH::BodyID id;
	// What the body was made from, so a change to the entity's Body is seen.
	Body body;
	// Where physics last put the body, or last saw the game put it: a Transform that differs was
	// written by the game. Exact, and compared with what the game wrote rather than with Jolt's
	// position, which comes back through the body's centre of mass and need not round to it.
	Sim::Transform placed;
	// The same, for a dynamic body's velocity.
	Velocity velocity;
	Detail::StarSystemWorld *world{nullptr};
};

std::string Describe(entt::entity entity)
{
	return std::format("entity {:#x}", entt::to_integral(entity));
}

// Why a body of this description cannot be made, or nothing.
std::optional<std::string> Unmakeable(const Body &body, const ShapeEntry *shape)
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
	return std::nullopt;
}

JPH::BodyCreationSettings MakeSettings(
    entt::entity entity, const Body &body, const ShapeEntry &shape, const Sim::Transform &transform,
    const Velocity &velocity, const PhysicsSettings &settings)
{
	static constexpr JPH::EMotionType kMotion[] = {
	    JPH::EMotionType::Static, JPH::EMotionType::Kinematic, JPH::EMotionType::Dynamic};
	JPH::BodyCreationSettings creation{
	    shape.shape, Detail::ToJolt(transform.position),
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
			break;
	}
	return creation;
}

std::uint64_t SystemOf(const entt::registry &registry, entt::entity entity)
{
	const auto *inSystem = registry.try_get<Sim::InSystem>(entity);
	return inSystem ? inSystem->system : Sim::kNoSystem;
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

	Detail::StarSystemWorld &World(std::uint64_t system)
	{
		std::unique_ptr<Detail::StarSystemWorld> &world = worlds[system];
		if (!world)
			world = std::make_unique<Detail::StarSystemWorld>(layers, settings);
		return *world;
	}

	// Makes the body in its world, with the identifier the world gives it or, from a save, the one
	// it had. Its record is the caller's to keep.
	void Make(
	    Detail::StarSystemWorld &world, const JPH::BodyID &id, entt::entity entity,
	    const Record &record, const ShapeEntry &shape) const
	{
		const JPH::BodyCreationSettings creation =
		    MakeSettings(entity, record.body, shape, record.placed, record.velocity, settings);
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
	std::map<std::uint64_t, std::unique_ptr<Detail::StarSystemWorld>> worlds;
	// Ordered by entity, which is the order every step visits bodies in.
	std::map<entt::entity, Record> records;
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
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Body, Sim::InSystem>().Writing<Sim::Transform, Velocity>(),
	     {},
	     [&physics](entt::registry &registry, double fixedStep)
	     { physics.Step(registry, fixedStep); }});
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

std::size_t PhysicsWorld::BodyCount() const
{
	return m_pImpl->records.size();
}

std::size_t PhysicsWorld::BodyCount(std::uint64_t system) const
{
	const auto found = m_pImpl->worlds.find(system);
	return found == m_pImpl->worlds.end() ? 0 : found->second->BodyCount();
}

std::vector<std::uint64_t> PhysicsWorld::Systems() const
{
	std::vector<std::uint64_t> systems;
	systems.reserve(m_pImpl->worlds.size());
	for (const auto &[system, world] : m_pImpl->worlds)
		systems.push_back(system);
	return systems;
}

std::optional<RayHit> PhysicsWorld::CastRay(
    std::uint64_t /*unused*/, const glm::dvec3 & /*unused*/, const glm::dvec3 & /*unused*/,
    double /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}

void PhysicsWorld::Step(entt::registry &registry, double fixedStep)
{
	JETTISON_PROFILE_ZONE("Physics::Step");
	Impl &impl = *m_pImpl;

	// 1. What changes: the bodies whose entity no longer makes them what they were, then the
	// entities that should have a body and will not, every one checked before anything changes.
	std::vector<entt::entity> removals;
	for (const auto &[entity, record] : impl.records)
	{
		const Body *body = registry.valid(entity) ? registry.try_get<Body>(entity) : nullptr;
		if (!body || !registry.all_of<Sim::Transform>(entity) ||
		    SystemOf(registry, entity) != record.system || *body != record.body)
			removals.push_back(entity);
	}
	struct Creation
	{
		entt::entity entity;
		std::uint64_t system;
		const ShapeEntry *shape;
	};
	std::vector<Creation> creations;
	const auto candidates = registry.view<const Body, const Sim::Transform>();
	for (const entt::entity entity : candidates)
	{
		const Body &body = candidates.get<const Body>(entity);
		const std::uint64_t system = SystemOf(registry, entity);
		if (system == Sim::kNoSystem ||
		    (impl.records.contains(entity) && !std::ranges::binary_search(removals, entity)))
			continue;
		const ShapeEntry *shape = impl.FindShape(body.shape);
		if (const std::optional<std::string> problem = Unmakeable(body, shape))
			throw std::invalid_argument{
			    std::format("Physics: {} cannot be a body: it {}", Describe(entity), *problem)};
		creations.push_back({entity, system, shape});
	}
	std::ranges::sort(creations, std::less{}, &Creation::entity);
	{
		std::map<std::uint64_t, std::size_t> counts;
		for (const auto &[system, world] : impl.worlds)
			counts[system] = world->BodyCount();
		for (const entt::entity entity : removals)
			--counts[impl.records.at(entity).system];
		for (const Creation &creation : creations)
		{
			if (++counts[creation.system] > impl.settings.maxBodies)
				throw std::invalid_argument{std::format(
				    "Physics: {} cannot be a body: system {:#x} has its {} already",
				    Describe(creation.entity), creation.system, impl.settings.maxBodies)};
		}
	}

	for (const entt::entity entity : removals)
	{
		const auto found = impl.records.find(entity);
		Detail::StarSystemWorld &world = *found->second.world;
		world.Bodies().RemoveBody(found->second.id);
		world.Bodies().DestroyBody(found->second.id);
		world.Release(found->second.id);
		impl.records.erase(found);
	}
	for (const Creation &creation : creations)
	{
		Record record;
		record.system = creation.system;
		record.body = registry.get<Body>(creation.entity);
		record.placed = registry.get<Sim::Transform>(creation.entity);
		if (record.body.kind == BodyKind::Dynamic)
			record.velocity = registry.get_or_emplace<Velocity>(creation.entity);
		record.world = &impl.World(creation.system);
		record.id = record.world->Allocate();
		impl.Make(*record.world, record.id, creation.entity, record, *creation.shape);
		impl.records.emplace(creation.entity, std::move(record));
	}
	std::erase_if(impl.worlds, [](const auto &entry) { return entry.second->BodyCount() == 0; });

	// 2. What the game wrote since the last step.
	const auto step = static_cast<float>(fixedStep);
	for (auto &[entity, record] : impl.records)
	{
		JPH::BodyInterface &bodies = record.world->Bodies();
		const Sim::Transform &transform = registry.get<Sim::Transform>(entity);
		switch (record.body.kind)
		{
			case BodyKind::Static:
			case BodyKind::Dynamic:
				if (!Detail::SameTransform(transform, record.placed))
				{
					bodies.SetPositionAndRotation(
					    record.id, Detail::ToJolt(transform.position),
					    Detail::ToJolt(transform.orientation).Normalized(),
					    record.body.kind == BodyKind::Static ? JPH::EActivation::DontActivate
					                                         : JPH::EActivation::Activate);
					record.placed = transform;
				}
				break;
			case BodyKind::Kinematic:
				bodies.MoveKinematic(
				    record.id, Detail::ToJolt(transform.position),
				    Detail::ToJolt(transform.orientation).Normalized(), step);
				record.placed = transform;
				break;
		}
		if (record.body.kind == BodyKind::Dynamic)
		{
			const Velocity &velocity = registry.get_or_emplace<Velocity>(entity);
			if (velocity != record.velocity)
			{
				bodies.SetLinearAndAngularVelocity(
				    record.id, Detail::ToJolt(velocity.linear), Detail::ToJolt(velocity.angular));
				record.velocity = velocity;
			}
		}
	}

	// 3. The step.
	for (const auto &[system, world] : impl.worlds)
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
			    system, static_cast<unsigned>(errors));
			impl.reportedUpdateError = true;
		}
	}

	// 4. Where it left the dynamic bodies.
	for (auto &[entity, record] : impl.records)
	{
		if (record.body.kind != BodyKind::Dynamic)
			continue;
		const JPH::BodyInterface &bodies = record.world->Bodies();
		JPH::RVec3 position;
		JPH::Quat rotation;
		bodies.GetPositionAndRotation(record.id, position, rotation);
		record.placed = {Detail::ToGlm(position), Detail::ToGlm(rotation)};
		record.velocity = {
		    Detail::ToGlmFloat(bodies.GetLinearVelocity(record.id)),
		    Detail::ToGlmFloat(bodies.GetAngularVelocity(record.id))};
		registry.get<Sim::Transform>(entity) = record.placed;
		registry.get<Velocity>(entity) = record.velocity;
	}
}

void PhysicsWorld::Save(Serialise::BinaryWriter &writer) const
{
	const Impl &impl = *m_pImpl;
	std::map<std::uint64_t, std::vector<const std::pair<const entt::entity, Record> *>> bySystem;
	for (const auto &entry : impl.records)
		bySystem[entry.second.system].push_back(&entry);

	writer.WriteUnsigned(kLayout, 2);
	writer.WriteUnsigned(impl.worlds.size(), 4);
	for (const auto &[system, world] : impl.worlds)
	{
		writer.Write(system);
		writer.Write(world->Sequences());
		const auto &bodies = bySystem.at(system);
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
	// The records may name entities the save no longer has, or whose Body or system changed after
	// physics ran in the step the save was taken after: the next step deals with them as it would
	// have in the world saved, so they are read as they are.
	const Impl &impl = *m_pImpl;
	if (const std::uint64_t layout = reader.ReadUnsigned(2); layout != kLayout)
		reader.Fail(std::format("the physics state is layout {}, not {}", layout, kLayout));

	struct Pending
	{
		std::map<std::uint64_t, std::unique_ptr<Detail::StarSystemWorld>> worlds;
		std::map<entt::entity, Record> records;
	};
	auto pending = std::make_shared<Pending>();

	const auto worldCount = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
	for (std::uint32_t worldIndex = 0; worldIndex < worldCount; ++worldIndex)
	{
		std::uint64_t system = 0;
		reader.Read(system);
		if (system == Sim::kNoSystem ||
		    (!pending->worlds.empty() && system <= pending->worlds.rbegin()->first))
			reader.Fail(std::format("the physics state's systems are not in order at {:#x}", system));
		std::vector<std::uint8_t> sequences;
		reader.Read(sequences);
		if (sequences.size() > impl.settings.maxBodies)
			reader.Fail(
			    std::format(
			        "system {:#x} has {} body identifiers, more than its {}", system, sequences.size(),
			        impl.settings.maxBodies));

		const auto bodyCount = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
		if (bodyCount == 0 || bodyCount > sequences.size())
			reader.Fail(
			    std::format(
			        "system {:#x} has {} bodies for {} identifiers", system, bodyCount,
			        sequences.size()));
		auto world = std::make_unique<Detail::StarSystemWorld>(impl.layers, impl.settings);
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
			if (const std::optional<std::string> problem = Unmakeable(record.body, shape))
				reader.Fail(std::format("{} cannot be a body: it {}", Describe(entity), *problem));
			record.system = system;
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
			reader.Fail(std::format("system {:#x}'s Jolt state does not fit its bodies", system));
		pending->worlds.emplace(system, std::move(world));
	}

	return [this, pending]
	{
		m_pImpl->records = std::move(pending->records);
		m_pImpl->worlds = std::move(pending->worlds);
	};
}
} // namespace Jettison::Physics
