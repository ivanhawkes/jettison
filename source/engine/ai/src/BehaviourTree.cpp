#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/Types.h>

#include <jettison/core/Assert.h>
#include <jettison/core/Log.h>
#include <jettison/physics/Flight.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/spatial/FrameIndices.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace Jettison::Ai
{
namespace
{
constexpr Core::Category kBehaviour{"behaviour"};

// A parallel's memory: a bit for each child that has finished, and above them one for each that
// succeeded.
constexpr std::uint64_t kParallelFinished = (std::uint64_t{1} << kParallelChildren) - 1;


// The most steps a wait or an interval may be: more than any run has, and every count up to it a
// double holds exactly, so the conversion is defined.
constexpr double kMostSteps = 9007199254740992.0;


// Whole steps, rounded up, as a portal's transit is; a little under a whole number of steps is that
// number, not one more. Nothing for a span of more than kMostSteps.
std::optional<std::uint64_t> WholeSteps(double seconds, double fixedStep)
{
	const double steps = std::ceil(seconds / fixedStep - 1e-9);
	if (!(steps <= kMostSteps))
		return std::nullopt;
	return static_cast<std::uint64_t>(std::max(steps, 0.0));
}


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
		case NodeKind::ReactiveSequence: return "a reactive sequence";
		case NodeKind::ReactiveSelector: return "a reactive selector";
		case NodeKind::Parallel: return "a parallel";
		case NodeKind::Invert: return "an invert";
		case NodeKind::Succeed: return "a succeed";
		case NodeKind::Repeat: return "a repeat";
		case NodeKind::Loop: return "a loop";
		case NodeKind::Wait: return "a wait";
		case NodeKind::Task: return "a task";
		case NodeKind::Subtree: return "a subtree";
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
		case NodeKind::ReactiveSequence:
		case NodeKind::ReactiveSelector:
		case NodeKind::Parallel:
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
		case NodeKind::Subtree:
			if (!node.children.empty())
				refuse("with children");
			break;
	}
	if (node.kind == NodeKind::Parallel && node.children.size() > kParallelChildren)
		refuse(std::format("with {} children, more than {}", node.children.size(), kParallelChildren));
	if (node.kind == NodeKind::Parallel && node.count > node.children.size())
		refuse(std::format("needing {} of its {} children", node.count, node.children.size()));
	if (node.kind == NodeKind::Subtree && node.tree.empty())
		refuse("that names no tree");
	if (node.kind != NodeKind::Subtree && !node.tree.empty())
		refuse("with a tree's name, which only a subtree has");
	if (node.kind == NodeKind::Task && node.task.empty())
		refuse("that names none");
	if (node.kind != NodeKind::Task && (!node.task.empty() || !node.arguments.empty()))
		refuse("with a task's name or arguments");
	if (node.kind != NodeKind::Repeat && node.kind != NodeKind::Parallel && node.count != 0)
		refuse("with a count, which only a repeat or a parallel has");
	if (node.kind == NodeKind::Wait && (!std::isfinite(node.seconds) || node.seconds < 0.0))
		refuse(std::format("of {} seconds", node.seconds));
	if (node.kind != NodeKind::Wait && node.seconds != 0.0)
		refuse("with seconds, which only a wait has");
	for (std::size_t index = 0; index < node.children.size(); ++index)
		CheckNode(name, node.children[index], std::format("{}.{}", path, index));
}


// Lets the vehicle's controls go, from the helm or to it.
void LetGo(entt::registry &registry, entt::entity vehicle)
{
	if (auto *controls = registry.try_get<Physics::FlightControls>(vehicle))
		*controls = Physics::FlightControls{};
}


void Leave(Sim::SimWorld &world, entt::entity vehicle)
{
	entt::registry &registry = world.Registry();
	const Sim::Controller controller = registry.get<Sim::Helm>(vehicle).controller;
	registry.remove<Sim::Helm>(vehicle);
	LetGo(registry, vehicle);
	if (auto *behaviour = registry.try_get<Behaviour>(vehicle))
		behaviour->nodes.clear();
	world.Events().Emit(HelmLeft{controller, vehicle});
}


