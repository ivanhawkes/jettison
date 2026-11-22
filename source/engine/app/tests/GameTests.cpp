#include "Application.h"

#include <doctest/doctest.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/ecs/Scheduler.h>
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
		return shell.m_sim;
	}


	static void SetRenderer(Application &shell, std::unique_ptr<Render::IRenderer> renderer)
	{
		shell.m_pRenderer = std::move(renderer);
	}


	static void ShowSequence(Application &shell)
	{
		shell.ShowSequence(shell.m_pScene->Registry());
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
	// The shell as Flotsam makes it: its world's systems attached, its commands and console's, and no
	// game. Nothing of the space-trading sample is in this suite's link, so a type of the sample's
	// that resolved would be the shell's own registering it.
	const App::Application shell{App::AppArguments{}, "Neutral"};
	constexpr std::array<std::string_view, 9> kSample{
	    "Shell.Market", "Shell.MarketDefinition", "Shell.Good",
	    "Shell.Trade",  "Shell.Trader",           "Shell.Hold",
	    "Shell.Purse",  "Shell.BuyShip",          "Shell.ShipForSale"};
	for (const std::string_view name : kSample)
	{
		CAPTURE(name);
		CHECK_FALSE(Reflect::Resolve(name));
	}
	// The shell's own world is registered all the same.
	CHECK(Reflect::Resolve("Sim.Transform"));
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
