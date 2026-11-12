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

namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
} // namespace Jettison::Sim

namespace Jettison::Ai
{
// Behaviour trees for an entity's sequences (docs/architecture.md, "Decisions"): undock, fly to a
// gate, jump, ask for a pad, wait for one, dock, trade, wait for the cargo. A tree is data, its
// leaves tasks the game defines by name, and it is ticked in the simulation step, deterministically:
// where each entity is in its tree is saved with the world, and a world loaded from a save carries
// on from the same nodes.

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
	// Its one child, with success and failure swapped.
	Invert,
	// Its one child, a failure taken as a success.
	Succeed,
	// Its one child, again from the start each time it succeeds, count times or, with none, for
	// ever; it fails as the child fails.
	Repeat,
	// Its one child, again from the start each time it succeeds, until it fails, which succeeds the
	// loop: a loop whose child's first task is its condition, as "while a jump is left".
	Loop,
	// Running for its seconds, in whole steps rounded up, then a success.
	Wait,
	// A task of the game's, by name, with the node's arguments.
	Task
};

// A node as data, its children nested in it. What a node does not use it must not set: a count on
// anything but a repeat, seconds on anything but a wait, a task or arguments on anything but a task,
// children on a wait or a task. Registered with Reflect as "Ai.BehaviourNode", every field Saved
// and Moddable.
struct BehaviourNode
{
	NodeKind kind{NodeKind::Task};
	std::string task;
	std::vector<std::string> arguments;
	std::uint32_t count{0};
	double seconds{0.0};
	std::vector<BehaviourNode> children;

	bool operator==(const BehaviourNode &) const = default;
};

// A tree: defs/behaviours/<name>.yaml, loaded through Sim::DefinitionRegistry, so a mod changes how
// a trader goes about its trade. Registered as "Ai.BehaviourDefinition", Moddable.
struct BehaviourDefinition
{
	BehaviourNode root;

	bool operator==(const BehaviourDefinition &) const = default;
};

inline constexpr std::uint32_t kBehaviourVersion = 1;
inline constexpr std::string_view kBehaviourDirectory = "defs/behaviours";

// Checks a tree's shape, in the one place a file read and a tree made in code both go through: a
// sequence or a selector with a child or more, a decorator with one, a wait or a task with none, a
// wait's seconds finite and not negative, a task named, and nothing set that its kind does not use.
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
// node is only ever left by its finishing: nothing interrupts it, so a task needs no undoing.
// Registered as "Ai.Behaviour", every field Saved.
struct Behaviour
{
	std::string tree;
	std::vector<NodeState> nodes;
	Status last{Status::Running};

	bool operator==(const Behaviour &) const = default;
};

// What a task is given as it is ticked.
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

// Behaviour trees in a simulation world: the Behaviour component, and the system "behaviour"
// (kSystemName), first in the step, before the spatial index's ("spatial"), so a tree acts on the
// world as the step's commands do, where it is as the step begins, and what its tasks set (a
// destination, a pad asked for) is flown and answered in the same step. Each step it ticks every
// entity's tree, in entity order. An entity whose tree is not defined is left alone, saying so once.
//
// Tasks and trees are the game's start-up, as physics' shapes are, and not state: a save names
// trees, and a world it is loaded into is given the same ones. A tree's tasks are defined before it.
class Behaviours
{
  public:
	static constexpr std::string_view kSystemName = "behaviour";

	// Registers the component and the system. Throws std::logic_error as SimWorld's registrations
	// do, for one already made.
	static Behaviours &Attach(Sim::SimWorld &world);

	// Throws std::invalid_argument for a name already defined, or one that is empty.
	void DefineTask(std::string name, Task task);
	// A tree, checked (CheckBehaviour), each of its tasks one defined. Throws Serialise::FormatError
	// for one that does not fit, and std::invalid_argument for a name already defined.
	void DefineTree(std::string name, const BehaviourDefinition &tree);
	// Every BehaviourDefinition the registry has loaded.
	void Define(const Sim::DefinitionRegistry &definitions);
	bool HasTree(std::string_view name) const;
	// The tree's nodes, in depth-first order, or none for a tree not defined.
	std::size_t NodeCount(std::string_view tree) const;

	// Ticks one entity's tree once, as the system does: what its root gave.
	Status Tick(Sim::SimWorld &world, entt::entity entity);

  private:
	// A node as it is ticked: its kind and what it holds, its children by index.
	struct Node
	{
		NodeKind kind{NodeKind::Task};
		const Task *task{nullptr};
		std::vector<std::string> arguments;
		std::uint32_t count{0};
		std::uint64_t steps{0};
		std::vector<std::uint32_t> children;
	};
	using Tree = std::vector<Node>;

	Behaviours() = default;

	void Step(Sim::SimWorld &world);
	void Flatten(const BehaviourNode &node, Tree &tree, std::string_view name, std::string path) const;
	Status TickNode(
	    Sim::SimWorld &world, entt::entity entity, const Tree &tree, std::vector<NodeState> &states,
	    std::uint32_t index) const;

	std::map<std::string, Task, std::less<>> m_tasks;
	std::map<std::string, Tree, std::less<>> m_trees;
	// Trees named by an entity and not defined, said once each; not state.
	std::set<std::string, std::less<>> m_missing;
	double m_fixedStep{0.0};
};
} // namespace Jettison::Ai