// Whether the helm was taken.
bool Take(Sim::SimWorld &world, const TakeHelm &command)
{
	entt::registry &registry = world.Registry();
	const auto refuse = [&](Reason reason)
	{
		world.Events().Emit(HelmRefused{command.sender, command.vehicle, Sim::MakeReason(reason)});
		return false;
	};
	if (!registry.valid(command.vehicle))
		return refuse(Reason::NoSuchEntity);
	if (command.sender.kind == Sim::ControllerKind::None)
		return refuse(Reason::NoPilot);
	if (!Sim::MayCommand(registry, command.vehicle, command.sender))
		return refuse(Reason::NotController);
	if (const auto *helm = registry.try_get<Sim::Helm>(command.vehicle);
	    helm && helm->controller != command.sender)
		return refuse(Reason::HelmHeld);

	// The helm it was at, in entity order, should a save hold more than one.
	std::vector<entt::entity> held;
	for (const auto [vehicle, helm] : registry.view<const Sim::Helm>().each())
		if (vehicle != command.vehicle && helm.controller == command.sender)
			held.push_back(vehicle);
	std::ranges::sort(held, {}, [](entt::entity vehicle) { return entt::to_integral(vehicle); });
	for (const entt::entity vehicle : held)
		Leave(world, vehicle);
	registry.emplace_or_replace<Sim::Helm>(command.vehicle, command.sender);
	registry.remove<Destination>(command.vehicle);
	LetGo(registry, command.vehicle);
	world.Events().Emit(HelmTaken{command.sender, command.vehicle});
	return true;
}


void Leave(Sim::SimWorld &world, const LeaveHelm &command)
{
	const entt::registry &registry = world.Registry();
	if (!registry.valid(command.vehicle))
		return world.Events().Emit(
		    HelmRefused{command.sender, command.vehicle, Sim::MakeReason(Reason::NoSuchEntity)});
	const auto *helm = registry.try_get<Sim::Helm>(command.vehicle);
	if (!helm ||
	    (command.sender.kind != Sim::ControllerKind::None && helm->controller != command.sender))
		return world.Events().Emit(
		    HelmRefused{command.sender, command.vehicle, Sim::MakeReason(Reason::NotAtHelm)});
	Leave(world, command.vehicle);
}
} // namespace


void CheckBehaviour(std::string_view name, const BehaviourDefinition &behaviour)
{
	if (!std::isfinite(behaviour.interval) || behaviour.interval < 0.0)
		throw Serialise::FormatError{
		    std::format("Behaviour '{}' has an interval of {} seconds", name, behaviour.interval)};
	CheckNode(name, behaviour.root, "root");
}


Behaviours &Behaviours::Attach(Sim::SimWorld &world)
{
	RegisterTypes();

	// Shared by the system, which the world keeps for as long as it lives.
	std::shared_ptr<Behaviours> behaviours{new Behaviours{}};
	behaviours->m_fixedStep = world.FixedStep();
	world.RegisterComponent<Behaviour>();
	world.RegisterComponent<Blackboard>();
	world.RegisterComponent<Sim::Helm>();

	// For whose helm a vehicle is at: the flight panel's, and which vehicle a player's camera follows.
	world.ShowInSnapshots<Sim::Helm>();
	world.RegisterCommand<AssignBehaviour>(
	    [behaviours](Sim::SimWorld &target, const AssignBehaviour &command)
	    { behaviours->Assign(target, command); });
	world.RegisterCommand<TakeHelm>(
	    [behaviours](Sim::SimWorld &target, const TakeHelm &command)
	    {
		    if (Take(target, command))
			    behaviours->Stop(target, command.vehicle);
	    });
	world.RegisterCommand<LeaveHelm>([](Sim::SimWorld &target, const LeaveHelm &command)
	                                 { Leave(target, command); });
	Ecs::SystemDescription system{
	    std::string{kSystemName},
	    Ecs::TickPhase::Simulation,
	    Ecs::Access::Of<Sim::Helm>().Writing<Behaviour>(),
	    {},
	    [behaviours, &world](entt::registry & /*registry*/, double /*fixedStep*/)
	    { behaviours->Step(world); }};
	system.before = {std::string{Spatial::FrameIndices::kSystemName}};
	world.RegisterSystem(
	    std::move(system),
	    {.next = [behaviours](const Sim::SimWorld &target) { return behaviours->QuietFrom(target); }});
	return *behaviours;
}


