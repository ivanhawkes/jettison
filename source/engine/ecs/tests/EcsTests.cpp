#include <jettison/ecs/Scheduler.h>
#include <jettison/ecs/StateMachine.h>

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace Jettison::Ecs;

namespace
{
struct Position
{
};
struct Velocity
{
};
struct Health
{
};

// A system that records its name when it runs, so a test can read the order back.
SystemDescription Recording(
    std::vector<std::string> &log, std::string name, TickPhase phase = TickPhase::Simulation,
    std::vector<std::string> after = {}, Access access = {})
{
	return {
	    name, phase, std::move(access), std::move(after),
	    [&log, name](entt::registry &, double) { log.push_back(name); }};
}
} // namespace

TEST_CASE("Ecs: the phases are in frame order")
{
	CHECK(TickPhase::Input < TickPhase::Commands);
	CHECK(TickPhase::Commands < TickPhase::Simulation);
	CHECK(TickPhase::Simulation < TickPhase::Sync);
	CHECK(TickPhase::Sync < TickPhase::Presentation);
	CHECK(TickPhase::Presentation < TickPhase::Render);
}

TEST_CASE("Ecs: a phase runs its own systems, in registration order when nothing constrains them")
{
	std::vector<std::string> log;
	Scheduler scheduler;
	scheduler.Register(Recording(log, "economy"));
	scheduler.Register(Recording(log, "camera", TickPhase::Presentation));
	scheduler.Register(Recording(log, "orbits"));
	scheduler.Register(Recording(log, "ai"));

	entt::registry registry;
	scheduler.Run(TickPhase::Simulation, registry, 0.05);
	CHECK(log == std::vector<std::string>{"economy", "orbits", "ai"});
	CHECK(scheduler.Order(TickPhase::Presentation) == std::vector<std::string>{"camera"});
	CHECK(scheduler.Order(TickPhase::Render).empty());
}

TEST_CASE("Ecs: after constraints order a phase, naming systems registered before or after")
{
	std::vector<std::string> log;
	Scheduler scheduler;
	scheduler.Register(Recording(log, "trade", TickPhase::Simulation, {"ai", "economy"}));
	scheduler.Register(Recording(log, "economy"));
	scheduler.Register(Recording(log, "ai", TickPhase::Simulation, {"orbits"}));
	scheduler.Register(Recording(log, "orbits"));
	scheduler.Register(Recording(log, "audit"));
	scheduler.Validate();

	CHECK(
	    scheduler.Order(TickPhase::Simulation) ==
	    std::vector<std::string>{"economy", "orbits", "ai", "trade", "audit"});
	entt::registry registry;
	scheduler.Run(TickPhase::Simulation, registry, 0.05);
	CHECK(log == scheduler.Order(TickPhase::Simulation));
}

TEST_CASE("Ecs: before constraints put a system ahead of one that does not name it")
{
	std::vector<std::string> log;
	Scheduler scheduler;
	const auto make = [&log](
	                      std::string name, std::vector<std::string> after,
	                      std::vector<std::string> before, TickPhase phase = TickPhase::Simulation)
	{
		SystemDescription system = Recording(log, std::move(name), phase, std::move(after));
		system.before = std::move(before);
		return system;
	};
	// flight and physics are another module's, which knows nothing of steering; index names a
	// system registered after it.
	scheduler.Register(Recording(log, "flight"));
	scheduler.Register(Recording(log, "physics", TickPhase::Simulation, {"flight"}));
	scheduler.Register(make("index", {}, {"steering"}));
	scheduler.Register(make("steering", {}, {"flight"}));
	scheduler.Validate();
	CHECK(
	    scheduler.Order(TickPhase::Simulation) ==
	    std::vector<std::string>{"index", "steering", "flight", "physics"});
	// A before orders as an after does: nothing here is ambiguous.
	CHECK(scheduler.Ambiguities(TickPhase::Simulation).empty());

	// late closes physics -> late -> index -> steering -> flight -> physics.
	CHECK_THROWS_WITH_AS(
	    scheduler.Register(make("late", {"physics"}, {"index"})),
	    "Scheduler: the order constraints among 'flight', 'physics', 'index', 'steering', 'late' "
	    "form a cycle",
	    std::logic_error);
	CHECK_THROWS_WITH_AS(
	    scheduler.Register(make("present", {}, {"flight"}, TickPhase::Presentation)),
	    doctest::Contains("'present' runs before 'flight', which is in another phase"),
	    std::logic_error);
	scheduler.Register(make("audit", {}, {"missing"}));
	CHECK_THROWS_WITH_AS(
	    scheduler.Validate(),
	    "Scheduler: system 'audit' runs before 'missing', which is not registered", std::logic_error);
}

