#include <jettison/host/Package.h>
#include <jettison/host/Session.h>
#include <jettison/host/WorldContent.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Binary.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/vfs/FileSystem.h>
#include <jettison/vfs/Pack.h>

using namespace Jettison;

namespace
{
class Scratch
{
  public:
	Scratch()
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-host-tests-" + std::to_string(getpid()))}
	{
		std::filesystem::remove_all(m_directory);
		std::filesystem::create_directories(m_directory);
	}


	~Scratch()
	{
		std::error_code error;
		std::filesystem::remove_all(m_directory, error);
	}


	Scratch(const Scratch &) = delete;


	Scratch &operator=(const Scratch &) = delete;


	void Put(const std::string &path, const std::string &contents) const
	{
		const auto file = m_directory / path;
		std::filesystem::create_directories(file.parent_path());
		std::ofstream{file, std::ios::binary} << contents;
	}


	std::filesystem::path Path(const std::string &name) const
	{
		return m_directory / name;
	}

  private:
	std::filesystem::path m_directory;
};


struct Crop
{
	std::string displayName;
	int yield{};
};


struct Field
{
	int planted{};
};


void RegisterTestTypes()
{
	static const bool registered = []
	{
		using Reflect::Attribute;
		Reflect::RegisterType<Crop>("HostTests.Crop")
		    .Field<&Crop::displayName>("displayName", Attribute::Saved)
		    .Field<&Crop::yield>("yield", Attribute::Saved);
		Reflect::RegisterType<Field>("HostTests.Field")
		    .Field<&Field::planted>("planted", Attribute::Saved);
		return true;
	}();
	(void)registered;
}


// A service that says when it goes, so teardown's order can be seen.
class Probe : public Sim::WorldService
{
  public:
	explicit Probe(std::vector<std::string> &log) : m_log{log}
	{
	}


	~Probe() override
	{
		m_log.emplace_back("world");
	}


	Probe(const Probe &) = delete;


	Probe &operator=(const Probe &) = delete;


	void Save(Serialise::BinaryWriter & /*writer*/) const override
	{
	}


	std::function<void()> Load(
	    Serialise::BinaryReader & /*reader*/, const entt::registry & /*loaded*/,
	    std::span<const entt::meta_any> /*queued*/) override
	{
		return [] {};
	}

  private:
	std::vector<std::string> &m_log;
};


// A game that records each hook it is given, and throws in the phase it is told to.
class Recorder : public Host::Game
{
  public:
	explicit Recorder(std::vector<std::string> &log, std::optional<Host::Phase> failIn = std::nullopt)
	    : m_log{log}, m_failIn{failIn}
	{
	}


	~Recorder() override
	{
		m_log.emplace_back("game");
	}


	Recorder(const Recorder &) = delete;


	Recorder &operator=(const Recorder &) = delete;


	void RegisterTypes() override
	{
		Hook(Host::Phase::Types, "types");
		RegisterTestTypes();
	}


	void LoadContent(
	    const Vfs::FileSystem &files, std::span<const Host::Package> packages,
	    Sim::DefinitionRegistry &definitions) override
	{
		Hook(Host::Phase::Content, "content");
		CHECK(files.Exists("defs/crops/wheat.yaml"));
		CHECK(packages.size() == files.Mounts().size());
		definitions.Load<Crop>("defs/crops", {1});
	}


	void RegisterWorld(Sim::SimWorld &world, const Sim::DefinitionRegistry &definitions) override
	{
		Hook(Host::Phase::World, "world");
		CHECK(definitions.Find<Crop>("wheat") != nullptr);
		world.RegisterComponent<Field>();
		world.AddService("probe", std::make_unique<Probe>(m_log));
	}


	void CreateInitialState(Sim::SimWorld &world) override
	{
		Hook(Host::Phase::InitialState, "initial state");
		world.Registry().emplace<Field>(world.Registry().create(), 3);
	}

  private:
	void Hook(Host::Phase phase, const char *name)
	{
		m_log.emplace_back(name);
		if (m_failIn == phase)
			throw std::runtime_error{"told to fail"};
	}

