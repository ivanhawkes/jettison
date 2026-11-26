#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/Types.h>

#include <jettison/physics/Flight.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/spatial/FrameIndices.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <vector>

using namespace Jettison;

namespace
{
using Ai::BehaviourNode;
using Ai::NodeKind;
using Ai::Status;

constexpr double kStep = 1.0 / 60.0;

// What the tasks do to an entity, saved with the world.
struct Tally
{
	std::int64_t count{0};
};


BehaviourNode TaskNode(std::string task, std::vector<std::string> arguments = {})
{
	BehaviourNode node;
	node.kind = NodeKind::Task;
	node.task = std::move(task);
	node.arguments = std::move(arguments);
	return node;
}


BehaviourNode Composite(NodeKind kind, std::vector<BehaviourNode> children, std::uint32_t count = 0)
{
	BehaviourNode node;
	node.kind = kind;
	node.children = std::move(children);
	node.count = count;
	return node;
}


BehaviourNode WaitNode(double seconds)
{
	BehaviourNode node;
	node.kind = NodeKind::Wait;
	node.seconds = seconds;
	return node;
}

// A world of trees alone, and the tasks the suite's trees are made of: each says what it did in the
// trace, by its first argument.
struct Trees
{
	Sim::SimWorld world{kStep, 3};
	Ai::Behaviours &behaviours{Attach(world)};
	std::vector<std::string> trace;


	Trees()
	{
		const auto named = [](const Ai::TaskContext &context)
		{ return context.arguments.empty() ? std::string{} : context.arguments[0]; };
		behaviours.DefineTask(
		    "succeed",
		    [this, named](Ai::TaskContext &context)
		    {
			    trace.push_back(named(context) + "+");
			    return Status::Success;
		    });
		behaviours.DefineTask(
		    "fail",
		    [this, named](Ai::TaskContext &context)
		    {
			    trace.push_back(named(context) + "-");
			    return Status::Failure;
		    });

		// Runs for as many ticks as its second argument says, then succeeds, counting in its memory.
		behaviours.DefineTask(
		    "run",
		    [this, named](Ai::TaskContext &context)
		    {
			    trace.push_back(named(context) + (context.starting ? "^" : "."));
			    const auto ticks = static_cast<std::uint64_t>(std::stoi(context.arguments[1]));
			    return ++context.memory >= ticks ? Status::Success : Status::Running;
		    });

		// Runs until it is halted, which it says.
		behaviours.DefineTask(
		    "hold",
		    [this, named](Ai::TaskContext &context)
		    {
			    trace.push_back(named(context) + (context.starting ? "^" : "."));
			    return Status::Running;
		    },
		    {}, [this, named](Ai::TaskContext &context) { trace.push_back(named(context) + "!"); });

		// Succeeds while the entity's blackboard holds its argument as an integer not zero.
		behaviours.DefineTask(
		    "while",
		    [this, named](Ai::TaskContext &context)
		    {
			    const auto *board = context.world.Registry().try_get<Ai::Blackboard>(context.entity);
			    const auto found = board ? board->integers.find(context.arguments[0])
			                             : decltype(board->integers.end()){};
			    const bool holds = board && found != board->integers.end() && found->second != 0;
			    trace.push_back(named(context) + (holds ? "+" : "-"));
			    return holds ? Status::Success : Status::Failure;
		    });

		// Counts on the entity, so a save has it; and fails once it has counted to its argument.
		behaviours.DefineTask(
		    "count",
		    [](Ai::TaskContext &context)
		    {
			    Tally &tally = context.world.Registry().get_or_emplace<Tally>(context.entity);
			    ++tally.count;
			    return !context.arguments.empty() && tally.count >= std::stoi(context.arguments[0])
			               ? Status::Failure
			               : Status::Success;
		    });
	}


	static Ai::Behaviours &Attach(Sim::SimWorld &world)
	{
		static const bool registered = []
		{
			Reflect::RegisterType<Tally>("BehaviourTests.Tally")
			    .Field<&Tally::count>("count", Reflect::Attribute::Saved);
			return true;
		}();
		(void)registered;
		world.RegisterComponent<Tally>();
		Spatial::FrameIndices::Attach(world);
		return Ai::Behaviours::Attach(world);
	}


	entt::entity Follow(std::string tree)
	{
		const entt::entity entity = world.Registry().create();
		world.Registry().emplace<Ai::Behaviour>(entity, Ai::Behaviour{std::move(tree), {}, {}});
		return entity;
	}


	// The trace of one tick, which is then cleared.
	std::vector<std::string> Tick(entt::entity entity, Status expected)
	{
		CHECK(behaviours.Tick(world, entity) == expected);
		return std::exchange(trace, {});
	}
};

using Trace = std::vector<std::string>;
} // namespace


TEST_CASE("Behaviour trees: a sequence goes on past success and a selector past failure")
{
	Trees trees;
	trees.behaviours.DefineTree(
	    "sequence", {Composite(
	                    NodeKind::Sequence, {TaskNode("succeed", {"a"}), TaskNode("succeed", {"b"}),
	                                         TaskNode("fail", {"c"}), TaskNode("succeed", {"d"})})});
	trees.behaviours.DefineTree(
	    "selector", {Composite(
	                    NodeKind::Selector, {TaskNode("fail", {"a"}), TaskNode("succeed", {"b"}),
	                                         TaskNode("succeed", {"c"})})});
	CHECK(trees.behaviours.NodeCount("sequence") == 5);
	CHECK(trees.Tick(trees.Follow("sequence"), Status::Failure) == Trace{"a+", "b+", "c-"});
	CHECK(trees.Tick(trees.Follow("selector"), Status::Success) == Trace{"a-", "b+"});
}


