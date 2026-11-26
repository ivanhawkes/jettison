#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/sim/Controller.h>
#include <jettison/sim/Reason.h>

namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
} // namespace Jettison::Sim

namespace Jettison::Ai
{
// Behaviour trees for an entity's sequences (docs/ai.md, "Behaviour trees"): undock, fly to a
// door, pass through it, ask for a berth, wait for one, dock, trade, wait for the load. A tree is data,
// its leaves tasks the game defines by name, and it is ticked in the simulation step,
// deterministically: where each entity is in its tree is saved with the world, and a world loaded from
// a save carries on from the same nodes.

enum class Status : std::uint8_t
{
	Running,
	Success,
	Failure
};

enum class NodeKind : std::uint8_t
{
	// Its children in order, until one fails, which fails it; it succeeds once every one has.
	Sequence,
	// Its children in order, until one succeeds, which succeeds it; it fails once every one has.
	Selector,
	// A sequence ticked from its first child every tick, so a condition before a running child is
	// checked again; the running child it no longer reaches is halted.
	ReactiveSequence,
	// A selector ticked from its first child every tick, so a branch before a running one takes over
	// once it no longer fails, and the running one is halted.
	ReactiveSelector,
	// Every child ticked each tick, until count of them have succeeded (with none, all of them), which
	// succeeds it, or so many have failed that count cannot be reached, which fails it; the children
	// still running then are halted. At most kParallelChildren.
	Parallel,
	// Its one child, with success and failure swapped.
	Invert,
	// Its one child, a failure taken as a success.
	Succeed,
	// Its one child, again from the start each time it succeeds, count times or, with none, for
	// ever; it fails as the child fails.
	Repeat,
	// Its one child, again from the start each time it succeeds, until it fails, which succeeds the
	// loop: a loop whose child's first task is its condition, as "while a leg is left".
	Loop,
	// Running for its seconds, in whole steps rounded up, then a success.
	Wait,
	// A task of the game's, by name, with the node's arguments.
	Task,
	// The tree named, defined before this one, in this node's place, as if written there.
	Subtree
};

// A parallel's children at most: its memory holds which have finished and which succeeded, a bit
// each in 64.
inline constexpr std::size_t kParallelChildren = 32;

// A node as data, its children nested in it. What a node does not use it must not set: a count on
// anything but a repeat or a parallel, seconds on anything but a wait, a task or arguments on anything
// but a task, a tree on anything but a subtree, children on a wait, a task or a subtree. Registered
// with Reflect as "Ai.BehaviourNode", every field Saved and Moddable.
struct BehaviourNode
{
	NodeKind kind{NodeKind::Task};
	std::string task;
	std::vector<std::string> arguments;
	std::uint32_t count{0};
	double seconds{0.0};
	std::string tree;
	std::vector<BehaviourNode> children;

	bool operator==(const BehaviourNode &) const = default;
};

// A tree: defs/behaviours/<name>.yaml, loaded through Sim::DefinitionRegistry, so a mod changes how
// a trader goes about its trade. Its interval is the seconds between its ticks, in whole steps rounded
// up, for many entities whose decisions need not run every step; with none, every step. Registered as
// "Ai.BehaviourDefinition", Moddable.
struct BehaviourDefinition
{
	BehaviourNode root;
	double interval{0.0};

	bool operator==(const BehaviourDefinition &) const = default;
};

inline constexpr std::uint32_t kBehaviourVersion = 1;
inline constexpr std::string_view kBehaviourDirectory = "defs/behaviours";

// Checks a tree's shape, in the one place a file read and a tree made in code both go through: a
// sequence or a selector with a child or more, a decorator with one, a wait or a task with none, a
// wait's seconds finite and not negative, a task or a subtree's tree named, a parallel's count no
// more than its children and its children no more than kParallelChildren, the interval finite and not
// negative, and nothing set that its kind does not use.
// Throws Serialise::FormatError naming the tree and the node, by its path of child indices from the
// root ("root.2.0").
void CheckBehaviour(std::string_view name, const BehaviourDefinition &behaviour);

// Where one node of an entity's tree is: whether it is running, and its memory while it runs: a
// sequence's or a selector's running child, a repeat's runs, the step a wait ends at, a task's own.
// Registered as "Ai.NodeState", both fields Saved.
struct NodeState
{
	std::uint64_t memory{0};
	bool running{false};

	bool operator==(const NodeState &) const = default;
};

// An entity's tree, by definition name, and where it is in it: each node's state, by the node's
// index in the tree's depth-first order, and what the root gave at its last tick. The root is ticked
// every step, so a tree that has finished starts again from the top at the next. A state that is not
// one for the tree (none yet, or a save's of a tree since changed) starts it from the top. A running
// node is left before it finishes only by a reactive node or a parallel, by AssignBehaviour or by its
// helm being taken, and its tasks are halted then (TaskHalt). Registered as "Ai.Behaviour", every
// field Saved.
struct Behaviour
{
	std::string tree;
	std::vector<NodeState> nodes;
	Status last{Status::Running};