	std::vector<std::string> &m_log;
	std::optional<Host::Phase> m_failIn;
};


void PutContent(const Scratch &scratch)
{
	scratch.Put("content/mod.yaml", "version: 1\nname: Farm\nrelease: 1.2.0\nengine: 0.1.0\n");
	scratch.Put("content/defs/crops/wheat.yaml", "version: 1\ndisplayName: Wheat\nyield: 7\n");
}


Host::Options OptionsFor(const std::filesystem::path &content)
{
	return {{{"farm", content}}, 1.0 / 60.0, 42};
}


// The phase a start-up failed in, or none for one that did not fail.
std::optional<Host::Phase> FailedPhase(const std::function<void()> &start)
{
	try
	{
		start();
	}
	catch (const Host::StartupError &error)
	{
		return error.FailedPhase();
	}
	return std::nullopt;
}
} // namespace


TEST_CASE("Host: a session runs the game's hooks in phase order and is ready once they have run")
{
	const Scratch scratch;
	PutContent(scratch);
	std::vector<std::string> log;
	{
		Host::Session session{OptionsFor(scratch.Path("content")), std::make_unique<Recorder>(log)};
		CHECK(log == std::vector<std::string>{"types", "content", "world", "initial state"});
		CHECK(session.Running());
		REQUIRE(session.Packages().size() == 1);
		CHECK(session.Packages()[0].mount == "farm");
		REQUIRE(session.Packages()[0].description.has_value());
		CHECK(session.Packages()[0].description->release == "1.2.0");
		CHECK(session.Definitions().Find<Crop>("wheat")->yield == 7);
		CHECK(session.World().Registry().view<Field>().size() == 1);

		session.Shutdown();
		CHECK_FALSE(session.Running());

		// The world goes before the game, whose state its callbacks may hold.
		CHECK(
		    log ==
		    std::vector<std::string>{"types", "content", "world", "initial state", "world", "game"});
		session.Shutdown();
	}
	CHECK(log.size() == 6);
}


TEST_CASE(
    "Host: a hook that throws fails start-up in its phase, runs no later hook and releases it all")
{
	const Scratch scratch;
	PutContent(scratch);
	const std::vector<std::pair<Host::Phase, std::vector<std::string>>> cases{
	    {Host::Phase::Types, {"types", "game"}},
	    {Host::Phase::Content, {"types", "content", "game"}},
	    {Host::Phase::World, {"types", "content", "world", "game"}},
	    {Host::Phase::InitialState, {"types", "content", "world", "initial state", "world", "game"}},
	};
	for (const auto &[phase, expected] : cases)
	{
		CAPTURE(Host::NameOf(phase));
		std::vector<std::string> log;
		CHECK(
		    FailedPhase(
		        [&]
		        {
			        Host::Session session{
			            OptionsFor(scratch.Path("content")), std::make_unique<Recorder>(log, phase)};
		        }) == phase);
		CHECK(log == expected);
	}

	// A session after a failed one starts, and two run side by side.
	std::vector<std::string> first;
	std::vector<std::string> second;
	Host::Session one{OptionsFor(scratch.Path("content")), std::make_unique<Recorder>(first)};
	Host::Session two{OptionsFor(scratch.Path("content")), std::make_unique<Recorder>(second)};
	CHECK(one.Running());
	CHECK(two.Running());
}


TEST_CASE("Host: options, sources and package descriptions that cannot start fail before any hook")
{
	const Scratch scratch;
	PutContent(scratch);
	scratch.Put("future/mod.yaml", "version: 1\nengine: 99.0.0\n");
	scratch.Put("needy/mod.yaml", "version: 1\nrequires: [farm]\n");
	scratch.Put("broken/mod.yaml", "version: 1\nrelase: 1.0.0\n");
	scratch.Put("not-a.pack", "this is not a pack");
	const auto start = [&](Host::Options options)
	{
		std::vector<std::string> log;
		const std::optional<Host::Phase> phase = FailedPhase(
		    [&] { Host::Session session{std::move(options), std::make_unique<Recorder>(log)}; });
		return std::pair{phase, log};
	};

	using Phase = Host::Phase;
	const Host::Options good = OptionsFor(scratch.Path("content"));
	CHECK(start({{}, 1.0, 0}).first == Phase::Options);
	CHECK(start({{{"farm", scratch.Path("content"), true}}, 1.0, 0}).first == Phase::Options);
	CHECK(start({good.content, 0.0, 0}).first == Phase::Options);
	CHECK(start({{{"gone", scratch.Path("gone")}}, 1.0, 0}).first == Phase::Mount);
	CHECK(start({{{"bad", scratch.Path("not-a.pack")}}, 1.0, 0}).first == Phase::Mount);

	// A package for a newer engine, one whose requirement is not before it, and one whose description
	// does not read each fail in the content phase, naming the source, before the game's hook.
	for (const char *source : {"future", "needy", "broken"})
	{
		CAPTURE(source);
		const auto [phase, log] = start({{{source, scratch.Path(source)}}, 1.0, 0});
		CHECK(phase == Phase::Content);
		CHECK(log == std::vector<std::string>{"types", "game"});
	}
	CHECK(
	    start({{{"farm", scratch.Path("content")}, {"needy", scratch.Path("needy")}}, 1.0, 0}).first ==
	    std::nullopt);

	try
	{
		Host::Session session{
		    {{{"future", scratch.Path("future")}}, 1.0, 0}, std::make_unique<Host::Game>()};
		FAIL("started");
	}
	catch (const Host::StartupError &error)
	{
		CHECK(error.Source() == std::filesystem::absolute(scratch.Path("future")).lexically_normal());
		CHECK(std::string{error.what()}.find("requires engine 99.0.0") != std::string::npos);
	}
}