TEST_CASE("Ecs: the delta time and the registry reach every system")
{
	Scheduler scheduler;
	double seen = 0.0;
	scheduler.Register(
	    {"spawn",
	     TickPhase::Simulation,
	     {},
	     {},
	     [](entt::registry &registry, double) { (void)registry.create(); }});
	scheduler.Register(
	    {"count",
	     TickPhase::Simulation,
	     {},
	     {"spawn"},
	     [&seen](entt::registry &registry, double delta)
	     { seen = delta * static_cast<double>(registry.view<entt::entity>().size()); }});
	entt::registry registry;
	scheduler.Run(TickPhase::Simulation, registry, 0.25);
	CHECK(seen == 0.25);
}

TEST_CASE("Ecs: registration errors are found at registration, and the scheduler stays usable")
{
	std::vector<std::string> log;
	Scheduler scheduler;
	CHECK_THROWS_WITH_AS(
	    scheduler.Register({"", TickPhase::Input, {}, {}, [](entt::registry &, double) {}}),
	    "Scheduler: a system needs a name", std::logic_error);
	CHECK_THROWS_WITH_AS(
	    scheduler.Register({"empty", TickPhase::Input, {}, {}, {}}),
	    doctest::Contains("has nothing to run"), std::logic_error);

	scheduler.Register(Recording(log, "a", TickPhase::Simulation, {"c"}));
	scheduler.Register(Recording(log, "b", TickPhase::Simulation, {"a"}));
	CHECK_THROWS_WITH_AS(
	    scheduler.Register(Recording(log, "a")), doctest::Contains("already registered"),
	    std::logic_error);
	// c closes a -> c -> b -> a.
	CHECK_THROWS_WITH_AS(
	    scheduler.Register(Recording(log, "c", TickPhase::Simulation, {"b"})),
	    "Scheduler: the order constraints among 'a', 'b', 'c' form a cycle", std::logic_error);
	CHECK(scheduler.Systems().size() == 2);

	CHECK_THROWS_WITH_AS(
	    scheduler.Register(Recording(log, "late", TickPhase::Render, {"a"})),
	    doctest::Contains("'late' runs after 'a', which is in another phase"), std::logic_error);
	// The other direction: a waits for a c that turns up in another phase.
	CHECK_THROWS_WITH_AS(
	    scheduler.Register(Recording(log, "c", TickPhase::Render)),
	    doctest::Contains("'a' runs after 'c', which is in another phase"), std::logic_error);

	// a still names a c that never came.
	CHECK_THROWS_WITH_AS(
	    scheduler.Validate(), "Scheduler: system 'a' runs after 'c', which is not registered",
	    std::logic_error);
	entt::registry registry;
	CHECK_THROWS_AS(scheduler.Run(TickPhase::Simulation, registry, 0.0), std::logic_error);

	scheduler.Register(Recording(log, "c"));
	scheduler.Run(TickPhase::Simulation, registry, 0.0);
	CHECK(log == std::vector<std::string>{"c", "a", "b"});
}

TEST_CASE("Ecs: access conflicts that no constraint orders are reported, in run order")
{
	std::vector<std::string> log;
	Scheduler scheduler;
	scheduler.Register(Recording(
	    log, "integrate", TickPhase::Simulation, {}, Access::Of<Velocity>().Writing<Position>()));
	scheduler.Register(
	    Recording(log, "damage", TickPhase::Simulation, {}, Access::Of<Position>().Writing<Health>()));
	scheduler.Register(
	    Recording(log, "steer", TickPhase::Simulation, {}, Access::Of<Position>().Writing<Velocity>()));
	scheduler.Register(
	    Recording(log, "report", TickPhase::Simulation, {"damage"}, Access::Of<Health>()));
	scheduler.Register(
	    Recording(log, "census", TickPhase::Simulation, {}, Access::Of<Position, Health>()));

	CHECK(Access::Of<Position>().ConflictsWith(Access::Of<Velocity>().Writing<Position>()));
	CHECK_FALSE(Access::Of<Position>().ConflictsWith(Access::Of<Position>()));

	// damage -> report is ordered; census reads what damage writes but nothing orders them.
	const std::vector<std::pair<std::string, std::string>> expected{
	    {"integrate", "damage"}, {"integrate", "steer"}, {"integrate", "census"}, {"damage", "census"}};
	CHECK(scheduler.Ambiguities(TickPhase::Simulation) == expected);
}

