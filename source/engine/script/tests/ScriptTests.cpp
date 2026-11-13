#include "TestTypes.h"

#include <jettison/core/Log.h>
#include <jettison/script/ScriptHost.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Jettison;
using ScriptTest::Inner;
using ScriptTest::Mood;
using ScriptTest::Ping;
using ScriptTest::Pong;

namespace
{
// The suite's modules, built from tests/scripts, at the root of a mount.
Vfs::FileSystem Modules()
{
	Vfs::FileSystem files;
	files.AddMount({"modules", JETTISON_SCRIPT_TEST_MODULES, false});
	return files;
}

// The script category's messages while it lives.
class Said
{
  public:
	Said()
	    : m_id{Core::AddLogCallback(
	          [this](const Core::LogRecord &record)
	          {
		          if (record.category == "script")
			          m_lines.emplace_back(record.message);
	          })}
	{
	}
	~Said()
	{
		Core::RemoveLogCallback(m_id);
	}
	Said(const Said &) = delete;
	Said &operator=(const Said &) = delete;

	bool Contains(std::string_view text) const
	{
		for (const std::string &line : m_lines)
			if (line.find(text) != std::string::npos)
				return true;
		return false;
	}

  private:
	Core::LogCallbackId m_id;
	std::vector<std::string> m_lines;
};

Script::ScriptDefinition Limits()
{
	Script::ScriptDefinition definition;
	definition.module = "limits.wasm";
	return definition;
}

// The host API's module, in C and in Rust: each test of it runs against both.
constexpr std::array<const char *, 2> kEventModules{"events.wasm", "events_rs.wasm"};

Script::ScriptDefinition Events(const char *module = kEventModules[0])
{
	Script::ScriptDefinition definition;
	definition.module = module;
	definition.capabilities = {"log", "send:Test.Pong"};
	definition.events = {{"Test.Ping", "on_ping"}};
	definition.inputs = {
	    {"doubled", "doubled"},
	    {"answers", "answers"},
	    {"unknown", "unknown"},
	    {"sends", "sends"},
	    {"not-a-number", "not_a_number"}};
	return definition;
}

// A world that applies the suite's command, keeping what it is sent.
struct World
{
	Sim::SimWorld world{1.0 / 60.0, 1};
	std::vector<Pong> applied;

	World()
	{
		ScriptTest::RegisterTypes();
		world.RegisterCommand<Pong>([this](Sim::SimWorld &, const Pong &pong)
		                            { applied.push_back(pong); });
	}
};

std::vector<Pong> Queued(Sim::SimWorld &world)
{
	std::vector<Pong> pongs;
	for (const entt::meta_any &command : world.Commands().Pending())
		if (const auto *pPong = command.try_cast<Pong>())
			pongs.push_back(*pPong);
	return pongs;
}

std::uint64_t Traps(const Script::ScriptHost &host, std::string_view name)
{
	for (const Script::ScriptStatus &status : host.Scripts())
		if (status.name == name)
			return status.traps;
	return 0;
}
} // namespace

TEST_CASE("Script: a module loads and its export is called")
{
	const Vfs::FileSystem files = Modules();
	Script::ScriptHost host{files};
	CHECK(host.LoadedModules() == 0);
	host.Load("limits", Limits());
	CHECK(host.LoadedModules() == 1);
	const std::array arguments{2, 40};
	CHECK(host.Call("limits", "add", arguments) == std::vector<std::int32_t>{42});
	CHECK_THROWS_AS(host.Call("limits", "missing", {}), std::invalid_argument);
	CHECK_THROWS_AS(host.Call("nothing", "add", arguments), std::invalid_argument);
	host.Unload("limits");
	CHECK(host.LoadedModules() == 0);
}

TEST_CASE("Script: a call out of fuel traps, and the script is called again after")
{
	const Vfs::FileSystem files = Modules();
	Script::ScriptHost host{files};
	host.Load("limits", Limits());
	const Said said;
	CHECK_FALSE(host.Call("limits", "spin", {}).has_value());
	CHECK(said.Contains("Script 'limits': spin trapped: Exception: instruction limit exceeded"));
	CHECK(Traps(host, "limits") == 1);
	const std::array arguments{1, 2};
	CHECK(host.Call("limits", "add", arguments) == std::vector<std::int32_t>{3});
}