TEST_CASE(
    "Behaviour trees: a running child is resumed where it is, and what came before it is not run again")
{
	Trees trees;
	trees.behaviours.DefineTree(
	    "errand",
	    {Composite(
	        NodeKind::Sequence, {TaskNode("succeed", {"undock"}), TaskNode("run", {"fly", "3"}),
	                             TaskNode("succeed", {"dock"})})});
	const entt::entity ship = trees.Follow("errand");
	CHECK(trees.Tick(ship, Status::Running) == Trace{"undock+", "fly^"});
	CHECK(trees.Tick(ship, Status::Running) == Trace{"fly."});
	CHECK(trees.Tick(ship, Status::Success) == Trace{"fly.", "dock+"});

	// Finished, it starts again from the top, and the task from its first tick.
	CHECK(trees.Tick(ship, Status::Running) == Trace{"undock+", "fly^"});
	CHECK(trees.world.Registry().get<Ai::Behaviour>(ship).last == Status::Running);

	// A selector resumes its running child too, past the failures before it.
	trees.behaviours.DefineTree(
	    "fallback",
	    {Composite(NodeKind::Selector, {TaskNode("fail", {"a"}), TaskNode("run", {"b", "2"})})});
	const entt::entity other = trees.Follow("fallback");
	CHECK(trees.Tick(other, Status::Running) == Trace{"a-", "b^"});
	CHECK(trees.Tick(other, Status::Success) == Trace{"b."});
}


TEST_CASE("Behaviour trees: decorators invert, succeed, repeat and loop, a run a tick")
{
	Trees trees;
	trees.behaviours.DefineTree("invert", {Composite(NodeKind::Invert, {TaskNode("succeed", {"a"})})});
	trees.behaviours.DefineTree("succeed", {Composite(NodeKind::Succeed, {TaskNode("fail", {"a"})})});
	CHECK(trees.Tick(trees.Follow("invert"), Status::Failure) == Trace{"a+"});
	CHECK(trees.Tick(trees.Follow("succeed"), Status::Success) == Trace{"a-"});

	// Three runs, one a tick, however soon the child succeeds.
	trees.behaviours.DefineTree(
	    "thrice", {Composite(NodeKind::Repeat, {TaskNode("succeed", {"a"})}, 3)});
	const entt::entity thrice = trees.Follow("thrice");
	CHECK(trees.Tick(thrice, Status::Running) == Trace{"a+"});
	CHECK(trees.Tick(thrice, Status::Running) == Trace{"a+"});
	CHECK(trees.Tick(thrice, Status::Success) == Trace{"a+"});

	// For ever, until the child fails, which fails it.
	trees.behaviours.DefineTree("forever", {Composite(NodeKind::Repeat, {TaskNode("count", {"4"})})});
	const entt::entity forever = trees.Follow("forever");
	for (int run = 0; run < 3; ++run)
		trees.Tick(forever, Status::Running);
	trees.Tick(forever, Status::Failure);
	CHECK(trees.world.Registry().get<Tally>(forever).count == 4);

	// A loop runs until its condition fails, and then succeeds.
	trees.behaviours.DefineTree(
	    "loop", {Composite(
	                NodeKind::Loop, {Composite(
	                                    NodeKind::Sequence,
	                                    {TaskNode("count", {"3"}), TaskNode("run", {"jump", "2"})})})});
	const entt::entity loop = trees.Follow("loop");
	CHECK(trees.Tick(loop, Status::Running) == Trace{"jump^"});
	CHECK(trees.Tick(loop, Status::Running) == Trace{"jump."});
	CHECK(trees.Tick(loop, Status::Running) == Trace{"jump^"});
	CHECK(trees.Tick(loop, Status::Running) == Trace{"jump."});
	CHECK(trees.Tick(loop, Status::Success).empty());
	CHECK(trees.world.Registry().get<Tally>(loop).count == 3);
}


TEST_CASE("Behaviour trees: the system ticks every tree each step, and a wait counts whole steps")
{
	Trees trees;
	trees.behaviours.DefineTree(
	    "pause",
	    {Composite(
	        NodeKind::Repeat,
	        {Composite(NodeKind::Sequence, {TaskNode("count"), WaitNode(0.5), TaskNode("count")})})});

	// Under a step is one; a little over a whole number of steps is the next.
	trees.behaviours.DefineTree(
	    "blink",
	    {Composite(
	        NodeKind::Repeat, {Composite(NodeKind::Sequence, {TaskNode("count"), WaitNode(0.001)})})});
	const entt::entity pause = trees.Follow("pause");
	const entt::entity blink = trees.Follow("blink");
	const entt::entity idle = trees.world.Registry().create();
	const auto counted = [&](entt::entity entity)
	{ return trees.world.Registry().get<Tally>(entity).count; };

	trees.world.Step();
	CHECK(counted(pause) == 1);
	CHECK(counted(blink) == 1);
	CHECK_FALSE(trees.world.Registry().all_of<Tally>(idle));

	// Thirty steps of waiting, the half second at sixty a step, then the second count; the next
	// run begins a step later.
	for (int step = 1; step < 30; ++step)
		trees.world.Step();
	CHECK(counted(pause) == 1);
	trees.world.Step();
	CHECK(counted(pause) == 2);
	trees.world.Step();
	CHECK(counted(pause) == 3);

	// One step's wait and a step to start again: a count every other step.
	CHECK(counted(blink) == 16);

	// A tree that is not defined is left alone.
	const entt::entity lost = trees.Follow("lost");
	trees.world.Step();
	CHECK_FALSE(trees.world.Registry().all_of<Tally>(lost));
	CHECK(trees.world.Registry().get<Ai::Behaviour>(lost).nodes.empty());
}


