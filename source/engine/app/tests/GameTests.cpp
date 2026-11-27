#include "Application.h"
#include "RunLog.h"

#include <doctest/doctest.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/core/Log.h>
#include <jettison/ecs/Scheduler.h>
#include <jettison/host/Session.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/render/IRenderer.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/SimWorld.h>

using namespace Jettison;

namespace Jettison::App
{
// The shell's stages that need no window: its user directory, resources and scene, its saves, and
// its frame's order.
struct ApplicationProbe
{
	static void Start(Application &shell)
	{
		shell.InitUser();
		shell.InitResources();
		shell.InitScene();
	}


	static void Reload(Application &shell)
	{
		shell.ReloadScene();
	}


	static void Shutdown(Application &shell)
	{
		shell.Shutdown();
	}


	static bool Save(Application &shell, std::string_view name)
	{
		return shell.SaveTo(name);
	}


	static bool Load(Application &shell, std::string_view name)
	{
		return shell.LoadFrom(name);
	}


	static std::vector<std::string> Order(const Application &shell, Ecs::TickPhase phase)
	{
		return shell.m_frame.Order(phase);
	}


	static Sim::SimWorld &World(Application &shell)
	{
		return shell.m_pSession->World();
	}


	static void SetRenderer(Application &shell, std::unique_ptr<Render::IRenderer> renderer)
	{
		shell.m_pRenderer = std::move(renderer);
	}


	static void ShowSequence(Application &shell)
	{
		shell.ShowSequence(shell.m_pScene->Registry());
	}


	// The game UI made over a renderer of the test's, as InitUi makes it once a window is open.
	static void OpenUi(Application &shell, std::unique_ptr<Render::IRenderer> renderer)
	{
		shell.m_pRenderer = std::move(renderer);
		shell.InitUi();
	}


	static std::string Language(Application &shell)
	{
		return std::string{shell.m_pUi->Strings().Language()};
	}


	static Scene::Scene &SceneOf(Application &shell)
	{
		return *shell.m_pScene;
	}
};
} // namespace Jettison::App

namespace
{
// A game of no genre on the shell's seams alone: a frame system after the shell's sync, a save section
// and detail of its own, and the scenes it is shown.
class SeamGame final : public App::Game
{
  public:
	void RegisterFrame(Ecs::Scheduler &frame, App::Shell & /*shell*/) override
	{
		frame.Register(
		    {"test.frame", Ecs::TickPhase::Sync, {}, {after}, [](entt::registry &, double) {}});
	}


	void SceneLoaded(App::Shell & /*shell*/) override
	{
		++loaded;
	}


	void SceneUnloading(App::Shell & /*shell*/) override
	{
		++unloading;
	}


	void WriteSave(Sim::SaveGame &save, App::Shell & /*shell*/) override
	{
		save.info.details.push_back({"test.owner", owner});
		save.AddSection("Test.Game", section);
	}


	void ReadSave(const Sim::SaveGame &save, App::Shell & /*shell*/) override
	{
		if (save.info.Detail("test.owner") != owner)
			throw std::runtime_error("not this game's save");
		const Sim::SaveSection *found = save.FindSection("Test.Game");
		read = found ? found->data : std::vector<std::byte>{};
	}

	std::string after{"sim.sync"};
	std::string owner{"seams"};
	std::vector<std::byte> section{std::byte{7}, std::byte{0}, std::byte{42}};
	std::vector<std::byte> read;
	int loaded{0};
	int unloading{0};
};


// A world of the game's own, as its server would start it: the shell's components with physics, a body
// in it, and the body shown in the snapshots already, which the shell then does not show again; or a
// content phase that fails.
class SeamWorld final : public Host::Game
{
  public:
	void LoadContent(
	    const Vfs::FileSystem & /*files*/, std::span<const Host::Package> /*packages*/,
	    Sim::DefinitionRegistry & /*definitions*/) override
	{
		if (failContent)
			throw std::runtime_error{"no market"};
	}


	void RegisterWorld(Sim::SimWorld &world, const Sim::DefinitionRegistry & /*definitions*/) override
	{
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InFrame>();
		Physics::PhysicsWorld::Attach(world);
		world.RegisterComponent<Sim::ControlledBy>();
		world.ShowInSnapshots<Physics::Body>();
	}