	bool operator==(const Behaviour &) const = default;
};

// An entity's memory that its tasks share and keep from one run of its tree to the next, by key: a
// target, a count. The behaviours leave it as it is when a tree starts again or is changed. Registered
// as "Ai.Blackboard", both fields Saved.
struct Blackboard
{
	std::map<std::string, std::int64_t, std::less<>> integers;
	std::map<std::string, entt::entity, std::less<>> entities;

	bool operator==(const Blackboard &) const = default;
};

struct TaskContext
{
	Sim::SimWorld &world;
	entt::entity entity;
	std::span<const std::string> arguments;

	// Its first tick since it last finished, when it sends what it then waits on.
	bool starting;

	// Its own memory while it runs, saved with the tree: a step it began at, a count.
	std::uint64_t &memory;
};

// A task: what it did this tick, Running until it finishes. It acts through the world's commands,
// applied at once (Sim::SimWorld::Apply), as a player's would be, and reads the state; it keeps
// nothing of its own between ticks but its memory, so a save holds all of it.
using Task = std::function<Status(TaskContext &context)>;

// When a running task next has to be ticked, for a step to be passed over (Sim::Quiet): the first
// step, by the clock's count, at which it may do anything but return Running with its memory and the
// world as they are, given the world as it is and no command applied. It reads what a tick would
// read and changes nothing. A task without one is ticked every step.
using TaskWake = std::function<std::uint64_t(
    const Sim::SimWorld &world, entt::entity entity, std::span<const std::string> arguments,
    std::uint64_t memory)>;

// Tells a running task it is left before it finishes (Behaviour), so it can let go of what it holds:
// a berth asked for, a destination. Its context's starting is false, and its memory is discarded after.
// It acts through commands, as a task does.
using TaskHalt = std::function<void(TaskContext &context)>;

// The command that gives an entity a tree, or with none takes its tree away: the running tasks of the
// one it had are halted, and the new one starts from the top. Its blackboard is kept. Refused, with a
// BehaviourRefused, for an entity not in the world, a sender that does not command it
// (Sim::MayCommand), and a tree not defined. Registered with Reflect, every field Saved, by
// RegisterTypes.
struct AssignBehaviour
{
	Sim::Controller sender;
	entt::entity entity{entt::null};
	std::string tree;
};

// Raised as an AssignBehaviour is applied, and as one is refused, saying why. Registered with
// Reflect, every field Saved, by RegisterTypes.
struct BehaviourAssigned
{
	Sim::Controller sender;
	entt::entity entity{entt::null};
	std::string tree;
};
struct BehaviourRefused
{
	Sim::Controller sender;
	entt::entity entity{entt::null};
	Sim::Reason reason;
};

// The commands that take and leave a vehicle's helm (Sim::Helm), which a player flies it by: taken
// by one that commands it (Sim::MayCommand), a player or an AI, not no one in particular; refused,
// with a HelmRefused, for a vehicle another is at the helm of. Each is at one helm at a time, so
// taking one leaves the one the sender was at. Taking it stands the vehicle's behaviour down and
// takes away the destination it was being flown to (Steering.h), its controls let go, so it holds
// where it is until its pilot flies it; what the game's rules fly it by goes on (the docks'
// approach and lift, a transit, a portal). A task it was running is halted (TaskHalt); what it
// asked for and did not let go of (a berth, a portal's clearance) goes on or lapses as it would for a
// vehicle that did not come. Leaving it lets the controls go, and the vehicle's tree starts again from
// the top. Left by the one at it, or by no one in particular, the game's rules, from
// whoever is. Registered with Reflect, every
// field Saved, by RegisterTypes.
struct TakeHelm
{
	Sim::Controller sender;
	entt::entity vehicle{entt::null};
};
struct LeaveHelm
{
	Sim::Controller sender;
	entt::entity vehicle{entt::null};
};

// Raised as a helm is taken and left, by whom, and as a TakeHelm or a LeaveHelm is refused, saying
// why. Registered with Reflect, every field Saved, by
// RegisterTypes.
struct HelmTaken
{
	Sim::Controller controller;
	entt::entity vehicle{entt::null};
};
struct HelmLeft
{
	Sim::Controller controller;
	entt::entity vehicle{entt::null};
};
struct HelmRefused
{
	Sim::Controller sender;
	entt::entity vehicle{entt::null};
	Sim::Reason reason;
};

// Behaviour trees in a simulation world: the Behaviour component, and the system "behaviour"
// (kSystemName), first in the step, before the spatial index's ("spatial"), so a tree acts on the
// world as the step's commands do, where it is as the step begins, and what its tasks set (a
// destination, a berth asked for) is flown and answered in the same step. Each step it ticks every
// entity's tree that is due, in entity order, but one someone is at the helm of (Sim::Helm), whose
// tree stands down until it is left. A tree with an interval is due once in its interval's steps, at
// a step set by the entity's index, so the entities following it are spread over the interval. An
// entity whose tree is not defined is left alone, saying so once. Attach registers the Blackboard and
// the AssignBehaviour command, and the helm is the module's too: Sim::Helm, shown in snapshots, and
// the TakeHelm and LeaveHelm commands.
//
// Tasks and trees are the game's start-up, as physics' shapes are, and not state: a save names
// trees, and a world it is loaded into is given the same ones. A tree's tasks are defined before it.
class Behaviours
{
  public:
	static constexpr std::string_view kSystemName = "behaviour";