TEST_CASE("Behaviour trees: a world saved part-way through its trees carries on as the one saved")
{
	const auto setup = [](Trees &trees)
	{
		trees.behaviours.DefineTree(
		    "patrol",
		    {Composite(
		        NodeKind::Repeat, {Composite(
		                              NodeKind::Selector,
		                              {Composite(
		                                   NodeKind::Sequence, {TaskNode("count", {"5"}), WaitNode(0.3),
		                                                        TaskNode("run", {"x", "7"})}),
		                               WaitNode(0.2)})})});
		for (int ship = 0; ship < 3; ++ship)
			trees.Follow("patrol");
	};
	Trees original;
	setup(original);
	for (int step = 0; step < 47; ++step)
		original.world.Step();
	const std::vector<std::byte> save = original.world.Save();
	for (int step = 0; step < 200; ++step)
		original.world.Step();

	Trees loaded;
	setup(loaded);
	loaded.world.Load(save);
	for (int step = 0; step < 200; ++step)
		loaded.world.Step();
	CHECK(loaded.world.StateHash() == original.world.StateHash());

	// A state not of the tree, as a save's would be once its tree had changed, starts it afresh.
	const entt::entity ship = loaded.Follow("patrol");
	loaded.world.Registry().get<Ai::Behaviour>(ship).nodes.resize(2, Ai::NodeState{9, true});
	loaded.world.Step();
	CHECK(
	    loaded.world.Registry().get<Ai::Behaviour>(ship).nodes.size() ==
	    loaded.behaviours.NodeCount("patrol"));
	CHECK(loaded.world.Registry().get<Tally>(ship).count == 1);
}


TEST_CASE("Behaviour trees: a tree reads from data and is checked as it is defined")
{
	Ai::RegisterTypes();
	const auto tree = Serialise::YamlArchive::Read<Ai::BehaviourDefinition>(
	    R"(version: 1
root:
  kind: Repeat
  children:
    - kind: Sequence
      children:
        - {task: succeed, arguments: [undock]}
        - {kind: Wait, seconds: 2}
        - kind: Loop
          children:
            - {task: count, arguments: ['2']}
)",
	    Serialise::FormatVersion{Ai::kBehaviourVersion});
	REQUIRE(tree.root.children.size() == 1);
	CHECK(tree.root.children[0].children[0].task == "succeed");
	CHECK(tree.root.children[0].children[1].seconds == 2.0);
	CHECK(
	    Serialise::YamlArchive::Read<Ai::BehaviourDefinition>(
	        Serialise::YamlArchive::Write(tree, Serialise::FormatVersion{Ai::kBehaviourVersion}),
	        Serialise::FormatVersion{Ai::kBehaviourVersion}) == tree);
	Trees trees;
	trees.behaviours.DefineTree("trader", tree);
	CHECK(trees.behaviours.NodeCount("trader") == 6);
	CHECK_THROWS_AS(trees.behaviours.DefineTree("trader", tree), std::invalid_argument);
	CHECK_THROWS_AS(
	    trees.behaviours.DefineTask("count", [](Ai::TaskContext &) { return Status::Success; }),
	    std::invalid_argument);

	const auto refused = [&](BehaviourNode root, const char *problem)
	{
		CAPTURE(problem);
		CHECK_THROWS_WITH_AS(
		    trees.behaviours.DefineTree("broken", {std::move(root)}), doctest::Contains(problem),
		    Serialise::FormatError);
	};
	refused(Composite(NodeKind::Sequence, {}), "at root, a sequence with no children");
	refused(
	    Composite(NodeKind::Invert, {TaskNode("succeed"), TaskNode("fail")}),
	    "an invert with 2 children");
	refused(Composite(NodeKind::Sequence, {TaskNode("")}), "at root.0, a task that names none");
	refused(Composite(NodeKind::Sequence, {TaskNode("fly")}), "the task 'fly', which is not defined");
	refused(
	    Composite(NodeKind::Sequence, {TaskNode("succeed"), WaitNode(-1.0)}),
	    "at root.1, a wait of -1 seconds");
	BehaviourNode counted = TaskNode("succeed");
	counted.count = 2;
	refused(counted, "a task with a count");
	BehaviourNode timed = Composite(NodeKind::Selector, {TaskNode("succeed")});
	timed.seconds = 1.0;
	refused(timed, "a selector with seconds");
	BehaviourNode named = WaitNode(1.0);
	named.task = "succeed";
	refused(named, "a wait with a task's name");
	BehaviourNode parent = WaitNode(1.0);
	parent.children.push_back(TaskNode("succeed"));
	refused(parent, "a wait with children");
	CHECK_FALSE(trees.behaviours.HasTree("broken"));
}