TEST_CASE("Host: a file system's packages are checked as a session's are, with no world")
{
	const Scratch scratch;
	PutContent(scratch);
	scratch.Put("needy/mod.yaml", "version: 1\nrequires: [farm]\n");
	scratch.Put("bare/defs/crops/wheat.yaml", "version: 1\n");

	Vfs::FileSystem files;
	files.AddMount({"farm", scratch.Path("content")});
	files.AddMount({"needy", scratch.Path("needy")});
	files.AddMount({"bare", scratch.Path("bare")});
	const std::vector<Host::Package> packages = Host::DescribePackages(files);
	REQUIRE(packages.size() == 3);
	CHECK(packages[0].mount == "farm");
	REQUIRE(packages[0].description.has_value());
	CHECK(packages[0].description->name == "Farm");
	CHECK(packages[1].mount == "needy");
	CHECK_FALSE(packages[2].description.has_value());

	// Its requirement is not before it.
	Vfs::FileSystem alone;
	alone.AddMount({"needy", scratch.Path("needy")});
	try
	{
		(void)Host::DescribePackages(alone);
		FAIL("described");
	}
	catch (const Host::StartupError &error)
	{
		CHECK(error.FailedPhase() == Host::Phase::Content);
		CHECK(error.Source() == std::filesystem::absolute(scratch.Path("needy")).lexically_normal());
		CHECK(std::string{error.what()}.find("requires the package 'farm'") != std::string::npos);
	}
}


TEST_CASE("Host: a package reads the same from its directory and its pack")
{
	const Scratch scratch;
	PutContent(scratch);
	Vfs::WritePack(scratch.Path("content"), scratch.Path("farm.pack"));

	const std::optional<Host::PackageDescription> fromDirectory =
	    Host::DescribePackage(scratch.Path("content"));
	const std::optional<Host::PackageDescription> fromPack =
	    Host::DescribePackage(scratch.Path("farm.pack"));
	REQUIRE(fromDirectory.has_value());
	REQUIRE(fromPack.has_value());
	CHECK(fromDirectory->name == "Farm");
	CHECK(fromPack->name == fromDirectory->name);
	CHECK(fromPack->release == fromDirectory->release);
	CHECK(fromPack->engine == fromDirectory->engine);

	std::vector<std::string> log;
	Host::Session session{OptionsFor(scratch.Path("farm.pack")), std::make_unique<Recorder>(log)};
	CHECK(session.Definitions().Find<Crop>("wheat")->displayName == "Wheat");

	scratch.Put("bare/defs/crops/wheat.yaml", "version: 1\n");
	CHECK_FALSE(Host::DescribePackage(scratch.Path("bare")).has_value());
}