	// Registers the components, the helm's commands and the system. Throws std::logic_error as
	// SimWorld's registrations do, for one already made.
	static Behaviours &Attach(Sim::SimWorld &world);

	// Throws std::invalid_argument for a name already defined, or one that is empty.
	void DefineTask(std::string name, Task task, TaskWake wake = {}, TaskHalt halt = {});

	// A tree, checked (CheckBehaviour), each of its tasks and subtrees one defined. Throws
	// Serialise::FormatError for one that does not fit, and std::invalid_argument for a name already
	// defined.
	void DefineTree(std::string name, const BehaviourDefinition &tree);

	// Every BehaviourDefinition the registry has loaded.
	void Define(const Sim::DefinitionRegistry &definitions);

	bool HasTree(std::string_view name) const;

	// The tree's nodes, in depth-first order, its subtrees' among them, or none for a tree not defined.
	std::size_t NodeCount(std::string_view tree) const;

	// Ticks one entity's tree once, as the system does an entity no one is at the helm of, due or not:
	// what its root gave.
	Status Tick(Sim::SimWorld &world, entt::entity entity);

	// Halts the running tasks of the entity's tree and starts it again from the top at its next tick,
	// as taking its helm or giving it another tree does. Asked by a command one of the tree's own tasks
	// applies as it is ticked, the rest of that tick does nothing and the halts come as it ends, since
	// until then its nodes are the tick's.
	void Stop(Sim::SimWorld &world, entt::entity entity);

  private:
	struct TaskEntry
	{
		Task task;
		TaskWake wake;
		TaskHalt halt;
	};

	// A node as it is ticked: its kind and what it holds, its children by index, and the index past
	// its last descendant, so its subtree is the nodes from it to there.
	struct Node
	{
		NodeKind kind{NodeKind::Task};
		const TaskEntry *task{nullptr};
		std::vector<std::string> arguments;
		std::uint32_t count{0};
		std::uint64_t steps{0};
		std::vector<std::uint32_t> children;
		std::uint32_t end{0};
	};

	// A tree's nodes in depth-first order, and the steps from one of its ticks to the next.
	struct Tree
	{
		std::vector<Node> nodes;
		std::uint64_t every{1};
	};

	Behaviours() = default;

	void Step(Sim::SimWorld &world);

	void Assign(Sim::SimWorld &world, const AssignBehaviour &command);

	// The first step from this one that the tree is due in for the entity.
	static std::uint64_t DueFrom(std::uint64_t step, entt::entity entity, const Tree &tree);

	// Halts every running node from the one at the index to its last descendant, its tasks told so.
	void Halt(
	    Sim::SimWorld &world, entt::entity entity, const Tree &tree, std::vector<NodeState> &states,
	    std::uint32_t index) const;

	// The step a tick next does anything in (Sim::Quiet): the soonest of every tree's, a tree's being
	// where its running path ends, a wait's end or a task's wake, and the next step for a tree that
	// finished or a node about to start.
	std::uint64_t QuietFrom(const Sim::SimWorld &world) const;

	std::uint64_t WakeOf(
	    const Sim::SimWorld &world, entt::entity entity, const Tree &tree,
	    const std::vector<NodeState> &states, std::uint32_t index) const;

	void Flatten(const BehaviourNode &node, Tree &tree, std::string_view name, std::string path) const;

	Status TickNode(
	    Sim::SimWorld &world, entt::entity entity, const Tree &tree, std::vector<NodeState> &states,
	    std::uint32_t index) const;

	std::map<std::string, TaskEntry, std::less<>> m_tasks;
	std::map<std::string, Tree, std::less<>> m_trees;

	// The trees as defined, for a later tree's subtrees.
	std::map<std::string, BehaviourDefinition, std::less<>> m_definitions;

	// Trees named by an entity and not defined, said once each; not state.
	std::set<std::string, std::less<>> m_missing;

	// The entities whose trees are being ticked, the innermost last (a task may tick another's), and
	// whether a command applied by one of its tasks has stopped each (Stop, an AssignBehaviour or its
	// helm taken): its nodes are the tick's own copy until the tick ends, so the stop's halts wait for
	// that, and the rest of the tick does nothing. Not state: empty between ticks.
	struct Ticking
	{
		entt::entity entity{entt::null};
		bool stopped{false};
	};
	std::vector<Ticking> m_ticking;

	bool Stopped(entt::entity entity) const;
	double m_fixedStep{0.0};
};
} // namespace Jettison::Ai