TEST_CASE(
    "Behaviour trees: a ship's tree stands down while a player is at its helm, and starts again "
    "from the top once they leave it")
{
	const Sim::Controller one{Sim::ControllerKind::Player, 1};
	const Sim::Controller two{Sim::ControllerKind::Player, 2};
	const auto setup = [&](Trees &trees)
	{
		trees.world.RegisterComponent<Sim::ControlledBy>();

		// A count, then three ticks' running: a count every third step.
		trees.behaviours.DefineTree(
		    "rounds",
		    {Composite(
		        NodeKind::Repeat,
		        {Composite(NodeKind::Sequence, {TaskNode("count"), TaskNode("run", {"x", "3"})})})});
	};
	Trees trees;
	setup(trees);
	entt::registry &registry = trees.world.Registry();
	const entt::entity first = trees.Follow("rounds");
	const entt::entity second = trees.Follow("rounds");

	// No one commands it: any player may take its helm.
	const entt::entity free = trees.Follow("rounds");
	registry.emplace<Sim::ControlledBy>(first, one);
	registry.emplace<Sim::ControlledBy>(second, one);
	registry.emplace<Ai::Destination>(first).offset = {0.0, 0.0, -500.0};
	registry.emplace<Physics::FlightControls>(
	    first, Physics::FlightControls{{0.0f, 0.0f, -1.0f}, {}, true, entt::null});
	const auto counted = [&](entt::entity entity) { return registry.get<Tally>(entity).count; };
	const auto events = [&]
	{
		std::vector<std::string> said;
		for (const Sim::SimEvent &event : trees.world.Events().Take())
			if (const auto *taken = event.event.try_cast<Ai::HelmTaken>())
				said.push_back(
				    std::format(
				        "taken {} {}", taken->controller.slot, entt::to_integral(taken->vehicle)));
			else if (const auto *left = event.event.try_cast<Ai::HelmLeft>())
				said.push_back(
				    std::format("left {} {}", left->controller.slot, entt::to_integral(left->vehicle)));
			else if (const auto *refused = event.event.try_cast<Ai::HelmRefused>())
				said.push_back(
				    std::format(
				        "refused {} {}", entt::to_integral(refused->vehicle), refused->reason.key));
		return said;
	};

	// Part-way through its run, the first ship's helm is taken: its tree stands down, the
	// destination it was flown to is taken away and its controls let go, while the second counts on.
	trees.world.Step();
	CHECK(counted(first) == 1);
	trees.world.Commands().Push(Ai::TakeHelm{one, first});
	for (int step = 1; step < 8; ++step)
		trees.world.Step();
	CHECK(counted(first) == 1);
	CHECK(counted(second) == 3);
	CHECK(registry.get<Sim::Helm>(first).controller == one);
	CHECK_FALSE(registry.all_of<Ai::Destination>(first));
	CHECK(registry.get<Physics::FlightControls>(first) == Physics::FlightControls{});
	CHECK(events() == std::vector<std::string>{std::format("taken 1 {}", entt::to_integral(first))});

	// Taking the second's leaves the first's, whose tree starts again from the top: it counts at
	// once, though it was part-way through its run.
	trees.world.Commands().Push(Ai::TakeHelm{one, second});
	trees.world.Step();
	CHECK_FALSE(registry.all_of<Sim::Helm>(first));
	CHECK(registry.get<Sim::Helm>(second).controller == one);
	CHECK(counted(first) == 2);
	CHECK(
	    events() == std::vector<std::string>{
	                    std::format("left 1 {}", entt::to_integral(first)),
	                    std::format("taken 1 {}", entt::to_integral(second))});

	// Refused: from a player who does not command the ship, from no one in particular, for a ship
	// another is at the helm of, for one that has gone, and leaving a helm the sender is not at.
	const entt::entity gone = registry.create();
	registry.destroy(gone);
	trees.world.Commands().Push(Ai::TakeHelm{two, second});
	trees.world.Commands().Push(Ai::TakeHelm{{}, first});
	trees.world.Commands().Push(Ai::TakeHelm{two, free});
	trees.world.Commands().Push(Ai::TakeHelm{one, free});
	trees.world.Commands().Push(Ai::TakeHelm{one, gone});
	trees.world.Commands().Push(Ai::LeaveHelm{one, first});
	trees.world.Commands().Push(Ai::LeaveHelm{two, second});
	trees.world.Step();
	const auto id = [](entt::entity ship) { return entt::to_integral(ship); };
	CHECK(
	    events() == std::vector<std::string>{
	                    std::format("refused {} reason.ai.not_controller", id(second)),
	                    std::format("refused {} reason.ai.no_pilot", id(first)),
	                    std::format("taken 2 {}", id(free)),
	                    std::format("refused {} reason.ai.helm_held", id(free)),
	                    std::format("refused {} reason.ai.no_such_entity", id(gone)),
	                    std::format("refused {} reason.ai.not_at_helm", id(first)),
	                    std::format("refused {} reason.ai.not_at_helm", id(second))});
	CHECK(registry.get<Sim::Helm>(second).controller == one);

	// The helm is saved: a world loaded holds the second ship's tree stood down.
	const std::int64_t before = counted(second);
	Trees loaded;
	setup(loaded);
	loaded.world.Load(trees.world.Save());
	for (int step = 0; step < 6; ++step)
		loaded.world.Step();
	CHECK(loaded.world.Registry().get<Sim::Helm>(second).controller == one);
	CHECK(loaded.world.Registry().get<Tally>(second).count == before);

	// Left by its player, or by the game's rules from whoever is at it.
	trees.world.Commands().Push(Ai::LeaveHelm{one, second});
	trees.world.Commands().Push(Ai::LeaveHelm{{}, free});
	trees.world.Step();
	CHECK(
	    events() == std::vector<std::string>{
	                    std::format("left 1 {}", id(second)), std::format("left 2 {}", id(free))});
	CHECK(registry.view<Sim::Helm>().empty());
	CHECK(counted(second) == before + 1);
}