TEST_CASE("Host: a server's session leaves out a package for the client alone, which holds no world")
{
	const Scratch scratch;
	PutContent(scratch);
	scratch.Put("paint/mod.yaml", "version: 1\nname: Paint\nside: client\n");
	scratch.Put("paint/textures/barn.png", "red");
	scratch.Put("both/mod.yaml", "version: 1\nside: both\n");
	scratch.Put("both/scripts/rain.wasm", "rain");
	scratch.Put("sneaky/mod.yaml", "version: 1\nside: client\n");
	scratch.Put("sneaky/defs/crops/wheat.yaml", "version: 1\ndisplayName: Gold\nyield: 99\n");
	scratch.Put("needs-paint/mod.yaml", "version: 1\nrequires: [paint]\n");
	scratch.Put("elsewhere/mod.yaml", "version: 1\nside: elsewhere\n");

	CHECK_FALSE(Host::HoldsWorld(scratch.Path("paint")));
	CHECK(Host::HoldsWorld(scratch.Path("both")));
	CHECK(Host::HoldsWorld(scratch.Path("sneaky")));
	CHECK(Host::DescribePackage(scratch.Path("paint"))->side == Host::Side::Client);
	CHECK(Host::DescribePackage(scratch.Path("both"))->side == Host::Side::Both);
	CHECK(Host::DescribePackage(scratch.Path("content"))->side == Host::Side::Both);

	const auto optionsWith = [&](std::vector<std::string> sources, bool server)
	{
		Host::Options options = OptionsFor(scratch.Path("content"));
		for (const std::string &source : sources)
			options.content.push_back({source, scratch.Path(source)});
		options.server = server;
		return options;
	};
	std::vector<std::string> log;

	// A client mounts the cosmetic package; a server leaves it out, as if it were not given.
	{
		Host::Session client{optionsWith({"paint", "both"}, false), std::make_unique<Recorder>(log)};
		CHECK(client.Packages().size() == 3);
		CHECK(client.Files().Exists("textures/barn.png"));
	}
	{
		Host::Session server{optionsWith({"paint", "both"}, true), std::make_unique<Recorder>(log)};
		REQUIRE(server.Packages().size() == 2);
		CHECK(server.Packages()[1].mount == "both");
		CHECK_FALSE(server.Files().Exists("textures/barn.png"));
		CHECK(server.Files().Exists("scripts/rain.wasm"));
	}

	// One for the client alone that would change the world is refused on either side, as is a world
	// package a server cannot run because what it requires is the client's.
	for (const bool server : {false, true})
	{
		CAPTURE(server);
		CHECK(
		    FailedPhase(
		        [&]
		        {
			        Host::Session session{
			            optionsWith({"sneaky"}, server), std::make_unique<Recorder>(log)};
		        }) == (server ? std::nullopt : std::optional{Host::Phase::Content}));
		CHECK(
		    FailedPhase(
		        [&]
		        {
			        Host::Session session{
			            optionsWith({"paint", "needs-paint"}, server), std::make_unique<Recorder>(log)};
		        }) == (server ? std::optional{Host::Phase::Content} : std::nullopt));
		CHECK(
		    FailedPhase(
		        [&]
		        {
			        Host::Session session{
			            optionsWith({"elsewhere"}, server), std::make_unique<Recorder>(log)};
		        }) == Host::Phase::Content);
	}
	try
	{
		Host::Session session{optionsWith({"sneaky"}, false), std::make_unique<Host::Game>()};
		FAIL("started");
	}
	catch (const Host::StartupError &error)
	{
		CHECK(error.Source() == std::filesystem::absolute(scratch.Path("sneaky")).lexically_normal());
		CHECK(std::string{error.what()}.find("for the client alone") != std::string::npos);
	}
}


TEST_CASE("Host: a server pack holds a package's description and world, and none of its presentation")
{
	const Scratch scratch;
	PutContent(scratch);
	scratch.Put("content/scripts/rain.wasm", "rain");
	scratch.Put("content/textures/barn.png", "red");
	scratch.Put("content/strings/en/farm.yaml", "version: 1\n");
	const Vfs::PackSummary summary =
	    Host::WriteServerPack(scratch.Path("content"), scratch.Path("server.pack"));
	CHECK(summary.files == 3);
	Vfs::FileSystem packed;
	packed.AddMount({"farm", scratch.Path("server.pack")});
	CHECK(
	    packed.Enumerate("") ==
	    std::vector<std::string>{"defs/crops/wheat.yaml", "mod.yaml", "scripts/rain.wasm"});
	CHECK(Host::DescribePackage(scratch.Path("server.pack"))->name == "Farm");

	// A server session runs on it as on the directory.
	std::vector<std::string> log;
	Host::Options options = OptionsFor(scratch.Path("server.pack"));
	options.server = true;
	Host::Session session{std::move(options), std::make_unique<Recorder>(log)};
	CHECK(session.Definitions().Find<Crop>("wheat")->displayName == "Wheat");

	// Neither a package for the client alone nor one with no world has a server pack.
	scratch.Put("paint/mod.yaml", "version: 1\nside: client\n");
	scratch.Put("paint/textures/barn.png", "red");
	scratch.Put("bare/textures/barn.png", "red");
	for (const char *source : {"paint", "bare"})
	{
		CAPTURE(source);
		CHECK_THROWS_AS(
		    Host::WriteServerPack(scratch.Path(source), scratch.Path("no.pack")), std::runtime_error);
		CHECK_FALSE(std::filesystem::exists(scratch.Path("no.pack")));
	}

	// One with no description packs its world alone.
	scratch.Put("plain/defs/crops/wheat.yaml", "version: 1\ndisplayName: Wheat\nyield: 7\n");
	CHECK(Host::WriteServerPack(scratch.Path("plain"), scratch.Path("plain.pack")).files == 1);
}