void Behaviours::DefineTask(std::string name, Task task, TaskWake wake, TaskHalt halt)
{
	if (name.empty() || !task)
		throw std::invalid_argument{"Ai: a task needs a name and a body"};
	if (m_tasks.contains(name))
		throw std::invalid_argument{std::format("Ai: task '{}' is already defined", name)};
	m_tasks.emplace(std::move(name), TaskEntry{std::move(task), std::move(wake), std::move(halt)});
}


void Behaviours::DefineTree(std::string name, const BehaviourDefinition &tree)
{
	if (m_trees.contains(name))
		throw std::invalid_argument{std::format("Ai: behaviour '{}' is already defined", name)};
	CheckBehaviour(name, tree);
	Tree flat;
	const std::optional<std::uint64_t> every = WholeSteps(tree.interval, m_fixedStep);
	if (!every)
		Refuse(
		    name, "root",
		    std::format("an interval of {} s, more steps than a run counts", tree.interval));
	flat.every = std::max<std::uint64_t>(1, *every);
	Flatten(tree.root, flat, name, "root");
	m_trees.emplace(name, std::move(flat));
	m_definitions.emplace(std::move(name), tree);
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
	return found == m_trees.end() ? 0 : found->second.nodes.size();
}


void Behaviours::Flatten(
    const BehaviourNode &node, Tree &tree, std::string_view name, std::string path) const
{
	if (node.kind == NodeKind::Subtree)
	{
		const auto subtree = m_definitions.find(node.tree);
		if (subtree == m_definitions.end())
			Refuse(name, path, std::format("the tree '{}', which is not defined", node.tree));
		return Flatten(subtree->second.root, tree, name, std::move(path));
	}
	std::vector<Node> &nodes = tree.nodes;
	const auto index = static_cast<std::uint32_t>(nodes.size());
	Node flat{node.kind, nullptr, node.arguments, node.count, 0, {}, 0};
	if (node.kind == NodeKind::Task)
	{
		const auto task = m_tasks.find(node.task);
		if (task == m_tasks.end())
			Refuse(name, path, std::format("the task '{}', which is not defined", node.task));
		flat.task = &task->second;
	}

	if (node.kind == NodeKind::Wait)
	{
		const std::optional<std::uint64_t> steps = WholeSteps(node.seconds, m_fixedStep);
		if (!steps)
			Refuse(
			    name, path, std::format("a wait of {} s, more steps than a run counts", node.seconds));
		flat.steps = *steps;
	}
	nodes.push_back(std::move(flat));
	for (std::size_t child = 0; child < node.children.size(); ++child)
	{
		nodes[index].children.push_back(static_cast<std::uint32_t>(nodes.size()));
		Flatten(node.children[child], tree, name, std::format("{}.{}", path, child));
	}
	nodes[index].end = static_cast<std::uint32_t>(nodes.size());
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
	if (states.size() != tree.nodes.size())
		states.assign(tree.nodes.size(), NodeState{});
	m_ticking.push_back({entity, false});
	const Status status = TickNode(world, entity, tree, states, 0);
	const bool stopped = m_ticking.back().stopped;
	m_ticking.pop_back();

	// Stopped by its own task's command: halted now its nodes are whole again, and what the command
	// left (a new tree, from its top, or none) stands.
	if (stopped)
	{
		Halt(world, entity, tree, states, 0);
		return Status::Failure;
	}
	if (auto *after = registry.try_get<Behaviour>(entity); after && after->tree == found->first)
	{
		after->nodes = std::move(states);
		after->last = status;
	}
	return status;
}


bool Behaviours::Stopped(entt::entity entity) const
{
	const auto ticking = std::ranges::find(m_ticking, entity, &Ticking::entity);
	return ticking != m_ticking.end() && ticking->stopped;
}


void Behaviours::Stop(Sim::SimWorld &world, entt::entity entity)
{
	entt::registry &registry = world.Registry();

	// Its tree is being ticked, its nodes the tick's: halted as the tick ends (Tick).
	if (const auto ticking = std::ranges::find(m_ticking, entity, &Ticking::entity);
	    ticking != m_ticking.end())
	{
		ticking->stopped = true;
		if (auto *behaviour = registry.try_get<Behaviour>(entity))
			behaviour->nodes.clear();
		return;
	}
	auto *behaviour = registry.try_get<Behaviour>(entity);
	if (!behaviour)
		return;

	// Its own copy while its tasks are halted, as in Tick.
	std::vector<NodeState> states = std::move(behaviour->nodes);
	behaviour->nodes.clear();
	const auto found = m_trees.find(behaviour->tree);
	if (found != m_trees.end() && states.size() == found->second.nodes.size())
		Halt(world, entity, found->second, states, 0);
}