TEST_CASE("Behaviour trees: steps a tree waits through are passed over, to the state ticking leaves")
{
	Trees forwarded;
	Trees stepped;
	for (Trees *trees : {&forwarded, &stepped})
	{
		// Asleep for its argument's steps from its first tick, which it says by its wake.
		trees->behaviours.DefineTask(
		    "doze",
		    [](Ai::TaskContext &context)
		    {
			    if (context.starting)
				    context.memory = context.world.Clock().Step() + std::stoull(context.arguments[0]);
			    return context.world.Clock().Step() >= context.memory ? Status::Success
			                                                          : Status::Running;
		    },
		    [](const Sim::SimWorld &, entt::entity, std::span<const std::string>, std::uint64_t memory)
		    { return memory; });
		trees->behaviours.DefineTree(
		    "shift",
		    {Composite(
		        NodeKind::Repeat, {Composite(
		                              NodeKind::Sequence, {WaitNode(2.0), TaskNode("doze", {"500"}),
		                                                   TaskNode("count")})})});
		trees->Follow("shift");
	}

	constexpr std::uint64_t kEnd = 6600;
	int calls = 0;
	while (forwarded.world.Clock().Step() < kEnd)
	{
		forwarded.world.FastForward(kEnd);
		++calls;
		while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
			stepped.world.Step();
		REQUIRE(forwarded.world.Save() == stepped.world.Save());
	}

	// Ten rounds of 621 steps (a start, the wait's 120, the doze's 500), each a few calls.
	const entt::registry &registry = forwarded.world.Registry();
	CHECK(registry.get<Tally>(registry.view<Ai::Behaviour>().front()).count == 10);
	MESSAGE(calls << " calls");
	CHECK(calls < 60);
}


TEST_CASE(
    "Behaviour trees: reactive nodes check what comes before a running branch each tick, and halt it")
{
	Trees trees;

	// It works while it is fit, and stops once it is not.
	trees.behaviours.DefineTree(
	    "work",
	    {Composite(
	        NodeKind::ReactiveSequence, {TaskNode("while", {"fit"}), TaskNode("hold", {"work"})})});

	// An NPC on an errand breaks off to flee while it is threatened, and goes back to it after.
	trees.behaviours.DefineTree(
	    "errand",
	    {Composite(
	        NodeKind::ReactiveSelector, {Composite(
	                                         NodeKind::ReactiveSequence, {TaskNode("while", {"threat"}),
	                                                                      TaskNode("hold", {"flee"})}),
	                                     TaskNode("hold", {"errand"})})});
	entt::registry &registry = trees.world.Registry();
	const entt::entity worker = trees.Follow("work");
	registry.emplace<Ai::Blackboard>(worker).integers["fit"] = 1;
	CHECK(trees.Tick(worker, Status::Running) == Trace{"fit+", "work^"});
	CHECK(trees.Tick(worker, Status::Running) == Trace{"fit+", "work."});
	registry.get<Ai::Blackboard>(worker).integers["fit"] = 0;
	CHECK(trees.Tick(worker, Status::Failure) == Trace{"fit-", "work!"});

	const entt::entity npc = trees.Follow("errand");
	CHECK(trees.Tick(npc, Status::Running) == Trace{"threat-", "errand^"});
	CHECK(trees.Tick(npc, Status::Running) == Trace{"threat-", "errand."});
	registry.emplace<Ai::Blackboard>(npc).integers["threat"] = 1;
	CHECK(trees.Tick(npc, Status::Running) == Trace{"threat+", "flee^", "errand!"});
	CHECK(trees.Tick(npc, Status::Running) == Trace{"threat+", "flee."});
	registry.get<Ai::Blackboard>(npc).integers["threat"] = 0;
	CHECK(trees.Tick(npc, Status::Running) == Trace{"threat-", "flee!", "errand^"});
}


TEST_CASE("Behaviour trees: a parallel runs its children together, and halts those left once decided")
{
	Trees trees;
	trees.behaviours.DefineTree(
	    "escort",
	    {Composite(
	        NodeKind::Parallel, {TaskNode("hold", {"guard"}), TaskNode("run", {"fly", "2"})}, 1)});
	trees.behaviours.DefineTree(
	    "all",
	    {Composite(NodeKind::Parallel, {TaskNode("succeed", {"a"}), TaskNode("run", {"b", "2"})})});
	trees.behaviours.DefineTree(
	    "both", {Composite(NodeKind::Parallel, {TaskNode("fail", {"a"}), TaskNode("hold", {"b"})})});

	// One success of two is enough: the guard is halted as the flight ends.
	const entt::entity escort = trees.Follow("escort");
	CHECK(trees.Tick(escort, Status::Running) == Trace{"guard^", "fly^"});
	CHECK(trees.Tick(escort, Status::Success) == Trace{"guard.", "fly.", "guard!"});
	CHECK(trees.Tick(escort, Status::Running) == Trace{"guard^", "fly^"});

	// With no count every child must succeed, and one finished is not ticked again.
	const entt::entity all = trees.Follow("all");
	CHECK(trees.Tick(all, Status::Running) == Trace{"a+", "b^"});
	CHECK(trees.Tick(all, Status::Success) == Trace{"b."});

	// A failure it cannot make up for fails it at once.
	CHECK(trees.Tick(trees.Follow("both"), Status::Failure) == Trace{"a-", "b^", "b!"});

	BehaviourNode greedy = Composite(NodeKind::Parallel, {TaskNode("succeed"), TaskNode("succeed")}, 3);
	CHECK_THROWS_WITH_AS(
	    trees.behaviours.DefineTree("greedy", {greedy}),
	    "Behaviour 'greedy' has, at root, a parallel needing 3 of its 2 children",
	    Serialise::FormatError);
	BehaviourNode crowd = Composite(NodeKind::Parallel, {});
	crowd.children.assign(Ai::kParallelChildren + 1, TaskNode("succeed"));
	CHECK_THROWS_WITH_AS(
	    trees.behaviours.DefineTree("crowd", {crowd}),
	    "Behaviour 'crowd' has, at root, a parallel with 33 children, more than 32",
	    Serialise::FormatError);
}