TEST_CASE("Host: a client's world content is checked against its server's, naming what differs")
{
	const Scratch scratch;
	PutContent(scratch);
	scratch.Put("engine/textures/white.png", "white");
	scratch.Put("rain/mod.yaml", "version: 1\nrelease: 2.0.0\n");
	scratch.Put("rain/scripts/rain.wasm", "rain");
	scratch.Put("paint/mod.yaml", "version: 1\nside: client\n");
	scratch.Put("paint/textures/barn.png", "red");
	scratch.Put("old-rain/mod.yaml", "version: 1\nrelease: 1.0.0\n");
	scratch.Put("old-rain/scripts/rain.wasm", "rain");
	scratch.Put("odd-rain/mod.yaml", "version: 1\nrelease: 2.0.0\n");
	scratch.Put("odd-rain/scripts/rain.wasm", "drizzle");
	scratch.Put("hail/defs/crops/hail.yaml", "version: 1\n");
	const auto contentOf = [&](std::vector<std::pair<std::string, std::string>> sources)
	{
		Vfs::FileSystem files;
		for (const auto &[mount, source] : sources)
			files.AddMount({mount, scratch.Path(source)});
		return Host::DescribeWorldContent(files);
	};

	// Packages of presentation alone, the client's own and the engine's assets, are not the world's.
	const Host::WorldContent served = contentOf({{"farm", "content"}, {"rain", "rain"}});
	const Host::WorldContent held =
	    contentOf({{"engine", "engine"}, {"farm", "content"}, {"rain", "rain"}, {"paint", "paint"}});
	REQUIRE(served.packages.size() == 2);
	CHECK(served.packages[0].mount == "farm");
	CHECK(served.packages[0].release == "1.2.0");
	CHECK(served.packages[1].release == "2.0.0");
	CHECK(held == served);
	CHECK(Host::ContentDifference(held, served) == std::nullopt);
	CHECK(Host::DecodeWorldContent(Host::EncodeWorldContent(served)) == served);
	CHECK_THROWS(Host::DecodeWorldContent(std::vector<std::byte>(4, std::byte{0x7f})));

	const auto differs = [&](const Host::WorldContent &client, std::string_view says)
	{
		const std::optional<std::string> difference = Host::ContentDifference(client, served);
		REQUIRE(difference.has_value());
		CAPTURE(*difference);
		CHECK(difference->find(says) != std::string::npos);
	};
	differs(contentOf({{"farm", "content"}}), "The server runs on 'rain' 2.0.0, which this client");
	differs(contentOf({{"farm", "content"}, {"rain", "old-rain"}}), "holds 'rain' 1.0.0, and the server");
	differs(contentOf({{"farm", "content"}, {"rain", "odd-rain"}}), "'rain' 2.0.0 is not the server's");
	differs(
	    contentOf({{"farm", "content"}, {"rain", "rain"}, {"hail", "hail"}}),
	    "holds 'hail', which changes the world");
	differs(contentOf({{"rain", "rain"}, {"farm", "content"}}), "in another order");
}


TEST_CASE("Host: an engine version is compared by its numbers")
{
	CHECK(Host::EngineAtLeast("0.1.0", "0.1"));
	CHECK(Host::EngineAtLeast("1.0.0", "0.10.3"));
	CHECK_FALSE(Host::EngineAtLeast("0.9.0", "0.10.0"));
	CHECK_THROWS_AS(Host::EngineAtLeast("0.1.0", "one"), std::invalid_argument);
	CHECK_THROWS_AS(Host::EngineAtLeast("0.1.0", "0.1.0.1"), std::invalid_argument);
	CHECK_THROWS_AS(Host::EngineAtLeast("0.1.0", "0..1"), std::invalid_argument);
}