void Behaviours::Assign(Sim::SimWorld &world, const AssignBehaviour &command)
{
	entt::registry &registry = world.Registry();
	const auto refuse = [&](Reason reason, std::vector<Sim::ReasonArgument> arguments = {})
	{
		world.Events().Emit(
		    BehaviourRefused{
		        command.sender, command.entity, Sim::MakeReason(reason, std::move(arguments))});
	};
	if (!registry.valid(command.entity))
		return refuse(Reason::NoSuchEntity);
	if (!Sim::MayCommand(registry, command.entity, command.sender))
		return refuse(Reason::NotController);
	if (!command.tree.empty() && !m_trees.contains(command.tree))
		return refuse(
		    Reason::NoSuchTree,
		    {Sim::ReasonArgument::Identifier(command.tree, "Ai.BehaviourDefinition")});
	Stop(world, command.entity);

	// A halted task may have taken the entity away.
	if (!registry.valid(command.entity))
		return;
	if (command.tree.empty())
		registry.remove<Behaviour>(command.entity);
	else
		registry.emplace_or_replace<Behaviour>(command.entity, Behaviour{command.tree, {}, {}});
	world.Events().Emit(BehaviourAssigned{command.sender, command.entity, command.tree});
}


std::uint64_t Behaviours::DueFrom(std::uint64_t step, entt::entity entity, const Tree &tree)
{
	// Never stays never, rather than wrapping round to a step soon.
	constexpr std::uint64_t kNever = std::numeric_limits<std::uint64_t>::max();
	const std::uint64_t phase = (step % tree.every + entt::to_entity(entity) % tree.every) % tree.every;
	if (phase == 0)
		return step;
	const std::uint64_t ahead = tree.every - phase;
	return step > kNever - ahead ? kNever : step + ahead;
}


void Behaviours::Step(Sim::SimWorld &world)
{
	entt::registry &registry = world.Registry();
	const std::uint64_t now = world.Clock().Step();
	std::vector<entt::entity> entities;
	for (const auto [entity, behaviour] :
	     registry.view<const Behaviour>(entt::exclude<Sim::Helm>).each())
	{
		const auto found = m_trees.find(behaviour.tree);
		if (found == m_trees.end() || DueFrom(now, entity, found->second) == now)
			entities.push_back(entity);
	}
	std::ranges::sort(entities, {}, [](entt::entity entity) { return entt::to_integral(entity); });

	// Asked again as each comes, since a task of one before it applies its commands at once: one
	// given another tree, not due this step, or whose helm was taken is not ticked.
	for (const entt::entity entity : entities)
	{
		if (!registry.valid(entity) || registry.all_of<Sim::Helm>(entity))
			continue;
		const auto *behaviour = registry.try_get<Behaviour>(entity);
		if (!behaviour)
			continue;
		const auto found = m_trees.find(behaviour->tree);
		if (found != m_trees.end() && DueFrom(now, entity, found->second) != now)
			continue;
		Tick(world, entity);
	}
}


std::uint64_t Behaviours::QuietFrom(const Sim::SimWorld &world) const
{
	const entt::registry &registry = world.Registry();
	const std::uint64_t now = world.Clock().Step();
	std::uint64_t next = std::numeric_limits<std::uint64_t>::max();
	for (const auto [entity, behaviour] :
	     registry.view<const Behaviour>(entt::exclude<Sim::Helm>).each())
	{
		const auto found = m_trees.find(behaviour.tree);

		// Left alone, as Tick leaves it.
		if (found == m_trees.end())
			continue;
		const Tree &tree = found->second;
		const std::uint64_t wake =
		    behaviour.nodes.size() != tree.nodes.size() || behaviour.last != Status::Running
		        ? now
		        : WakeOf(world, entity, tree, behaviour.nodes, 0);
		next = std::min(next, DueFrom(std::max(wake, now), entity, tree));
		if (next <= now)
			return now;
	}
	return next;
}


