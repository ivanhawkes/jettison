#include <jettison/sim/SimWorld.h>

#include <jettison/core/Hash.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Binary.h>
#include <jettison/serialise/Migration.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Jettison::Sim
{
namespace
{
constexpr std::string_view kFormat = "Jettison.World";

// The layout Save writes. Until player testing a change to it, or to what the world saves around its
// components, changes it in place, and the saves the repositories hold are written again
// (docs/decisions.md, "Formats change in place until player testing"); the version stays.
constexpr Serialise::FormatVersion kVersion{5};

// The entity storage as a save holds it: how many entities, how many of them are in use, then each.
// EnTT's loader takes the second as where its free list starts, and asserts it is within the first,
// and makes each entity it is given; the save fuzz target found a count past the length
// (sparse_set.hpp, free_list, 2026-11-28), so both, and each entity once, are checked here.
struct EntityInput : Detail::SnapshotInput
{
	using SnapshotInput::operator();

	std::optional<std::uint32_t> length;
	std::set<entt::id_type> seen;


	void operator()(std::uint32_t &count)
	{
		SnapshotInput::operator()(count);
		if (!length)
			length = count;
		else if (count > *length)
			reader.Fail(std::format("{} entities are in use of the {} the save holds", count, *length));
	}


	void operator()(entt::entity &entity)
	{
		SnapshotInput::operator()(entity);
		if (entity == entt::null || !seen.insert(entt::to_entity(entity)).second)
			reader.Fail(
			    std::format(
			        "the save holds entity {} twice, or as no entity", entt::to_integral(entity)));
	}
};


const Serialise::BinaryMigrations &Migrations()
{
	static const Serialise::BinaryMigrations migrations{std::string{kFormat}, kVersion};
	return migrations;
}
} // namespace


std::string_view Detail::ReflectedName(const entt::meta_type &type, std::string_view what)
{
	const entt::meta_type registered = type ? entt::resolve(type.info()) : entt::meta_type{};
	if (!registered)
		throw std::logic_error{std::format(
		    "Sim: the {} type '{}' is not registered with Reflect", what,
		    type ? type.info().name() : std::string_view{"(none)"})};
	return registered.name();
}


SimWorld::SimWorld(double fixedStep, std::uint64_t seed) : m_fixedStep{fixedStep}
{
	if (!std::isfinite(fixedStep) || fixedStep <= 0.0)
		throw std::invalid_argument{
		    std::format("SimWorld: a fixed step of {} s is not a step", fixedStep)};
	m_registry.ctx().emplace<Math::Random>(seed);
	m_registry.ctx().emplace<EventQueue>();
}


Math::Random &SimWorld::Random()
{
	return m_registry.ctx().get<Math::Random>();
}


EventQueue &SimWorld::Events()
{
	return m_registry.ctx().get<EventQueue>();
}


void SimWorld::AddComponent(const entt::meta_type &type, Component component)
{
	component.name = std::string{Detail::ReflectedName(type, "component")};
	if (std::ranges::find(m_components, component.name, &Component::name) != m_components.end())
		throw std::logic_error{
		    std::format("SimWorld: component '{}' is already registered", component.name)};
	if (component.migrations.Format() != component.name)
		throw std::logic_error{std::format(
		    "SimWorld: component '{}' is given the migrations of '{}'", component.name,
		    component.migrations.Format())};
	m_components.push_back(std::move(component));
}


void SimWorld::AddShown(ShownComponent shown)
{
	if (Shows(shown.type))
		throw std::logic_error("SimWorld: a component is shown in snapshots twice");
	m_shown.push_back(std::move(shown));
}


bool SimWorld::Shows(entt::id_type type) const
{
	return std::ranges::any_of(
	    m_shown, [type](const ShownComponent &shown) { return shown.type == type; });
}


const void *SimWorld::FindShown(const Snapshot &snapshot, entt::id_type type, entt::entity entity) const
{
	const auto found = std::ranges::find_if(
	    m_shown, [type](const ShownComponent &shown) { return shown.type == type; });
	return found != m_shown.end() ? found->find(snapshot, entity) : nullptr;
}


void SimWorld::AddCommand(const entt::meta_type &type, Applier apply)
{
	const std::string_view name = Detail::ReflectedName(type, "command");
	if (!m_applies.try_emplace(type.info().hash(), std::move(apply)).second)
		throw std::logic_error{std::format("SimWorld: command '{}' is already registered", name)};
}


void SimWorld::RegisterSystem(Ecs::SystemDescription system, Quiet quiet)
{
	if (system.phase != Ecs::TickPhase::Simulation)
		throw std::logic_error{
		    std::format("SimWorld: system '{}' is not a simulation-phase system", system.name)};
	std::string name = system.name;
	m_scheduler.Register(std::move(system));
	if (quiet.next)
		m_quiets.insert_or_assign(std::move(name), std::move(quiet));
	m_quietOrder.clear();
}


const std::vector<const Quiet *> &SimWorld::QuietInOrder()
{
	if (m_quietOrder.empty())
	{
		// As Step's first Run does: an after or a before naming no system throws here too.
		m_scheduler.Validate();
		for (const std::string &name : m_scheduler.Order(Ecs::TickPhase::Simulation))
		{
			const auto found = m_quiets.find(name);
			m_quietOrder.push_back(found == m_quiets.end() ? nullptr : &found->second);
		}
	}
	return m_quietOrder;
}


WorldService &SimWorld::AddService(std::string name, std::unique_ptr<WorldService> service)
{
	if (!service)
		throw std::logic_error{std::format("SimWorld: service '{}' is null", name)};
	if (std::ranges::find(m_services, name, &Service::name) != m_services.end())
		throw std::logic_error{std::format("SimWorld: service '{}' is already registered", name)};
	m_services.push_back({std::move(name), std::move(service)});
	return *m_services.back().service;
}


std::vector<std::string> SimWorld::ComponentNames() const
{
	std::vector<std::string> names;
	names.reserve(m_components.size());
	for (const Component &component : m_components)
		names.emplace_back(component.name);
	return names;
}


std::vector<std::string> SimWorld::ServiceNames() const
{
	std::vector<std::string> names;
	names.reserve(m_services.size());
	for (const Service &service : m_services)
		names.emplace_back(service.name);
	return names;
}


WorldService *SimWorld::FindService(std::string_view name)
{
	const auto found = std::ranges::find(m_services, name, &Service::name);
	return found == m_services.end() ? nullptr : found->service.get();
}


void SimWorld::Step()
{
	// Checked before any is applied, so a step either applies all its commands or none.
	for (const entt::meta_any &command : m_commands.Pending())
	{
		if (!m_applies.contains(command.type().info().hash()))
			throw std::logic_error{std::format(
			    "SimWorld: no handler is registered for command '{}'", command.type().info().name())};
	}

	const std::uint64_t step = m_clock.Step();
	Events().m_step = step;
	for (entt::meta_any &command : m_commands.Drain())
	{
		m_applies.at(command.type().info().hash())(*this, command);
		if (m_recording)
			m_recorded.push_back({step, std::move(command)});
	}
	m_scheduler.Run(Ecs::TickPhase::Simulation, m_registry, m_fixedStep);
	m_clock.Advance(m_fixedStep);
}


std::uint64_t SimWorld::FastForward(std::uint64_t until)
{
	const std::uint64_t from = m_clock.Step();
	if (until <= from)
		return 0;
	std::uint64_t to = m_commands.Pending().empty() ? until : from;
	for (const Quiet *quiet : QuietInOrder())
	{
		if (to <= from)
			break;
		to = quiet ? std::min(to, quiet->next(*this)) : from;
	}
	if (to <= from)
	{
		Step();
		return 1;
	}

	// The clock's time is the sum of the steps, as stepping makes it.
	double lastBegan = m_clock.Now();
	for (std::uint64_t step = from; step < to; ++step)
	{
		lastBegan = m_clock.Now();
		m_clock.Advance(m_fixedStep);
	}
	for (const Quiet *quiet : QuietInOrder())
		if (quiet && quiet->skip)
			quiet->skip(*this, to - from, lastBegan);
	return to - from;
}


void SimWorld::Apply(const entt::meta_any &command)
{
	const auto found = m_applies.find(command.type().info().hash());
	if (found == m_applies.end())
		throw std::logic_error{std::format(
		    "SimWorld: no handler is registered for command '{}'", command.type().info().name())};
	found->second(*this, command);
}


bool SimWorld::Accepts(const entt::meta_type &type) const
{
	return type && m_applies.contains(type.info().hash());
}


std::vector<std::byte> SimWorld::Save() const
{
	Serialise::BinaryWriter writer{kFormat};

	// First, so a load knows every storage's version before it reads one. Each is written, at 1 too,
	// so a save says what it holds without a reader knowing the default.
	writer.WriteUnsigned(m_components.size(), 4);
	for (const Component &component : m_components)
	{
		writer.WriteName(component.name);
		writer.WriteUnsigned(component.migrations.Current().version, 4);
	}
	writer.Write(m_clock.Step());
	writer.Write(m_clock.Now());
	const Math::RandomState random = m_registry.ctx().get<Math::Random>().State();
	writer.Write(random.state);
	writer.Write(random.increment);

	const std::vector<entt::meta_any> &pending = m_commands.Pending();
	writer.WriteUnsigned(pending.size(), 4);
	for (const entt::meta_any &command : pending)
	{
		writer.WriteName(Detail::ReflectedName(command.type(), "command"));
		writer.WriteValue(command);
	}

	Detail::SnapshotOutput output{writer};
	entt::snapshot{m_registry}.get<entt::entity>(output);
	writer.WriteUnsigned(m_components.size(), 4);
	for (const Component &component : m_components)
	{
		writer.WriteName(component.name);
		component.save(m_registry, writer);
	}
	writer.WriteUnsigned(m_services.size(), 4);
	for (const Service &service : m_services)
	{
		writer.WriteName(service.name);
		service.service->Save(writer);
	}
	return writer.Finish(kVersion);
}


void SimWorld::Load(std::span<const std::byte> save)
{
	// Everything is read into locals and committed at the end, so a save that does not fit
	// leaves the world as it was. A save of another version is refused.
	Serialise::BinaryReader reader{save, Migrations()};

	// Every storage's version is checked against its component's chain here, before anything else is
	// read, so a component of a later build is refused by name.
	std::vector<std::pair<std::string_view, std::uint32_t>> versions;
	const auto versionCount = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
	if (versionCount > reader.Remaining() / 8)
		reader.Fail("the count of component versions is more than the data holds");
	for (std::uint32_t index = 0; index < versionCount; ++index)
	{
		const std::string_view name = reader.ReadName();
		const auto version = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
		if (std::ranges::find(versions, name, &std::pair<std::string_view, std::uint32_t>::first) !=
		    versions.end())
			reader.Fail(std::format("the save gives component '{}' a version twice", name));

		// A component the world does not register is refused where its storage is.
		if (const auto component = std::ranges::find(m_components, name, &Component::name);
		    component != m_components.end())
			component->migrations.Check(version);
		versions.emplace_back(name, version);
	}
	std::uint64_t step = 0;
	double now = 0.0;
	Math::RandomState random;
	reader.Read(step);
	reader.Read(now);
	reader.Read(random.state);
	reader.Read(random.increment);
	if (!std::isfinite(now) || now < 0.0)
		reader.Fail(std::format("the time {} is not a simulation time", now));

	std::vector<entt::meta_any> pending;
	const auto commandCount = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
	for (std::uint32_t index = 0; index < commandCount; ++index)
	{
		const std::string_view name = reader.ReadName();
		const entt::meta_type type = Reflect::Resolve(name);
		if (!type || !m_applies.contains(type.info().hash()))
			reader.Fail(
			    std::format("the save queues a command '{}', which this world does not apply", name));
		entt::meta_any command = type.construct();
		reader.ReadValue(command);
		pending.push_back(std::move(command));
	}

	entt::registry registry;
	entt::snapshot_loader loader{registry};
	EntityInput entities{{reader, nullptr}, {}, {}};
	loader.get<entt::entity>(entities);
	const auto componentCount = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
	std::vector<std::string_view> seen;
	for (std::uint32_t index = 0; index < componentCount; ++index)
	{
		const std::string_view name = reader.ReadName();
		const auto component = std::ranges::find(m_components, name, &Component::name);
		if (component == m_components.end())
			reader.Fail(
			    std::format("the save holds component '{}', which this world does not register", name));
		if (std::ranges::find(seen, name) != seen.end())
			reader.Fail(std::format("the save holds component '{}' twice", name));
		seen.push_back(name);
		const auto version =
		    std::ranges::find(versions, name, &std::pair<std::string_view, std::uint32_t>::first);
		component->load(
		    loader, registry, reader, component->migrations,
		    version == versions.end() ? 1 : version->second);
	}

	// A service reads its state against the registry and the queue it will have, and keeps it aside
	// until they are committed.
	std::vector<std::function<void()>> commits(m_services.size());
	const auto serviceCount = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
	for (std::uint32_t index = 0; index < serviceCount; ++index)
	{
		const std::string_view name = reader.ReadName();
		const auto service = std::ranges::find(m_services, name, &Service::name);
		if (service == m_services.end())
			reader.Fail(
			    std::format("the save holds service '{}', which this world does not have", name));
		auto &commit = commits[static_cast<std::size_t>(service - m_services.begin())];
		if (commit)
			reader.Fail(std::format("the save holds service '{}' twice", name));
		commit = service->service->Load(reader, registry, pending);
		if (!commit)
			reader.Fail(std::format("service '{}' read its state and gave nothing to commit", name));
	}
	for (std::size_t index = 0; index < m_services.size(); ++index)
	{
		if (!commits[index])
			reader.Fail(std::format("the save has no state for service '{}'", m_services[index].name));
	}
	reader.End();

	registry.ctx().emplace<Math::Random>(Math::Random::FromState(random));

	// The events raised so far belong to the state being replaced.
	registry.ctx().emplace<EventQueue>();
	m_registry = std::move(registry);
	m_clock.Restore(step, now);
	m_commands = CommandQueue{};
	for (entt::meta_any &command : pending)
		m_commands.Push(std::move(command));
	for (const std::function<void()> &commit : commits)
		commit();

	// What was recorded led to the state just replaced.
	m_recorded.clear();
}


std::uint64_t SimWorld::StateHash() const
{
	const std::vector<std::byte> save = Save();
	return Core::Fnv1a64({reinterpret_cast<const char *>(save.data()), save.size()});
}
} // namespace Jettison::Sim