TEST_CASE("Script: the fuel a call needs is the same on every run")
{
	const Vfs::FileSystem files = Modules();
	// The least fuel burn(1000) runs on, found by halving.
	const auto least = [&]
	{
		std::uint32_t low = 1;
		std::uint32_t high = 1'000'000;
		while (low < high)
		{
			const std::uint32_t middle = low + (high - low) / 2;
			Script::ScriptHost host{files};
			Script::ScriptDefinition definition = Limits();
			definition.fuel = middle;
			host.Load("limits", definition);
			const std::array turns{1000};
			if (host.Call("limits", "burn", turns))
				high = middle;
			else
				low = middle + 1;
		}
		return low;
	};
	const std::uint32_t first = least();
	CHECK(first > 1000);
	CHECK(first < 1'000'000);
	CHECK(least() == first);
}

TEST_CASE("Script: a call past the time limit is stopped, and its script is not called again")
{
	const Vfs::FileSystem files = Modules();
	Script::HostLimits limits;
	limits.fuel = 2'000'000'000;
	limits.time = std::chrono::milliseconds{50};
	Script::ScriptHost host{files, limits};
	Script::ScriptDefinition definition = Limits();
	definition.fuel = limits.fuel;
	host.Load("limits", definition);
	const Said said;
	const auto start = std::chrono::steady_clock::now();
	CHECK_FALSE(host.Call("limits", "spin", {}).has_value());
	CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds{2});
	CHECK(said.Contains("spin ran past the time limit of 50 ms"));
	CHECK(host.Scripts().front().stopped);
	const std::array arguments{1, 2};
	CHECK_FALSE(host.Call("limits", "add", arguments).has_value());
}

TEST_CASE("Script: memory grows only as far as the definition allows, and traps are survived")
{
	const Vfs::FileSystem files = Modules();
	Script::ScriptHost host{files};
	host.Load("limits", Limits());
	const std::array tooMany{1000};
	CHECK(host.Call("limits", "grow", tooMany) == std::vector<std::int32_t>{-1});
	const std::array one{1};
	const std::optional<std::vector<std::int32_t>> before = host.Call("limits", "grow", one);
	REQUIRE(before.has_value());
	CHECK(before->front() > 0);
	CHECK(before->front() < 16);

	const Said said;
	CHECK_FALSE(host.Call("limits", "wild", {}).has_value());
	CHECK(said.Contains("out of bounds memory access"));
	CHECK_FALSE(host.Call("limits", "boom", {}).has_value());
	CHECK(said.Contains("unreachable"));
	const std::array deep{1'000'000};
	CHECK_FALSE(host.Call("limits", "deep", deep).has_value());
	CHECK(Traps(host, "limits") == 3);
}

TEST_CASE("Script: the handlers' instance keeps what it counts until a trap makes it again")
{
	const Vfs::FileSystem files = Modules();
	Script::ScriptHost host{files};
	host.Load("limits", Limits());
	CHECK(host.Call("limits", "count", {}) == std::vector<std::int32_t>{1});
	CHECK(host.Call("limits", "count", {}) == std::vector<std::int32_t>{2});
	CHECK_FALSE(host.Call("limits", "boom", {}).has_value());
	CHECK(host.Call("limits", "count", {}) == std::vector<std::int32_t>{1});
}

TEST_CASE("Script: a module is refused for what it imports and is not granted")
{
	for (const char *module : kEventModules)
	{
		CAPTURE(module);
		World world;
		const Vfs::FileSystem files = Modules();
		Script::ScriptHost host{files};
		host.SendCommandsTo(world.world);

		SUBCASE("a command it sends")
		{
			Script::ScriptDefinition definition = Events(module);
			definition.capabilities = {"log"};
			CHECK_THROWS_WITH_AS(
			    host.Load("events", definition),
			    "Script 'events': it imports 'send:Test.Pong', which it is not granted",
			    Script::ScriptError);
		}
		SUBCASE("the log")
		{
			Script::ScriptDefinition definition = Events(module);
			definition.capabilities = {"send:Test.Pong"};
			CHECK_THROWS_WITH_AS(
			    host.Load("events", definition),
			    "Script 'events': it imports 'log', which it is not granted", Script::ScriptError);
		}
		SUBCASE("the inputs, for a script that answers none")
		{
			Script::ScriptDefinition definition = Events(module);
			definition.inputs.clear();
			CHECK_THROWS_WITH_AS(
			    host.Load("events", definition),
			    "Script 'events': it imports 'input', which it is not granted", Script::ScriptError);
		}
		SUBCASE("a function of another module")
		{
			Script::ScriptDefinition definition;
			definition.module = "rogue.wasm";
			CHECK_THROWS_WITH_AS(
			    host.Load("rogue", definition),
			    "Script 'rogue': it imports 'abort' from 'env', which the host does not offer",
			    Script::ScriptError);
		}
		CHECK(host.LoadedModules() == 0);
	}
}