TEST_CASE("Behaviour trees: a subtree is the tree it names in its place, one defined before it")
{
	Trees trees;
	trees.behaviours.DefineTree(
	    "leg",
	    {Composite(NodeKind::Sequence, {TaskNode("succeed", {"a"}), TaskNode("succeed", {"b"})})});
	BehaviourNode leg;
	leg.kind = NodeKind::Subtree;
	leg.tree = "leg";
	trees.behaviours.DefineTree("trip", {Composite(NodeKind::Sequence, {leg, leg})});
	CHECK(trees.behaviours.NodeCount("trip") == 7);
	CHECK(trees.Tick(trees.Follow("trip"), Status::Success) == Trace{"a+", "b+", "a+", "b+"});

	BehaviourNode later = leg;
	later.tree = "later";
	CHECK_THROWS_WITH_AS(
	    trees.behaviours.DefineTree("early", {later}),
	    "Behaviour 'early' has, at root, the tree 'later', which is not defined",
	    Serialise::FormatError);
	BehaviourNode nameless;
	nameless.kind = NodeKind::Subtree;
	CHECK_THROWS_WITH_AS(
	    trees.behaviours.DefineTree("nameless", {nameless}),
	    "Behaviour 'nameless' has, at root, a subtree that names no tree", Serialise::FormatError);
	CHECK_THROWS_WITH_AS(
	    trees.behaviours.DefineTree("slow", {TaskNode("succeed"), -1.0}),
	    "Behaviour 'slow' has an interval of -1 seconds", Serialise::FormatError);
}


TEST_CASE(
    "Behaviour trees: a command gives an entity a tree, halting the one it had; the helm halts it too, "
    "and the blackboard is kept")
{
	const Sim::Controller one{Sim::ControllerKind::Player, 1};
	const Sim::Controller two{Sim::ControllerKind::Player, 2};
	Trees trees;
	trees.world.RegisterComponent<Sim::ControlledBy>();
	trees.behaviours.DefineTree("watch", {TaskNode("hold", {"watch"})});
	trees.behaviours.DefineTree("patrol", {TaskNode("hold", {"patrol"})});
	entt::registry &registry = trees.world.Registry();
	const entt::entity guard = registry.create();
	registry.emplace<Sim::ControlledBy>(guard, one);
	registry.emplace<Ai::Blackboard>(guard).entities["post"] = guard;
	const entt::entity gone = registry.create();
	registry.destroy(gone);
	const auto id = [](entt::entity entity) { return entt::to_integral(entity); };
	const auto step = [&]
	{
		trees.world.Step();
		std::vector<std::string> said = std::exchange(trees.trace, {});
		for (const Sim::SimEvent &event : trees.world.Events().Take())
			if (const auto *assigned = event.event.try_cast<Ai::BehaviourAssigned>())
				said.push_back(std::format("assigned {} '{}'", id(assigned->entity), assigned->tree));
			else if (const auto *refused = event.event.try_cast<Ai::BehaviourRefused>())
				said.push_back(std::format("refused {} {}", id(refused->entity), refused->reason.key));
		return said;
	};

	// Applied before the step's trees are ticked, so the tree starts in the same step.
	trees.world.Commands().Push(Ai::AssignBehaviour{one, guard, "watch"});
	CHECK(step() == Trace{"watch^", std::format("assigned {} 'watch'", id(guard))});
	trees.world.Commands().Push(Ai::AssignBehaviour{one, guard, "patrol"});
	CHECK(step() == Trace{"watch!", "patrol^", std::format("assigned {} 'patrol'", id(guard))});

	trees.world.Commands().Push(Ai::AssignBehaviour{two, guard, "watch"});
	trees.world.Commands().Push(Ai::AssignBehaviour{one, guard, "nowhere"});
	trees.world.Commands().Push(Ai::AssignBehaviour{one, gone, "watch"});
	CHECK(
	    step() == Trace{
	                  "patrol.", std::format("refused {} reason.ai.not_controller", id(guard)),
	                  std::format("refused {} reason.ai.no_such_tree", id(guard)),
	                  std::format("refused {} reason.ai.no_such_entity", id(gone))});

	// No tree, from the game's rules: it follows none, and keeps what it remembered.
	trees.world.Commands().Push(Ai::AssignBehaviour{{}, guard, ""});
	CHECK(step() == Trace{"patrol!", std::format("assigned {} ''", id(guard))});
	CHECK_FALSE(registry.all_of<Ai::Behaviour>(guard));
	CHECK(registry.get<Ai::Blackboard>(guard).entities.at("post") == guard);

	// Taking the helm halts the running task; leaving it starts the tree from the top.
	trees.world.Commands().Push(Ai::AssignBehaviour{one, guard, "watch"});
	step();
	trees.world.Commands().Push(Ai::TakeHelm{one, guard});
	CHECK(step() == Trace{"watch!"});
	CHECK(step().empty());
	trees.world.Commands().Push(Ai::LeaveHelm{one, guard});
	CHECK(step() == Trace{"watch^"});

	// The blackboard is saved with the world.
	Trees loaded;
	loaded.world.RegisterComponent<Sim::ControlledBy>();
	loaded.behaviours.DefineTree("watch", {TaskNode("hold", {"watch"})});
	loaded.behaviours.DefineTree("patrol", {TaskNode("hold", {"patrol"})});
	loaded.world.Load(trees.world.Save());
	CHECK(loaded.world.Registry().get<Ai::Blackboard>(guard) == registry.get<Ai::Blackboard>(guard));
	CHECK(loaded.world.StateHash() == trees.world.StateHash());
}


