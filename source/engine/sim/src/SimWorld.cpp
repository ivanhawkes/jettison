#include <jettison/sim/SimWorld.h>

#include <jettison/core/Hash.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Migration.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <utility>

namespace Jettison::Sim
{
namespace
{
constexpr std::string_view kFormat = "Jettison.World";
// The layout Save writes. A change to it, or to what the world saves around its components, is a
// new version, with a step in Migrations from the one before; a change to a component's fields is
// the component's own business, since fields are found by name.
constexpr Serialise::FormatVersion kVersion{2};

const Serialise::BinaryMigrations &Migrations()
{
	static const Serialise::BinaryMigrations migrations = []
	{
		Serialise::BinaryMigrations chain{std::string{kFormat}, kVersion};
		// Version 2 (phase 7a) added the services' state after the components, as a count and each
		// service's; a world of version 1 had none.
		chain.Add(
		    1, [](Serialise::BinaryDocument &document)
		    { document.payload.insert(document.payload.end(), 4, std::byte{0}); });
		return chain;
	}();
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
	m_components.push_back(std::move(component));
}

void SimWorld::AddShown(entt::id_type type, ShownCapture capture)
{
	if (std::ranges::any_of(m_shown, [type](const auto &shown) { return shown.first == type; }))
		throw std::logic_error("SimWorld: a component is shown in snapshots twice");
	m_shown.emplace_back(type, std::move(capture));
}

void SimWorld::AddCommand(const entt::meta_type &type, Applier apply)
{
	const std::string_view name = Detail::ReflectedName(type, "command");
	if (!m_applies.try_emplace(type.info().hash(), std::move(apply)).second)
		throw std::logic_error{std::format("SimWorld: command '{}' is already registered", name)};
}

void SimWorld::RegisterSystem(Ecs::SystemDescription system)
{
	if (system.phase != Ecs::TickPhase::Simulation)
		throw std::logic_error{
		    std::format("SimWorld: system '{}' is not a simulation-phase system", system.name)};
	m_scheduler.Register(std::move(system));
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

void SimWorld::Apply(const entt::meta_any &command)
{
	const auto found = m_applies.find(command.type().info().hash());
	if (found == m_applies.end())
		throw std::logic_error{std::format(
		    "SimWorld: no handler is registered for command '{}'", command.type().info().name())};
	found->second(*this, command);
}

std::vector<std::byte> SimWorld::Save() const
{
	Serialise::BinaryWriter writer{kFormat};
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
	// leaves the world as it was. A save of an earlier version is migrated first.
	Serialise::BinaryReader reader{save, Migrations()};
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
	Detail::SnapshotInput input{reader};
	loader.get<entt::entity>(input);
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
		component->load(loader, reader);
	}

	// A service reads its state against the registry it will have, and keeps it aside until the
	// registry is committed.
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
		commit = service->service->Load(reader, registry);
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