TEST_CASE("Script: a definition is refused for what does not fit it")
{
	World world;
	const std::filesystem::path scratch =
	    std::filesystem::path{JETTISON_SCRIPT_TEST_OUTPUT} / "garbage-scripts";
	std::filesystem::create_directories(scratch);
	std::ofstream{scratch / "garbage.wasm"} << "not a module";
	Vfs::FileSystem files = Modules();
	files.AddMount({"scratch", scratch, false});
	Script::ScriptHost host{files};
	const auto refused = [&](const Script::ScriptDefinition &definition)
	{
		try
		{
			host.Load("events", definition);
		}
		catch (const Script::ScriptError &error)
		{
			return std::string{error.what()};
		}
		return std::string{};
	};

	Script::ScriptDefinition definition = Events();
	CHECK(refused(definition) == "Script 'events': 'send:Test.Pong' grants a command nothing applies");
	host.SendCommandsTo(world.world);
	CHECK(refused(definition).empty());

	definition = Events();
	definition.capabilities.emplace_back("fly");
	CHECK(refused(definition) == "Script 'events': 'fly' is not a capability: log, or send:<command>");
	definition = Events();
	definition.capabilities.emplace_back("send:Test.Ping");
	CHECK(refused(definition) == "Script 'events': 'send:Test.Ping' grants a command nothing applies");
	definition = Events();
	definition.events.push_back({"Test.Inner", "on_ping"});
	CHECK(
	    refused(definition) ==
	    "Script 'events': it handles 'Test.Inner', which is not an event scripts see");
	definition = Events();
	definition.events.push_back({"Test.Ping", "pings"});
	CHECK(
	    refused(definition) ==
	    "Script 'events': its handler 'pings' is not exported as void pings(const Test_Ping *)");
	definition = Events();
	definition.inputs.push_back({"handled", "on_ping"});
	CHECK(
	    refused(definition) ==
	    "Script 'events': its input 'on_ping' is not exported as double on_ping(void)");
	definition = Events();
	definition.inputs.push_back({"doubled", "answers"});
	CHECK(refused(definition) == "Script 'events': it answers input 'doubled' twice");
	definition = Events();
	definition.fuel = 20'000'000;
	CHECK(
	    refused(definition) ==
	    "Script 'events': its fuel of 20000000 is not between 1 and the host's 10000000");
	definition = Events();
	definition.memoryPages = 0;
	CHECK(
	    refused(definition) ==
	    "Script 'events': its 0 pages of memory are not between 1 and the host's 256");
	definition = Events();
	definition.module = "missing.wasm";
	CHECK(refused(definition) == "Script 'events': there is no module at 'missing.wasm'");
	definition = Events();
	definition.module = "garbage.wasm";
	CHECK(refused(definition)
	          .starts_with("Script 'events': 'garbage.wasm' is not a module it can run: "));

	// A second script may not answer an input the first does.
	host.Load("events", Events());
	Script::ScriptDefinition other;
	other.module = "events.wasm";
	other.capabilities = {"log", "send:Test.Pong"};
	other.inputs = {{"doubled", "doubled"}};
	CHECK_THROWS_WITH_AS(
	    host.Load("other", other),
	    "Script 'other': input 'doubled' is answered by script 'events' already", Script::ScriptError);
}

TEST_CASE("Script: an event is copied in for its handler, which sends a command into the world's queue")
{
	for (const char *module : kEventModules)
	{
		CAPTURE(module);
		World world;
		const Vfs::FileSystem files = Modules();
		Script::ScriptHost host{files};
		host.SendCommandsTo(world.world);
		host.Load("events", Events(module));

		const Said said;
		Ping ping;
		ping.ship = entt::entity{12};
		ping.count = 100;
		ping.text = "a ping";
		ping.numbers = {1, 2, 3};
		ping.inner = {Mood::Cross, 9};
		ping.where = {1.0, 2.0, 4.0};
		ping.flag = true;
		host.Dispatch(Sim::SimEvent{5, entt::meta_any{ping}});
		CHECK(said.Contains("events: a ping"));
		ping.flag = false;
		ping.text.clear();
		host.Dispatch(Sim::SimEvent{6, entt::meta_any{ping}});
		// An event no script handles goes nowhere.
		host.Dispatch(Sim::SimEvent{6, entt::meta_any{Inner{}}});

		const std::vector<Pong> sent = Queued(world.world);
		REQUIRE(sent.size() == 2);
		CHECK(sent[0].ship == entt::entity{12});
		CHECK(sent[0].total == 106 + 7);
		CHECK(sent[0].text == "a ping");
		CHECK(sent[0].items == std::vector<Inner>{{Mood::Cross, 9}, {Mood::Cross, 1}});
		CHECK(sent[1].total == 106);
		CHECK(sent[1].text.empty());
		// The second ping is the handler's second: what it counted was kept.
		CHECK(sent[1].items[1].weight == 2);
		CHECK(host.Call("events", "pings", {}) == std::vector<std::int32_t>{2});

		// Applied as the step starts, as a player's are.
		world.world.Step();
		CHECK(world.applied.size() == 2);
	}
}