TEST_CASE(
    "Behaviour trees: a tree with an interval is ticked once in it, its entities spread over it, and "
    "the steps between passed over")
{
	Trees spread;
	Trees forwarded;
	Trees stepped;
	for (Trees *trees : {&spread, &forwarded, &stepped})
	{
		// A tenth of a second: six steps.
		trees->behaviours.DefineTree("slow", {TaskNode("count"), 0.1});
		for (int entity = 0; entity < 3; ++entity)
			trees->Follow("slow");
	}
	const auto counts = [](const Trees &trees)
	{
		const entt::registry &registry = trees.world.Registry();
		std::vector<std::int64_t> counted;
		for (const auto [entity, behaviour] : registry.view<const Ai::Behaviour>().each())
		{
			const auto *tally = registry.try_get<Tally>(entity);
			counted.push_back(tally ? tally->count : 0);
		}
		std::ranges::sort(counted);
		return counted;
	};
	spread.world.Step();
	CHECK(counts(spread) == std::vector<std::int64_t>{0, 0, 1});
	for (int step = 1; step < 6; ++step)
		spread.world.Step();
	CHECK(counts(spread) == std::vector<std::int64_t>{1, 1, 1});

	constexpr std::uint64_t kEnd = 600;
	int calls = 0;
	while (forwarded.world.Clock().Step() < kEnd)
	{
		forwarded.world.FastForward(kEnd);
		++calls;
		while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
			stepped.world.Step();
		REQUIRE(forwarded.world.Save() == stepped.world.Save());
	}
	CHECK(counts(stepped) == std::vector<std::int64_t>{100, 100, 100});

	// Fewer calls than steps: the steps between an interval's three ticks are passed over.
	MESSAGE(calls << " calls");
	CHECK(calls < static_cast<int>(kEnd));
}


TEST_CASE(
    "Behaviour trees: a tree its own task gives again is halted after the tick and starts from the top")
{
	// On its second tick the task gives its entity its own tree again, as a command applied at once.
	Trees trees;
	int ticks = 0;
	trees.behaviours.DefineTask(
	    "again",
	    [&](Ai::TaskContext &context)
	    {
		    ++context.memory;
		    if (++ticks == 2)
			    context.world.Apply(Ai::AssignBehaviour{{}, context.entity, "errand"});
		    return Status::Running;
	    },
	    {}, [&](Ai::TaskContext &) { trees.trace.emplace_back("!"); });
	trees.behaviours.DefineTree("errand", {TaskNode("again")});
	const entt::entity ship = trees.Follow("errand");
	CHECK(trees.Tick(ship, Status::Running).empty());

	// Halted as its tick ends, and the tree it was given again has nothing of the old run.
	CHECK(trees.Tick(ship, Status::Failure) == Trace{"!"});
	CHECK(trees.world.Registry().get<Ai::Behaviour>(ship).nodes.empty());
	CHECK(trees.Tick(ship, Status::Running).empty());
	CHECK(trees.world.Registry().get<Ai::Behaviour>(ship).nodes.at(0).memory == 1);
}


TEST_CASE("Behaviour trees: a task that never wakes leaves an interval tree's steps passed over")
{
	Trees trees;
	trees.behaviours.DefineTask(
	    "forever", [](Ai::TaskContext &) { return Status::Running; },
	    [](const Sim::SimWorld &, entt::entity, std::span<const std::string>, std::uint64_t)
	    { return std::numeric_limits<std::uint64_t>::max(); });
	Ai::BehaviourDefinition idle{TaskNode("forever")};
	idle.interval = 6.0 * kStep;
	trees.behaviours.DefineTree("idle", idle);
	trees.Follow("idle");
	trees.world.Step();

	// To step 1,000 in a few calls, not one a step: never is not rounded round to a step soon.
	constexpr std::uint64_t kEnd = 1000;
	int calls = 0;
	while (trees.world.Clock().Step() < kEnd && calls < 100)
	{
		trees.world.FastForward(kEnd);
		++calls;
	}
	CHECK(trees.world.Clock().Step() == kEnd);
	CHECK(calls < 5);
}