	void CreateInitialState(Sim::SimWorld &world) override
	{
		entt::registry &registry = world.Registry();
		const entt::entity crate = registry.create();
		registry.emplace<Sim::Transform>(crate);
		registry.emplace<Physics::Body>(crate, Physics::Body{Physics::BodyKind::Static, "crate"});
	}

	bool failContent{false};
};


// A renderer that draws nothing and keeps the fade it was last given.
class FadeRenderer final : public Render::IRenderer
{
  public:
	void Init() override
	{
	}


	void Destroy() override
	{
	}


	GLFWwindow *GetGLFWWindow() const override
	{
		return nullptr;
	}


	void BeginUi() override
	{
	}


	void EndUi() override
	{
	}


	bool UiColoursLinearised() const override
	{
		return true;
	}


	bool Render(
	    const Scene::Scene & /*scene*/, const Render::DebugDraw & /*debug*/,
	    const Render::Overlay & /*overlay*/, double /*seconds*/) override
	{
		return false;
	}


	void SetExposure(float /*exposure*/) override
	{
	}


	void SetFade(const glm::vec3 & /*color*/, float amount) override
	{
		fade = amount;
	}


	void SetLetterbox(float /*share*/) override
	{
	}


	void SetDebugView(Render::DebugView /*view*/) override
	{
	}


	Render::DebugView GetDebugView() const override
	{
		return Render::DebugView::Shaded;
	}


	void SetDepthPrepass(bool /*enabled*/) override
	{
	}


	bool GetDepthPrepass() const override
	{
		return false;
	}


	void SetAmbientOcclusion(bool /*enabled*/) override
	{
	}


	bool GetAmbientOcclusion() const override
	{
		return false;
	}


	void SetBloom(float /*strength*/) override
	{
	}


	float GetBloom() const override
	{
		return 0.0f;
	}


	const Render::FrameStats &GetFrameStats() const override
	{
		return stats;
	}


	const std::vector<Render::PassTiming> &GetPassTimings() const override
	{
		return timings;
	}


	std::uint64_t GetMeasuredFrames() const override
	{
		return 0;
	}


	void ResetPassTimings() override
	{
	}


	const Render::ClusterCheckTotals &GetClusterChecks() const override
	{
		return checks;
	}


	void RequestScreenshot(const std::filesystem::path & /*path*/) override
	{
	}


	bool ScreenshotRequested() const override
	{
		return false;
	}


	const std::string &DeviceName() const override
	{
		return device;
	}


	std::uint32_t FrameWidth() const override
	{
		return 640;
	}


	std::uint32_t FrameHeight() const override
	{
		return 480;
	}

	float fade{-1.0f};
	Render::FrameStats stats;
	std::vector<Render::PassTiming> timings;
	Render::ClusterCheckTotals checks;
	std::string device;
};

struct VehicleEvent
{
	entt::entity vehicle{entt::null};
	int count{0};
};


App::AppArguments WindowlessArguments(const std::filesystem::path &user)
{
	App::AppArguments arguments;
	arguments.assets = JETTISON_ASSET_SOURCE_DIR;
	arguments.userDirectory = user;
	arguments.noSound = true;
	return arguments;
}
} // namespace


