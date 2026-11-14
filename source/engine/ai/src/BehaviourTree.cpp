#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/Types.h>

#include <jettison/core/Log.h>
#include <jettison/physics/Flight.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/spatial/SystemIndices.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Jettison::Ai
{
namespace
{
constexpr Core::Category kBehaviour{"behaviour"};

[[noreturn]] void Refuse(std::string_view name, std::string_view path, const std::string &problem)
{
	throw Serialise::FormatError{std::format("Behaviour '{}' has, at {}, {}", name, path, problem)};
}

std::string_view KindName(NodeKind kind)
{
	switch (kind)
	{
		case NodeKind::Sequence: return "a sequence";
		case NodeKind::Selector: return "a selector";
		case NodeKind::Invert: return "an invert";
		case NodeKind::Succeed: return "a succeed";
		case NodeKind::Repeat: return "a repeat";
		case NodeKind::Loop: return "a loop";
		case NodeKind::Wait: return "a wait";
		case NodeKind::Task: return "a task";
	}
	return "a node";
}

void CheckNode(std::string_view name, const BehaviourNode &node, const std::string &path)
{
	const std::string_view kind = KindName(node.kind);
	const auto refuse = [&](std::string_view problem)
	{ Refuse(name, path, std::format("{} {}", kind, problem)); };
	switch (node.kind)
	{
		case NodeKind::Sequence:
		case NodeKind::Selector:
			if (node.children.empty())
				refuse("with no children");
			break;
		case NodeKind::Invert:
		case NodeKind::Succeed:
		case NodeKind::Repeat:
		case NodeKind::Loop:
			if (node.children.size() != 1)
				refuse(std::format("with {} children, not one", node.children.size()));
			break;
		case NodeKind::Wait:
		case NodeKind::Task:
			if (!node.children.empty())
				refuse("with children");
			break;
	}
	if (node.kind == NodeKind::Task && node.task.empty())
		refuse("that names none");
	if (node.kind != NodeKind::Task && (!node.task.empty() || !node.arguments.empty()))
		refuse("with a task's name or arguments");
	if (node.kind != NodeKind::Repeat && node.count != 0)
		refuse("with a count, which only a repeat has");
	if (node.kind == NodeKind::Wait && (!std::isfinite(node.seconds) || node.seconds < 0.0))
		refuse(std::format("of {} seconds", node.seconds));
	if (node.kind != NodeKind::Wait && node.seconds != 0.0)
		refuse("with seconds, which only a wait has");
	for (std::size_t index = 0; index < node.children.size(); ++index)
		CheckNode(name, node.children[index], std::format("{}.{}", path, index));
}

// Lets the ship's controls go, from the helm or to it.
void LetGo(entt::registry &registry, entt::entity ship)
{
	if (auto *controls = registry.try_get<Physics::FlightControls>(ship))
		*controls = Physics::FlightControls{};
}

void Leave(Sim::SimWorld &world, entt::entity ship)
{
	entt::registry &registry = world.Registry();
	const Sim::Controller controller = registry.get<Sim::Helm>(ship).controller;
	registry.remove<Sim::Helm>(ship);
	LetGo(registry, ship);
	if (auto *behaviour = registry.try_get<Behaviour>(ship))
		behaviour->nodes.clear();
	world.Events().Emit(HelmLeft{controller, ship});
}

void Take(Sim::SimWorld &world, const TakeHelm &command)
{
	entt::registry &registry = world.Registry();
	const auto refuse = [&](Reason reason)
	{ world.Events().Emit(HelmRefused{command.sender, command.ship, Sim::MakeReason(reason)}); };
	if (!registry.valid(command.ship))
		return refuse(Reason::NoSuchEntity);
	if (command.sender.kind == Sim::ControllerKind::None)
		return refuse(Reason::NoPilot);
	if (!Sim::MayCommand(registry, command.ship, command.sender))
		return refuse(Reason::NotController);
	if (const auto *helm = registry.try_get<Sim::Helm>(command.ship);
	    helm && helm->controller != command.sender)
		return refuse(Reason::HelmHeld);
	// The helm it was at, in entity order, should a save hold more than one.
	std::vector<entt::entity> held;
	for (const auto [ship, helm] : registry.view<const Sim::Helm>().each())
		if (ship != command.ship && helm.controller == command.sender)
			held.push_back(ship);
	std::ranges::sort(held, {}, [](entt::entity ship) { return entt::to_integral(ship); });
	for (const entt::entity ship : held)
		Leave(world, ship);
	registry.emplace_or_replace<Sim::Helm>(command.ship, command.sender);
	registry.remove<Destination>(command.ship);
	LetGo(registry, command.ship);
	world.Events().Emit(HelmTaken{command.sender, command.ship});
}

void Leave(Sim::SimWorld &world, const LeaveHelm &command)
{
	const entt::registry &registry = world.Registry();
	if (!registry.valid(command.ship))
		return world.Events().Emit(
		    HelmRefused{command.sender, command.ship, Sim::MakeReason(Reason::NoSuchEntity)});
	const auto *helm = registry.try_get<Sim::Helm>(command.ship);
	if (!helm ||
	    (command.sender.kind != Sim::ControllerKind::None && helm->controller != command.sender))
		return world.Events().Emit(
		    HelmRefused{command.sender, command.ship, Sim::MakeReason(Reason::NotAtHelm)});
	Leave(world, command.ship);
}
} // namespace

void CheckBehaviour(std::string_view name, const BehaviourDefinition &behaviour)
{
	CheckNode(name, behaviour.root, "root");
}

Behaviours &Behaviours::Attach(Sim::SimWorld &world)
{
	RegisterTypes();
	// Shared by the system, which the world keeps for as long as it lives.
	std::shared_ptr<Behaviours> behaviours{new Behaviours{}};
	behaviours->m_fixedStep = world.FixedStep();
	world.RegisterComponent<Behaviour>();
	world.RegisterComponent<Sim::Helm>();
	// For whose helm a ship is at: the flight panel's, and which ship a player's camera follows.
	world.ShowInSnapshots<Sim::Helm>();
	world.RegisterCommand<TakeHelm>([](Sim::SimWorld &target, const TakeHelm &command)
	                                { Take(target, command); });
	world.RegisterCommand<LeaveHelm>([](Sim::SimWorld &target, const LeaveHelm &command)
	                                 { Leave(target, command); });
	Ecs::SystemDescription system{
	    std::string{kSystemName},
	    Ecs::TickPhase::Simulation,
	    Ecs::Access::Of<Sim::Helm>().Writing<Behaviour>(),
	    {},
	    [behaviours, &world](entt::registry & /*registry*/, double /*fixedStep*/)
	    { behaviours->Step(world); }};
	system.before = {std::string{Spatial::SystemIndices::kSystemName}};
	world.RegisterSystem(std::move(system));
	return *behaviours;
}

void Behaviours::DefineTask(std::string name, Task task)
{
	if (name.empty() || !task)
		throw std::invalid_argument{"Ai: a task needs a name and a body"};
	if (m_tasks.contains(name))
		throw std::invalid_argument{std::format("Ai: task '{}' is already defined", name)};
	m_tasks.emplace(std::move(name), std::move(task));
}

void Behaviours::DefineTree(std::string name, const BehaviourDefinition &tree)
{
	if (m_trees.contains(name))
		throw std::invalid_argument{std::format("Ai: behaviour '{}' is already defined", name)};
	CheckBehaviour(name, tree);
	Tree flat;
	Flatten(tree.root, flat, name, "root");
	m_trees.emplace(std::move(name), std::move(flat));
}

void Behaviours::Define(const Sim::DefinitionRegistry &definitions)
{
	for (const std::string &name : definitions.Names<BehaviourDefinition>())
		DefineTree(name, *definitions.Find<BehaviourDefinition>(name));
}

bool Behaviours::HasTree(std::string_view name) const
{
	return m_trees.contains(name);
}

std::size_t Behaviours::NodeCount(std::string_view tree) const
{
	const auto found = m_trees.find(tree);
	return found == m_trees.end() ? 0 : found->second.size();
}

void Behaviours::Flatten(
    const BehaviourNode &node, Tree &tree, std::string_view name, std::string path) const
{
	const auto index = static_cast<std::uint32_t>(tree.size());
	Node flat{node.kind, nullptr, node.arguments, node.count, 0, {}};
	if (node.kind == NodeKind::Task)
	{
		const auto task = m_tasks.find(node.task);
		if (task == m_tasks.end())
			Refuse(name, path, std::format("the task '{}', which is not defined", node.task));
		flat.task = &task->second;
	}
	// Whole steps, rounded up, as a lane's transit is; a little under a whole number of steps is
	// that number, not one more.
	if (node.kind == NodeKind::Wait)
		flat.steps = static_cast<std::uint64_t>(std::ceil(node.seconds / m_fixedStep - 1e-9));
	tree.push_back(std::move(flat));
	for (std::size_t child = 0; child < node.children.size(); ++child)
	{
		tree[index].children.push_back(static_cast<std::uint32_t>(tree.size()));
		Flatten(node.children[child], tree, name, std::format("{}.{}", path, child));
	}
}

Status Behaviours::Tick(Sim::SimWorld &world, entt::entity entity)
{
	entt::registry &registry = world.Registry();
	auto *behaviour = registry.try_get<Behaviour>(entity);
	if (!behaviour)
		return Status::Failure;
	const auto found = m_trees.find(behaviour->tree);
	if (found == m_trees.end())
	{
		if (m_missing.insert(behaviour->tree).second)
			Core::Log(
			    Core::Severity::Warning, kBehaviour,
			    "Entity {:#x} follows the behaviour '{}', which is not defined",
			    entt::to_integral(entity), behaviour->tree);
		return Status::Failure;
	}
	const Tree &tree = found->second;
	// Its own copy while it is ticked: a task may add or take components, the Behaviour among them.
	std::vector<NodeState> states = std::move(behaviour->nodes);
	if (states.size() != tree.size())
		states.assign(tree.size(), NodeState{});
	const Status status = TickNode(world, entity, tree, states, 0);
	if (auto *after = registry.try_get<Behaviour>(entity); after && after->tree == found->first)
	{
		after->nodes = std::move(states);
		after->last = status;
	}
	return status;
}

void Behaviours::Step(Sim::SimWorld &world)
{
	entt::registry &registry = world.Registry();
	std::vector<entt::entity> entities;
	for (const auto [entity, behaviour] :
	     registry.view<const Behaviour>(entt::exclude<Sim::Helm>).each())
		entities.push_back(entity);
	std::ranges::sort(entities, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	for (const entt::entity entity : entities)
		if (registry.valid(entity))
			Tick(world, entity);
}

Status Behaviours::TickNode(
    Sim::SimWorld &world, entt::entity entity, const Tree &tree, std::vector<NodeState> &states,
    std::uint32_t index) const
{
	const Node &node = tree[index];
	const bool starting = !states[index].running;
	if (starting)
		states[index].memory = 0;
	const auto child = [&](std::size_t which)
	{ return TickNode(world, entity, tree, states, node.children[which]); };

	Status status = Status::Running;
	switch (node.kind)
	{
		case NodeKind::Sequence:
		case NodeKind::Selector:
		{
			// A sequence goes on past a success and a selector past a failure.
			const Status onward = node.kind == NodeKind::Sequence ? Status::Success : Status::Failure;
			status = onward;
			for (auto next = static_cast<std::size_t>(states[index].memory);
			     next < node.children.size(); ++next)
			{
				const Status result = child(next);
				if (result == onward)
					continue;
				states[index].memory = next;
				status = result;
				break;
			}
			break;
		}
		case NodeKind::Invert:
			status = child(0);
			if (status != Status::Running)
				status = status == Status::Success ? Status::Failure : Status::Success;
			break;
		case NodeKind::Succeed:
			status = child(0) == Status::Running ? Status::Running : Status::Success;
			break;
		case NodeKind::Repeat:
			status = child(0);
			// The next run starts at the next tick, so a child that succeeds at once is not run without
			// end in one step.
			if (status == Status::Success)
				status = node.count != 0 && ++states[index].memory >= node.count ? Status::Success
				                                                                 : Status::Running;
			break;
		case NodeKind::Loop:
			status = child(0);
			if (status != Status::Running)
				status = status == Status::Success ? Status::Running : Status::Success;
			break;
		case NodeKind::Wait:
			// Ends once the clock has counted its steps from the one it started in.
			if (starting)
				states[index].memory = world.Clock().Step() + node.steps;
			status = world.Clock().Step() >= states[index].memory ? Status::Success : Status::Running;
			break;
		case NodeKind::Task:
		{
			TaskContext context{world, entity, node.arguments, starting, states[index].memory};
			status = (*node.task)(context);
			break;
		}
	}
	states[index].running = status == Status::Running;
	return status;
}
} // namespace Jettison::Ai