TEST_CASE(
    "Behaviour trees: a tree stopped by its task after that task ticked another's is still halted")
{
	// The outer task ticks a second entity's tree through Tick, then gives its own entity its tree
	// again: the stop is the outer tick's, kept apart from the inner one's.
	Trees trees;
	trees.behaviours.DefineTree("idle", {TaskNode("succeed", {"other"})});
	const entt::entity other = trees.Follow("idle");
	int ticks = 0;
	trees.behaviours.DefineTask(
	    "outer",
	    [&](Ai::TaskContext &context)
	    {
		    ++context.memory;
		    (void)trees.behaviours.Tick(context.world, other);
		    if (++ticks == 2)
			    context.world.Apply(Ai::AssignBehaviour{{}, context.entity, "outer"});
		    return Status::Running;
	    },
	    {}, [&](Ai::TaskContext &) { trees.trace.emplace_back("!"); });
	trees.behaviours.DefineTree("outer", {TaskNode("outer")});
	const entt::entity ship = trees.Follow("outer");
	CHECK(trees.Tick(ship, Status::Running) == Trace{"other+"});
	CHECK(trees.Tick(ship, Status::Failure) == Trace{"other+", "!"});
	CHECK(trees.world.Registry().get<Ai::Behaviour>(ship).nodes.empty());
}


TEST_CASE(
    "Behaviour trees: a tree stopped inside a tick of its own entity's, begun by its task, is halted")
{
	// The task ticks its own entity's tree again, and in that tick gives the entity its tree again:
	// neither tick writes its nodes over the new tree's.
	Trees trees;
	int depth = 0;
	trees.behaviours.DefineTask(
	    "again",
	    [&](Ai::TaskContext &context)
	    {
		    ++context.memory;
		    ++depth;
		    if (depth == 1)
			    (void)trees.behaviours.Tick(context.world, context.entity);
		    else
			    context.world.Apply(Ai::AssignBehaviour{{}, context.entity, "again"});
		    --depth;
		    return Status::Running;
	    },
	    {}, [&](Ai::TaskContext &) { trees.trace.emplace_back("!"); });
	trees.behaviours.DefineTree("again", {TaskNode("again")});
	const entt::entity ship = trees.Follow("again");
	CHECK(trees.behaviours.Tick(trees.world, ship) == Status::Failure);
	CHECK(trees.world.Registry().get<Ai::Behaviour>(ship).nodes.empty());
}


TEST_CASE("Behaviour trees: a tree a task makes due this step is ticked this step, and once")
{
	// The first entity's task gives the second its first tree, due every step, and lets the third's
	// helm go: both are ticked in the step, as an assignment starts its tree in the step it comes in.
	Trees trees;
	trees.behaviours.DefineTree("tally", {TaskNode("count")});
	entt::entity third = entt::null;
	entt::entity second = entt::null;
	int changes = 0;
	trees.behaviours.DefineTask(
	    "change",
	    [&](Ai::TaskContext &context)
	    {
		    ++changes;
		    context.world.Apply(Ai::AssignBehaviour{{}, second, "tally"});
		    context.world.Registry().remove<Sim::Helm>(third);
		    return Status::Success;
	    });
	trees.behaviours.DefineTree("change", {TaskNode("change")});
	trees.Follow("change");
	second = trees.world.Registry().create();
	third = trees.Follow("tally");
	trees.world.Registry().emplace<Sim::Helm>(third);
	trees.world.Step();
	CHECK(changes == 1);
	CHECK(trees.world.Registry().all_of<Tally>(second));
	CHECK(trees.world.Registry().all_of<Tally>(third));
}


TEST_CASE("Behaviour trees: an interval or a wait longer than a run counts in steps is refused")
{
	Trees trees;
	Ai::BehaviourDefinition forever{TaskNode("succeed")};
	forever.interval = 1.0e100;
	CHECK_THROWS_AS(trees.behaviours.DefineTree("forever", forever), Serialise::FormatError);
	CHECK_THROWS_AS(trees.behaviours.DefineTree("ages", {WaitNode(1.0e100)}), Serialise::FormatError);
}


TEST_CASE(
    "Behaviour trees: a tree changed by an earlier entity's task this step is ticked as it now is")
{
	// The first entity's task gives the second a tree due only every sixth step, and the third's
	// helm is taken: neither is then ticked this step, though both were due as the step began.
	Trees trees;
	trees.behaviours.DefineTree("tally", {TaskNode("count")});
	Ai::BehaviourDefinition slow{TaskNode("count")};
	slow.interval = 6.0 * kStep;
	trees.behaviours.DefineTree("slow", slow);
	entt::entity second = entt::null;
	entt::entity third = entt::null;
	trees.behaviours.DefineTask(
	    "change",
	    [&](Ai::TaskContext &context)
	    {
		    context.world.Apply(Ai::AssignBehaviour{{}, second, "slow"});
		    context.world.Registry().emplace_or_replace<Sim::Helm>(third, Sim::Helm{});
		    return Status::Success;
	    });
	trees.behaviours.DefineTree("change", {TaskNode("change")});
	trees.Follow("change");
	second = trees.Follow("tally");
	third = trees.Follow("tally");

	// The second's phase in a six-step interval puts it off this step (its due step is not 0).
	REQUIRE(entt::to_entity(second) % 6 != 0);
	trees.world.Step();
	CHECK_FALSE(trees.world.Registry().all_of<Tally>(second));
	CHECK_FALSE(trees.world.Registry().all_of<Tally>(third));
}