TEST_CASE("Script: a command whose memory does not read is refused, and nothing is sent")
{
	for (const char *module : kEventModules)
	{
		CAPTURE(module);
		World world;
		const Vfs::FileSystem files = Modules();
		Script::ScriptHost host{files};
		host.SendCommandsTo(world.world);
		Script::ScriptDefinition definition = Events(module);
		definition.events = {{"Test.Ping", "bad_send"}};
		host.Load("events", definition);
		const Said said;
		host.Dispatch(Sim::SimEvent{1, entt::meta_any{Ping{}}});
		CHECK(said.Contains(
		    "bad_send trapped: Script: Test.Pong.text: 100 bytes at 2147483632 are past the end"));
		CHECK(Queued(world.world).empty());
		CHECK(Traps(host, "events") == 1);
	}
}

TEST_CASE(
    "Script: an input is the script's, worked out from the option's other inputs, afresh each call")
{
	for (const char *module : kEventModules)
	{
		CAPTURE(module);
		World world;
		const Vfs::FileSystem files = Modules();
		Script::ScriptHost host{files};
		host.SendCommandsTo(world.world);
		host.Load("events", Events(module));

		const Script::Inputs game = [](std::string_view input) -> double
		{
			if (input == "base")
				return 21.0;
			throw std::invalid_argument{std::format("no input '{}'", input)};
		};
		CHECK(host.Answers("doubled"));
		CHECK_FALSE(host.Answers("base"));
		CHECK(host.Input("doubled", game) == 42.0);
		// The same answer every call: nothing the last call did is seen by the next.
		CHECK(host.Input("answers", game) == 1.0);
		CHECK(host.Input("answers", game) == 1.0);
		CHECK_THROWS_AS(host.Input("base", game), std::invalid_argument);

		const Script::Inputs inputs = host.WithScripts(game);
		CHECK(inputs("doubled") == 42.0);
		CHECK(inputs("base") == 21.0);

		const Said said;
		CHECK(inputs("unknown") == 0.0);
		CHECK(said.Contains("unknown trapped: input 'nothing': no input 'nothing'"));
		CHECK(inputs("sends") == 0.0);
		CHECK(said.Contains("sends trapped: sent a 'Test.Pong' while answering an input"));
		CHECK(Queued(world.world).empty());
		CHECK(inputs("not-a-number") == 0.0);
		CHECK(said.Contains("input 'not-a-number' answered nan, read as 0"));
		CHECK(Traps(host, "events") == 3);
	}
}

TEST_CASE("Script: every script the registry defines loads, and one that does not is left out")
{
	World world;
	const std::filesystem::path scratch =
	    std::filesystem::path{JETTISON_SCRIPT_TEST_OUTPUT} / "registry-scripts";
	std::filesystem::remove_all(scratch);
	std::filesystem::create_directories(scratch / "defs/scripts");
	std::ofstream{scratch / "defs/scripts/events.yaml"}
	    << "version: 1\nmodule: events.wasm\ncapabilities: [log, send:Test.Pong]\n"
	       "events:\n  - {event: Test.Ping, function: on_ping}\n"
	       "inputs:\n  - {input: doubled, function: doubled}\n";
	std::ofstream{scratch / "defs/scripts/rogue.yaml"} << "version: 1\nmodule: rogue.wasm\n";

	Vfs::FileSystem files = Modules();
	files.AddMount({"defs", scratch, false});
	Sim::DefinitionRegistry definitions{files};
	Script::LoadDefinitions(definitions);
	Script::ScriptHost host{files};
	host.SendCommandsTo(world.world);
	const Said said;
	CHECK(host.LoadAll(definitions) == 1);
	CHECK(said.Contains(
	    "Script 'events': events.wasm loaded, 1 handler(s), 1 input(s), granted log, send:Test.Pong"));
	CHECK(said.Contains(
	    "Script 'rogue': it imports 'abort' from 'env', which the host does not offer; it is left "
	    "out"));
	REQUIRE(host.Scripts().size() == 1);
	CHECK(host.Scripts().front().name == "events");

	// The header is every type Reflect marks Script, and a send for each the world applies.
	const std::string header = host.WriteHeader();
	CHECK(header.find("typedef struct Test_Ping") != std::string::npos);
	CHECK(header.find("jettison_send_Test_Pong") != std::string::npos);
	CHECK(header.find("jettison_send_Test_Ping") == std::string::npos);
}