TEST_CASE("Game: the shell with no game composed into it registers no market, trader, fleet or ship")
{
	// The shell as Flotsam makes it, started: its own world, its commands and console's, and no game.
	// Nothing of the space-trading sample is in this suite's link, so a type of the sample's that
	// resolved would be the shell's own registering it.
	const auto user =
	    std::filesystem::temp_directory_path() / ("jettison-neutral-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(user, error);
	App::Application shell{WindowlessArguments(user), "Neutral"};
	App::ApplicationProbe::Start(shell);
	const Sim::SimWorld &world = App::ApplicationProbe::World(shell);
	constexpr std::array<std::string_view, 9> kSample{
	    "Shell.Market", "Shell.MarketDefinition", "Shell.Good",
	    "Shell.Trade",  "Shell.Trader",           "Shell.Hold",
	    "Shell.Purse",  "Shell.BuyShip",          "Shell.ShipForSale"};
	for (const std::string_view name : kSample)
	{
		CAPTURE(name);
		CHECK_FALSE(Reflect::Resolve(name));
	}

	// The world is what the shell draws and picks with, physics alone: no component of the sample's,
	// and no service but physics.
	const std::vector<std::string> components = world.ComponentNames();
	CHECK(std::ranges::find(components, "Sim.Transform") != components.end());
	for (const std::string_view name : kSample)
		CHECK(std::ranges::find(components, name) == components.end());
	CHECK(world.ServiceNames() == std::vector<std::string>{"physics"});
	App::ApplicationProbe::Shutdown(shell);
	std::filesystem::remove_all(user, error);
}


TEST_CASE("Game: the engine's assets are the engine's, and none of the test content is in them")
{
	// What assets/CMakeLists.txt stages and packs: CMakeLists.txt itself is not staged.
	constexpr std::array<std::string_view, 4> kEngine{"fonts", "shaders", "strings", "ui"};
	const std::filesystem::path engine{JETTISON_ASSET_SOURCE_DIR};
	const std::filesystem::path content{JETTISON_TEST_CONTENT_DIR};
	std::size_t files = 0;
	for (const auto &entry : std::filesystem::recursive_directory_iterator{engine})
	{
		if (!entry.is_regular_file() || entry.path().filename() == "CMakeLists.txt")
			continue;
		const std::filesystem::path path = entry.path().lexically_relative(engine);
		CAPTURE(path.string());
		CHECK(std::ranges::find(kEngine, path.begin()->string()) != kEngine.end());

		// A file of the engine's the content ships again would be the content's to change, not the
		// engine's: an engine path is the content's at most to read.
		CHECK_FALSE(std::filesystem::exists(content / path));
		++files;
	}
	CHECK(files > 0);
}


TEST_CASE("Game: what the shell reports after its teardown is in the run's log file")
{
	// Run reports the benchmark, the GPU's timings and the cluster check once the frame loop has
	// torn the engine down: the log is still open then, and closes with the shell.
	const auto user =
	    std::filesystem::temp_directory_path() / ("jettison-log-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(user, error);
	{
		App::Application shell{WindowlessArguments(user), "Reports"};
		App::ApplicationProbe::Start(shell);
		App::ApplicationProbe::Shutdown(shell);
		constexpr Core::Category kTest{"test"};
		Core::Log(Core::Severity::Info, kTest, "Reported after the teardown");
	}
	std::ifstream log{user / "state" / App::kRunLogFile};
	const std::string text{std::istreambuf_iterator<char>{log}, {}};
	CHECK(text.find("Reported after the teardown") != std::string::npos);
	std::filesystem::remove_all(user, error);
}


TEST_CASE("Game: a game's frame system, save section and scene hooks run on the shell's seams")
{
	const auto user =
	    std::filesystem::temp_directory_path() / ("jettison-game-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(user, error);
	SeamGame game;
	{
		App::Application shell{WindowlessArguments(user), "Seams", &game};

		// In the phase it asked for, after the system it named.
		const std::vector<std::string> sync = App::ApplicationProbe::Order(shell, Ecs::TickPhase::Sync);
		const auto named = std::ranges::find(sync, "sim.sync");
		const auto own = std::ranges::find(sync, "test.frame");
		REQUIRE(named != sync.end());
		REQUIRE(own != sync.end());
		CHECK(named < own);

		App::ApplicationProbe::Start(shell);
		CHECK(game.loaded == 1);
		CHECK(game.unloading == 0);

		// The user's own directories are made private from the start (XDG), the crash reports' and
		// the log's among them, whatever the umask.
		for (const char *directory : {"data", "config", "state"})
			CHECK(
			    (std::filesystem::status(user / directory).permissions() &
			     std::filesystem::perms::all) == std::filesystem::perms::owner_all);
		App::ApplicationProbe::Reload(shell);
		CHECK(game.loaded == 2);
		CHECK(game.unloading == 1);

		// The section goes out and comes back as it was written.
		REQUIRE(App::ApplicationProbe::Save(shell, "seams"));
		Sim::SimWorld &world = App::ApplicationProbe::World(shell);
		const std::uint64_t saved = world.StateHash();
		world.Step();
		REQUIRE(world.StateHash() != saved);
		REQUIRE(App::ApplicationProbe::Load(shell, "seams"));
		CHECK(game.read == game.section);
		CHECK(world.StateHash() == saved);

		// A save the game refuses leaves the world as it was.
		world.Step();
		const std::uint64_t stepped = world.StateHash();
		game.owner = "another";
		game.read.clear();
		CHECK_FALSE(App::ApplicationProbe::Load(shell, "seams"));
		CHECK(game.read.empty());
		CHECK(world.StateHash() == stepped);
	}
	std::filesystem::remove_all(user, error);
}


TEST_CASE("Game: the shell starts a game's world through a session, as its server would")
{
	const auto user =
	    std::filesystem::temp_directory_path() / ("jettison-world-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(user, error);
	SeamGame game;
	{
		App::Application shell{
		    WindowlessArguments(user), "Worlds", &game, std::make_unique<SeamWorld>()};
		App::ApplicationProbe::Start(shell);
		Sim::SimWorld &world = App::ApplicationProbe::World(shell);
		CHECK(world.Registry().view<const Physics::Body>().size() == 1);
		CHECK(world.Shows(entt::type_hash<Physics::Body>::value()));
		CHECK(world.Shows(entt::type_hash<Physics::LocalFrame>::value()));
		CHECK(game.loaded == 1);

		// The world its server would start, alone over the same content, is the same to the byte, and
		// stays so as both step.
		Host::Session server{
		    {{{"engine", JETTISON_ASSET_SOURCE_DIR}}, 1.0 / 60.0, 0}, std::make_unique<SeamWorld>()};
		CHECK(world.Save() == server.World().Save());
		constexpr int kSteps = 5;
		for (int step = 0; step < kSteps; ++step)
		{
			world.Step();
			server.World().Step();
		}
		CHECK(world.Save() == server.World().Save());
	}
	std::filesystem::remove_all(user, error);
}


namespace
{
// The locale's variables set for a test, and put back as they were when it ends.
class Locale
{
  public:
	Locale(const char *all, const char *messages, const char *lang)
	{
		for (std::size_t at = 0; at < kVariables.size(); ++at)
		{
			const char *was = std::getenv(kVariables[at]);
			m_was[at] = was != nullptr ? std::optional<std::string>{was} : std::nullopt;
		}
		Set({all, messages, lang});
	}


	~Locale()
	{
		for (std::size_t at = 0; at < kVariables.size(); ++at)
			if (m_was[at])
				::setenv(kVariables[at], m_was[at]->c_str(), 1);
			else
				::unsetenv(kVariables[at]);
	}


	Locale(const Locale &) = delete;


	Locale &operator=(const Locale &) = delete;

  private:
	static constexpr std::array<const char *, 3> kVariables{"LC_ALL", "LC_MESSAGES", "LANG"};


	static void Set(const std::array<const char *, 3> &values)
	{
		for (std::size_t at = 0; at < kVariables.size(); ++at)
			if (values[at] != nullptr)
				::setenv(kVariables[at], values[at], 1);
			else
				::unsetenv(kVariables[at]);
	}

	std::array<std::optional<std::string>, 3> m_was;
};


// The language a windowless shell's screens are in once its UI is made, in the user directory given.
std::string LanguageOf(App::AppArguments arguments)
{
	App::Application shell{std::move(arguments), "Language"};
	App::ApplicationProbe::Start(shell);
	App::ApplicationProbe::OpenUi(shell, std::make_unique<FadeRenderer>());
	return App::ApplicationProbe::Language(shell);
}
} // namespace


TEST_CASE("Game: the first start takes the locale's language and keeps it; a test's run never does")
{
	const auto root =
	    std::filesystem::temp_directory_path() / ("jettison-locale-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(root, error);
	const auto settings = [](const std::filesystem::path &user)
	{
		std::ifstream file{user / "config" / "settings.yaml"};
		return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
	};

	// LC_ALL over LANG, the language's table for a region with none, and the choice kept.
	{
		const Locale locale{"de_AT.UTF-8", nullptr, "en_GB.UTF-8"};
		CHECK(LanguageOf(WindowlessArguments(root / "first")) == "de");
	}
	CHECK(settings(root / "first").find("language: de") != std::string::npos);

	// Kept: a later start in another locale reads the setting.
	{
		const Locale locale{nullptr, "en_GB.UTF-8", nullptr};
		CHECK(LanguageOf(WindowlessArguments(root / "first")) == "de");
	}

	// A run of a fixed frame step, as a reference frame's, never reads the locale.
	{
		const Locale locale{"de_DE.UTF-8", nullptr, nullptr};
		App::AppArguments arguments = WindowlessArguments(root / "fixed");
		arguments.frameStep = 1.0 / 60.0;
		CHECK(LanguageOf(arguments) == "en");
	}
	CHECK(settings(root / "fixed").find("language") == std::string::npos);

	// A settings file that cannot be written costs the choice being kept, not the start.
	{
		const Locale locale{nullptr, nullptr, "de_DE.UTF-8"};
		std::filesystem::create_directories(root / "locked" / "config");
		std::filesystem::permissions(
		    root / "locked" / "config",
		    std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
		CHECK(LanguageOf(WindowlessArguments(root / "locked")) == "de");
		std::filesystem::permissions(root / "locked" / "config", std::filesystem::perms::owner_all);
	}
	CHECK(settings(root / "locked").empty());

	// Settings the language cannot be put in (ui a value of its own) cost it being kept, not the start.
	{
		const Locale locale{nullptr, nullptr, "de_DE.UTF-8"};
		std::filesystem::create_directories(root / "taken" / "config");
		std::ofstream{root / "taken" / "config" / "settings.yaml"} << "ui: old-setting\n";
		CHECK(LanguageOf(WindowlessArguments(root / "taken")) == "de");
	}
	CHECK(settings(root / "taken") == "ui: old-setting\n");
	std::filesystem::remove_all(root, error);
}


TEST_CASE("Game: a game's world that fails to start names the phase it failed in")
{
	const auto user =
	    std::filesystem::temp_directory_path() / ("jettison-failed-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(user, error);
	{
		auto world = std::make_unique<SeamWorld>();
		world->failContent = true;
		App::Application shell{WindowlessArguments(user), "Worlds", nullptr, std::move(world)};
		try
		{
			App::ApplicationProbe::Start(shell);
			FAIL("the start did not fail");
		}
		catch (const Host::StartupError &failure)
		{
			CHECK(failure.FailedPhase() == Host::Phase::Content);
			CHECK(std::string_view{failure.what()}.find("no market") != std::string_view::npos);
		}
	}
	std::filesystem::remove_all(user, error);
}


TEST_CASE("Game: a game's frame system naming no system of the shell's fails the shell's start")
{
	SeamGame game;
	game.after = "no.such.system";
	CHECK_THROWS_AS(App::Application(App::AppArguments{}, "Seams", &game), std::logic_error);
}


TEST_CASE("Game: a sequence's fade is cleared once it is over, and left alone otherwise")
{
	const auto user =
	    std::filesystem::temp_directory_path() / ("jettison-fade-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(user, error);
	{
		App::Application shell{WindowlessArguments(user), "Fades"};
		App::ApplicationProbe::Start(shell);
		auto owned = std::make_unique<FadeRenderer>();
		FadeRenderer &renderer = *owned;
		App::ApplicationProbe::SetRenderer(shell, std::move(owned));
		entt::registry &scene = App::ApplicationProbe::SceneOf(shell).Registry();

		// No sequence has faded: a game's fade is the game's.
		App::ApplicationProbe::ShowSequence(shell);
		CHECK(renderer.fade == -1.0f);

		const entt::entity camera = scene.create();
		scene.emplace<Scene::SequencePlayerComponent>(camera).fade = 0.75f;
		App::ApplicationProbe::ShowSequence(shell);
		CHECK(renderer.fade == 0.75f);
		scene.get<Scene::SequencePlayerComponent>(camera).fade = 0.0f;
		App::ApplicationProbe::ShowSequence(shell);
		CHECK(renderer.fade == 0.0f);

		// Stopped while faded, as a cancelled sequence is.
		scene.get<Scene::SequencePlayerComponent>(camera).fade = 0.5f;
		App::ApplicationProbe::ShowSequence(shell);
		scene.remove<Scene::SequencePlayerComponent>(camera);
		App::ApplicationProbe::ShowSequence(shell);
		CHECK(renderer.fade == 0.0f);
		renderer.fade = -1.0f;
		App::ApplicationProbe::ShowSequence(shell);
		CHECK(renderer.fade == -1.0f);
	}
	std::filesystem::remove_all(user, error);
}


TEST_CASE("Game: an event's entity is read by its field's reflected name")
{
	Reflect::RegisterType<VehicleEvent>("Test.VehicleEvent")
	    .Field<&VehicleEvent::vehicle>("vehicle", Reflect::Attribute::Saved)
	    .Field<&VehicleEvent::count>("count", Reflect::Attribute::Saved);
	const auto entity = static_cast<entt::entity>(7);
	const Sim::SimEvent event{0, entt::meta_any{VehicleEvent{entity, 3}}};
	CHECK(App::EventEntity(event, "vehicle") == entity);
	CHECK(App::EventEntity(event, "ship") == entt::null);
	CHECK(App::EventEntity(event, "count") == entt::null);
	CHECK(App::EventEntity({0, entt::meta_any{}}, "vehicle") == entt::null);
}