std::uint64_t Behaviours::WakeOf(
    const Sim::SimWorld &world, entt::entity entity, const Tree &tree,
    const std::vector<NodeState> &states, std::uint32_t index) const
{
	const Node &node = tree.nodes[index];
	const NodeState &state = states[index];
	const std::uint64_t now = world.Clock().Step();
	if (!state.running)
		return now;
	switch (node.kind)
	{
		case NodeKind::Sequence:
		case NodeKind::Selector:
			// Its running child is the one it stopped at.
			return state.memory < node.children.size()
			           ? WakeOf(world, entity, tree, states, node.children[state.memory])
			           : now;

		// Its children before the running one are asked again at every tick.
		case NodeKind::ReactiveSequence:
		case NodeKind::ReactiveSelector: return now;
		case NodeKind::Parallel:
		{
			std::uint64_t wake = std::numeric_limits<std::uint64_t>::max();
			for (std::size_t child = 0; child < node.children.size(); ++child)
				if ((state.memory & (std::uint64_t{1} << child)) == 0)
					wake = std::min(wake, WakeOf(world, entity, tree, states, node.children[child]));
			return wake;
		}
		case NodeKind::Invert:
		case NodeKind::Succeed:
		case NodeKind::Repeat:
		case NodeKind::Loop: return WakeOf(world, entity, tree, states, node.children[0]);
		case NodeKind::Wait: return state.memory;
		case NodeKind::Task:
			return node.task->wake ? node.task->wake(world, entity, node.arguments, state.memory) : now;

		// Flattened into the tree it names.
		case NodeKind::Subtree: return now;
	}
	return now;
}


Status Behaviours::TickNode(
    Sim::SimWorld &world, entt::entity entity, const Tree &tree, std::vector<NodeState> &states,
    std::uint32_t index) const
{
	// Stopped part-way through by one of its tasks' commands: nothing more of the tick is done.
	if (Stopped(entity))
		return Status::Failure;
	const Node &node = tree.nodes[index];
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
		case NodeKind::ReactiveSequence:
		case NodeKind::ReactiveSelector:
		{
			const Status onward =
			    node.kind == NodeKind::ReactiveSequence ? Status::Success : Status::Failure;
			std::size_t at = 0;
			for (; at < node.children.size(); ++at)
			{
				status = child(at);
				if (status != onward)
					break;
			}

			// The branch it has left: a child past the one that answered, running from a tick before.
			for (std::size_t later = at + 1; later < node.children.size(); ++later)
				Halt(world, entity, tree, states, node.children[later]);
			break;
		}
		case NodeKind::Parallel:
		{
			const std::size_t children = node.children.size();
			const std::size_t needed = node.count == 0 ? children : node.count;
			std::uint64_t finished = states[index].memory;
			for (std::size_t which = 0; which < children; ++which)
			{
				const std::uint64_t bit = std::uint64_t{1} << which;
				if ((finished & bit) != 0)
					continue;
				const Status result = child(which);
				if (result == Status::Running)
					continue;
				finished |= bit;
				if (result == Status::Success)
					finished |= bit << kParallelChildren;
			}
			states[index].memory = finished;
			const auto succeeded =
			    static_cast<std::size_t>(std::popcount(finished >> kParallelChildren));
			const auto failed =
			    static_cast<std::size_t>(std::popcount(finished & kParallelFinished)) - succeeded;
			status = succeeded >= needed          ? Status::Success
			         : failed > children - needed ? Status::Failure
			                                      : Status::Running;
			if (status != Status::Running)
				for (const std::uint32_t which : node.children)
					Halt(world, entity, tree, states, which);
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
			status = node.task->task(context);
			break;
		}

		// Flattened into the tree it names, so never ticked.
		case NodeKind::Subtree: JETTISON_ASSERT(false); break;
	}
	states[index].running = status == Status::Running;
	return status;
}


void Behaviours::Halt(
    Sim::SimWorld &world, entt::entity entity, const Tree &tree, std::vector<NodeState> &states,
    std::uint32_t index) const
{
	for (std::uint32_t at = index; at < tree.nodes[index].end; ++at)
	{
		if (!states[at].running)
			continue;
		states[at].running = false;
		const Node &node = tree.nodes[at];
		if (node.kind == NodeKind::Task && node.task->halt)
		{
			TaskContext context{world, entity, node.arguments, false, states[at].memory};
			node.task->halt(context);
		}
	}
}
} // namespace Jettison::Ai