TEST_CASE("Ecs: an ordering chain resolves an ambiguity even through a system that touches nothing")
{
	std::vector<std::string> log;
	Scheduler scheduler;
	scheduler.Register(
	    Recording(log, "write", TickPhase::Simulation, {}, Access::Of<>().Writing<Position>()));
	scheduler.Register(Recording(log, "barrier", TickPhase::Simulation, {"write"}));
	scheduler.Register(
	    Recording(log, "read", TickPhase::Simulation, {"barrier"}, Access::Of<Position>()));
	CHECK(scheduler.Ambiguities(TickPhase::Simulation).empty());
}

TEST_CASE("Ecs: an exception from a system stops its phase there")
{
	std::vector<std::string> log;
	Scheduler scheduler;
	scheduler.Register(Recording(log, "first"));
	scheduler.Register(
	    {"fails",
	     TickPhase::Simulation,
	     {},
	     {},
	     [](entt::registry &, double) { throw std::runtime_error{"bad step"}; }});
	scheduler.Register(Recording(log, "last"));
	entt::registry registry;
	CHECK_THROWS_WITH_AS(
	    scheduler.Run(TickPhase::Simulation, registry, 0.0), "bad step", std::runtime_error);
	CHECK(log == std::vector<std::string>{"first"});
}

namespace
{
enum class Flight
{
	Docked,
	Undocking,
	Flying,
};
enum class Order
{
	Undock,
	Clear,
	Dock,
	Abort,
};

StateMachine<Flight, Order> FlightMachine()
{
	StateMachine<Flight, Order> machine{Flight::Docked};
	machine.AddTransition(Flight::Docked, Order::Undock, Flight::Undocking);
	machine.AddTransition(Flight::Undocking, Order::Clear, Flight::Flying);
	machine.AddTransition(Flight::Undocking, Order::Abort, Flight::Docked);
	machine.AddTransition(Flight::Flying, Order::Dock, Flight::Docked);
	return machine;
}
} // namespace

TEST_CASE("Ecs: a state machine follows its table, and ignores an event with no transition")
{
	StateMachine<Flight, Order> machine = FlightMachine();
	CHECK(machine.Current() == Flight::Docked);
	CHECK_FALSE(machine.CanDispatch(Order::Dock));
	CHECK_FALSE(machine.Dispatch(Order::Dock));
	CHECK(machine.Current() == Flight::Docked);

	CHECK(machine.CanDispatch(Order::Undock));
	CHECK(machine.Dispatch(Order::Undock) == Flight::Undocking);
	CHECK(machine.Dispatch(Order::Clear) == Flight::Flying);
	CHECK(machine.Dispatch(Order::Dock) == Flight::Docked);
	CHECK(machine.Current() == Flight::Docked);
}

TEST_CASE("Ecs: a state machine runs exit, then entry, hooks; and refuses a second answer")
{
	StateMachine<Flight, Order> machine = FlightMachine();
	std::vector<std::string> log;
	machine.OnExit(Flight::Docked, [&] { log.emplace_back("exit docked"); });
	machine.OnEnter(Flight::Undocking, [&] { log.emplace_back("enter undocking"); });
	machine.OnExit(Flight::Undocking, [&] { log.emplace_back("exit undocking"); });
	machine.OnEnter(Flight::Docked, [&] { log.emplace_back("enter docked"); });

	(void)machine.Dispatch(Order::Undock);
	(void)machine.Dispatch(Order::Abort);
	CHECK(
	    log ==
	    std::vector<std::string>{"exit docked", "enter undocking", "exit undocking", "enter docked"});

	CHECK_THROWS_AS(
	    machine.AddTransition(Flight::Docked, Order::Undock, Flight::Flying), std::logic_error);
}

TEST_CASE(
    "Ecs: a self-transition runs both hooks; a hook cannot dispatch, and one that throws leaves it "
    "usable")
{
	StateMachine<Flight, Order> machine{Flight::Flying};
	machine.AddTransition(Flight::Flying, Order::Clear, Flight::Flying);
	machine.AddTransition(Flight::Flying, Order::Dock, Flight::Docked);
	int entries = 0;
	machine.OnEnter(Flight::Flying, [&] { ++entries; });
	CHECK(machine.Dispatch(Order::Clear) == Flight::Flying);
	CHECK(entries == 1);

	machine.OnEnter(Flight::Docked, [&] { (void)machine.Dispatch(Order::Clear); });
	CHECK_THROWS_WITH_AS(
	    machine.Dispatch(Order::Dock), "StateMachine: Dispatch was called from a hook",
	    std::logic_error);
	// The state had changed before the entry hook threw.
	CHECK(machine.Current() == Flight::Docked);
	machine.OnEnter(Flight::Docked, [] {});
	CHECK_FALSE(machine.Dispatch(Order::Clear));
}
