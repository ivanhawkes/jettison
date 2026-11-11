#include <jettison/scene/JumpTransition.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <jettison/celestial/Galaxy.h>
#include <jettison/jobs/IoWorker.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>
#include <jettison/scene/SystemScene.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/sim/Stepper.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison;
using Scene::JumpStage;

namespace
{
std::string ReadAsset(const std::string &path)
{
	std::ifstream file{JETTISON_ASSET_SOURCE_DIR "/" + path};
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

entt::entity Named(const entt::registry &registry, std::string_view id)
{
	for (const auto [entity, name] : registry.view<const Scene::SceneEntityId>().each())
		if (name.value == id)
			return entity;
	return entt::null;
}

// The shipped assets and galaxy, behind a resource manager that reads them on a worker of its own.
struct Assets
{
	Vfs::FileSystem files;
	Jobs::IoWorker worker;
	std::optional<Resource::ResourceManager> resources;
	Celestial::Galaxy galaxy = Celestial::ReadGalaxy(ReadAsset("systems/galaxy.yaml"));

	Assets()
	{
		files.AddMount({"base", JETTISON_ASSET_SOURCE_DIR});
		resources.emplace(
		    files, worker, Resource::ResourceOptions{.hotReload = false, .pollInterval = {}});
		Scene::RegisterJumpLoaders(*resources);
	}

	// A frame's resource update, with the worker's reads done first, so what a frame sees does not
	// depend on the worker's pace.
	void Settle()
	{
		worker.WaitIdle();
		resources->Update();
	}

	std::size_t Index(std::string_view name) const
	{
		const int index = galaxy.Find(name);
		REQUIRE(index >= 0);
		return static_cast<std::size_t>(index);
	}
};

// The shell's half of a jump, as the app shell does it: the old system scene goes with the scene's
// old contents, and the destination is built in their place.
struct Shown
{
	Scene::Scene scene;
	std::optional<Scene::SystemScene> systemScene;
	Scene::ProxyFactory factory;
	int shows{0};
	float fadeAtShow{-1.0f};
	std::string arrival;

	void Show(Scene::JumpArrival &destination, float fade)
	{
		++shows;
		fadeAtShow = fade;
		arrival = destination.arrival;
		systemScene.reset();
		if (destination.sceneFile)
			scene.Load(*destination.sceneFile);
		else
			scene.LoadSystem(destination.starSystemPath);
		systemScene.emplace(scene, std::move(destination.starSystem));
		systemScene->SetProxyFactory(factory);
	}
};

constexpr double kFrame = 0.05;

// Frames until the jump is over, at most a limit, recording each stage the frames were in.
std::vector<JumpStage> RunJump(Scene::JumpTransition &jump, Assets &assets, int limit = 400)
{
	std::vector<JumpStage> stages;
	for (int frame = 0; frame < limit && jump.Active(); ++frame)
	{
		jump.Advance(kFrame);
		if (stages.empty() || stages.back() != jump.Stage())
			stages.push_back(jump.Stage());
		assets.Settle();
	}
	return stages;
}

// A ship's velocity, in the system frame of the system it is in.
struct Cruise
{
	glm::dvec3 velocity{0.0};
};

// A ship on a jump lane, in no system, for the steps left before it arrives at the far end.
struct Transiting
{
	std::uint64_t destination{0};
	std::uint64_t stepsLeft{0};
	glm::dvec3 arrival{0.0};
};

void RegisterTestTypes()
{
	static const bool registered = []
	{
		using Reflect::Attribute;
		Sim::RegisterTypes();
		Reflect::RegisterType<Cruise>("JumpTests.Cruise")
		    .Field<&Cruise::velocity>("velocity", Attribute::Saved);
		Reflect::RegisterType<Transiting>("JumpTests.Transiting")
		    .Field<&Transiting::destination>("destination", Attribute::Saved)
		    .Field<&Transiting::stepsLeft>("stepsLeft", Attribute::Saved)
		    .Field<&Transiting::arrival>("arrival", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace

TEST_CASE("Jump: the effect covers the frame before the destination is shown, and lifts after it")
{
	Assets assets;
	Shown shown;
	Scene::JumpTransition *pJump = nullptr;
	Scene::JumpTransition jump{
	    *assets.resources, [&](Scene::JumpArrival &arrival) { shown.Show(arrival, pJump->Fade()); }};
	pJump = &jump;
	CHECK(jump.Stage() == JumpStage::Idle);
	CHECK(jump.Fade() == 0.0f);
	jump.Advance(kFrame);
	CHECK(jump.Stage() == JumpStage::Idle);

	const std::size_t vesper = assets.Index("Vesper");
	REQUIRE(jump.Begin(assets.galaxy, vesper, "Vesper Gate"));
	CHECK(jump.Active());
	// One jump at a time.
	CHECK_FALSE(jump.Begin(assets.galaxy, assets.Index("Marrow")));

	jump.Advance(kFrame);
	CHECK(jump.Fade() > 0.0f);
	CHECK(jump.Fade() < 1.0f);
	const std::vector<JumpStage> stages = RunJump(jump, assets);
	CHECK(
	    stages ==
	    std::vector{JumpStage::FadingOut, JumpStage::Streaming, JumpStage::FadingIn, JumpStage::Idle});
	CHECK(jump.Error().empty());
	CHECK(jump.Fade() == 0.0f);
	CHECK(shown.shows == 1);
	CHECK(shown.fadeAtShow == 1.0f);
	CHECK(shown.arrival == "Vesper Gate");
	// Vesper has no scene file: it is its system scene alone.
	CHECK(shown.scene.StarSystem() == "systems/vesper.yaml");
	CHECK(Named(shown.scene.Registry(), "corva-refinery") != entt::null);
	CHECK(Named(shown.scene.Registry(), "vesper-deep-gate") != entt::null);
	// A fade out, a frame shown and a fade in, at least.
	CHECK(jump.Seconds() >= 0.6 + 0.8);
	CHECK(assets.resources->Loading() == 0);

	// Halcyon is shown as its scene file authors it.
	REQUIRE(jump.Begin(assets.galaxy, assets.Index("Halcyon"), "Halcyon Gate"));
	RunJump(jump, assets);
	CHECK(shown.shows == 2);
	CHECK(shown.scene.StarSystem() == "systems/halcyon.yaml");
	CHECK(Named(shown.scene.Registry(), "highport-hub") != entt::null);
	CHECK(Named(shown.scene.Registry(), "corva") == entt::null);
}

TEST_CASE("Jump: the effect is held while the files are read, and while the destination streams in")
{
	Assets assets;
	Shown shown;
	Scene::JumpSettings settings;
	settings.streamTimeout = 1.0;
	Scene::JumpTransition jump{
	    *assets.resources, [&](Scene::JumpArrival &arrival) { shown.Show(arrival, 1.0f); }, settings};

	// The worker is kept busy, so the files are still being read when the effect covers the frame.
	std::promise<void> release;
	const std::shared_future<void> gate = release.get_future().share();
	assets.worker.Enqueue([gate] { gate.wait(); }, {});
	REQUIRE(jump.Begin(assets.galaxy, assets.Index("Marrow"), "Marrow Gate"));
	for (int frame = 0; frame < 40; ++frame)
	{
		jump.Advance(kFrame);
		assets.resources->Update();
	}
	CHECK(jump.Stage() == JumpStage::Reading);
	CHECK(jump.Fade() == 1.0f);
	CHECK(shown.shows == 0);
	release.set_value();
	assets.Settle();
	jump.Advance(kFrame);
	CHECK(shown.shows == 1);
	CHECK(jump.Stage() == JumpStage::Streaming);

	// What the destination asked for is not in, so the effect stays over it until the timeout.
	const auto pending = assets.resources->Load<Scene::TextFile>("systems/halcyon.yaml");
	for (int frame = 0; frame < 10; ++frame)
	{
		jump.Advance(kFrame);
		CHECK(jump.Stage() == JumpStage::Streaming);
		CHECK(jump.Fade() == 1.0f);
	}
	for (int frame = 0; frame < 20 && jump.Stage() == JumpStage::Streaming; ++frame)
		jump.Advance(kFrame);
	CHECK(jump.Stage() == JumpStage::FadingIn);
	CHECK(jump.StreamingSeconds() == doctest::Approx(1.0).epsilon(0.06));
	CHECK(assets.resources->Loading() == 1);
	assets.Settle();
	assets.resources->Release(pending);
	RunJump(jump, assets);
	CHECK_FALSE(jump.Active());

	// Once what it asked for has arrived, the effect lifts at the next frame.
	REQUIRE(jump.Begin(assets.galaxy, assets.Index("Vesper"), "Vesper Deep Gate"));
	while (jump.Stage() != JumpStage::Streaming)
	{
		jump.Advance(kFrame);
		assets.Settle();
	}
	const auto streamed = assets.resources->Load<Scene::TextFile>("systems/halcyon.yaml");
	jump.Advance(kFrame);
	jump.Advance(kFrame);
	CHECK(jump.Stage() == JumpStage::Streaming);
	assets.Settle();
	jump.Advance(kFrame);
	CHECK(jump.Stage() == JumpStage::FadingIn);
	assets.resources->Release(streamed);
}

TEST_CASE("Jump: a destination that fails is abandoned, and the effect lifts on the system it left")
{
	Assets assets;
	Shown shown;
	shown.scene.LoadSystem("systems/halcyon.yaml");
	const std::uint64_t generation = shown.scene.Generation();
	Scene::JumpTransition jump{
	    *assets.resources, [&](Scene::JumpArrival &arrival) { shown.Show(arrival, 1.0f); }};

	const auto abandons = [&](const Celestial::Galaxy &galaxy, std::size_t system, std::string arrival,
	                          const char *message)
	{
		CAPTURE(message);
		REQUIRE(jump.Begin(galaxy, system, std::move(arrival)));
		const std::vector<JumpStage> stages = RunJump(jump, assets);
		CHECK(stages.back() == JumpStage::Idle);
		CHECK(std::ranges::find(stages, JumpStage::Streaming) == stages.end());
		CHECK(jump.Error().find(message) != std::string::npos);
		CHECK(jump.Fade() == 0.0f);
		CHECK(shown.shows == 0);
		CHECK(shown.scene.Generation() == generation);
		CHECK(assets.resources->Loading() == 0);
	};

	Celestial::Galaxy missing = assets.galaxy;
	missing.systems[1].system = "systems/missing.yaml";
	abandons(missing, 1, {}, "it could not be read: 'systems/missing.yaml' is not in any mount");

	// A system file that is not the system the galaxy says is there.
	Celestial::Galaxy swapped = assets.galaxy;
	swapped.systems[1].system = "systems/marrow.yaml";
	abandons(swapped, 1, {}, "the system there is named 'Marrow'");

	Celestial::Galaxy unrelated = assets.galaxy;
	unrelated.systems[0].scene = "scene/main.yaml";
	abandons(unrelated, 0, {}, "names the system file '', not 'systems/halcyon.yaml'");

	abandons(assets.galaxy, 1, "Corva Refinery", "no jump point 'Corva Refinery' to arrive at");

	// The error goes with the next jump that does not fail.
	REQUIRE(jump.Begin(assets.galaxy, 1));
	RunJump(jump, assets);
	CHECK(jump.Error().empty());
	CHECK(shown.shows == 1);
}

TEST_CASE(
    "Jump: a jump between two systems unloads one and loads the other while the galaxy's "
    "simulation keeps ticking")
{
	// The phase's exit criterion, without a window: the simulation is the galaxy's, with ships in
	// Halcyon, in Vesper and on the lane between them, and presentation jumps from one to the other.
	RegisterTestTypes();
	constexpr double kStep = 1.0 / 60.0;
	Sim::SimWorld world{kStep, 6};
	world.RegisterComponent<Sim::Transform>();
	world.RegisterComponent<Sim::InSystem>();
	world.RegisterComponent<Cruise>();
	world.RegisterComponent<Transiting>();
	world.RegisterSystem(
	    {"cruise",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Cruise>().Writing<Sim::Transform>(),
	     {},
	     [](entt::registry &registry, double delta)
	     {
		     for (auto [entity, transform, cruise] :
		          registry.view<Sim::Transform, const Cruise>().each())
			     transform.position += cruise.velocity * delta;
	     }});
	world.RegisterSystem(
	    {"lanes",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Transiting>().Writing<Sim::InSystem, Sim::Transform>(),
	     {"cruise"},
	     [](entt::registry &registry, double)
	     {
		     std::vector<entt::entity> arrived;
		     for (auto [entity, transit] : registry.view<Transiting>().each())
			     if (--transit.stepsLeft == 0)
				     arrived.push_back(entity);
		     for (const entt::entity entity : arrived)
		     {
			     const Transiting transit = registry.get<Transiting>(entity);
			     registry.get<Sim::InSystem>(entity).system = transit.destination;
			     registry.get<Sim::Transform>(entity).position = transit.arrival;
			     registry.remove<Transiting>(entity);
		     }
	     }});

	const std::uint64_t halcyon = Sim::SystemId("Halcyon");
	const std::uint64_t vesper = Sim::SystemId("Vesper");
	entt::registry &simulation = world.Registry();
	const auto ship = [&](std::uint64_t system, double x, double speed)
	{
		const entt::entity entity = simulation.create();
		simulation.emplace<Sim::Transform>(entity, glm::dvec3{x, 0.0, 0.0});
		simulation.emplace<Sim::InSystem>(entity, system);
		simulation.emplace<Cruise>(entity, glm::dvec3{speed, 0.0, 0.0});
		return entity;
	};
	const entt::entity trader = ship(halcyon, 1.0e11, 250.0);
	const entt::entity miner = ship(halcyon, 1.5e11, -40.0);
	const entt::entity hauler = ship(vesper, 3.0e10, 120.0);
	const entt::entity tanker = ship(vesper, 3.1e10, 0.0);
	// Leaves Halcyon through its gate as presentation does, and comes out at Vesper's 45 steps
	// later, while the jump is still under way.
	const entt::entity courier = ship(halcyon, 4.0e11, 0.0);
	const glm::dvec3 vesperGate{6.0e10, 0.0, 0.0};
	Sim::Stepper stepper{world};

	Assets assets;
	Shown shown;
	shown.factory = [](entt::registry &presentation, entt::entity) { return presentation.create(); };
	Scene::JumpSettings settings;
	settings.fadeOutSeconds = 0.5;
	settings.fadeInSeconds = 0.5;
	Scene::JumpTransition jump{
	    *assets.resources, [&](Scene::JumpArrival &arrival) { shown.Show(arrival, 1.0f); }, settings};
	const auto traffic = [&shown]
	{
		std::vector<entt::entity> entities;
		for (const auto [entity, proxy] :
		     shown.scene.Registry().view<const Scene::SimProxyComponent>().each())
			entities.push_back(proxy.simEntity);
		std::ranges::sort(entities);
		return entities;
	};
	// One frame as the shell runs it: the simulation's step, then the jump, then the sync of the
	// scene shown, then the resources.
	std::uint64_t frames = 0;
	const auto frame = [&]
	{
		CHECK(stepper.Advance(kStep) == 1);
		++frames;
		jump.Advance(kStep);
		if (shown.systemScene)
		{
			shown.systemScene->SyncTraffic(stepper.Previous(), stepper.Current(), stepper.Alpha());
			shown.systemScene->Place(
			    Celestial::PresentationTime(
			        stepper.Previous().time, stepper.Current().time, stepper.Alpha()));
		}
		assets.Settle();
	};

	// Halcyon, as the galaxy names it, with its own traffic and no other.
	REQUIRE(jump.Begin(assets.galaxy, assets.Index("Halcyon")));
	while (jump.Active())
		frame();
	CHECK(shown.scene.StarSystem() == "systems/halcyon.yaml");
	CHECK(traffic() == std::vector{trader, miner, courier});

	// Through Halcyon's gate to Vesper, and the courier with it, as a game's command would send it.
	// The simulation steps once in every frame of the jump.
	const auto exit = Celestial::ExitThrough(assets.galaxy, assets.Index("Halcyon"), "Halcyon Gate");
	REQUIRE(exit);
	const std::uint64_t departed = world.Clock().Step();
	simulation.get<Sim::InSystem>(courier).system = Sim::kNoSystem;
	simulation.emplace<Transiting>(courier, vesper, std::uint64_t{45}, vesperGate);
	REQUIRE(jump.Begin(assets.galaxy, exit->destination, std::string{exit->arrival}));
	std::uint64_t shownAt = 0;
	while (jump.Active())
	{
		frame();
		if (shownAt == 0 && shown.shows == 2)
			shownAt = world.Clock().Step();
	}
	CHECK(world.Clock().Step() == frames);
	CHECK(world.Clock().Step() - departed > 40);
	// Vesper's bodies have replaced Halcyon's, and its traffic Halcyon's: the courier among it,
	// having come out of the lane after Vesper was shown and before the effect lifted.
	CHECK(shown.scene.StarSystem() == "systems/vesper.yaml");
	CHECK(shown.arrival == "Vesper Gate");
	CHECK(Named(shown.scene.Registry(), "ostra-highport") == entt::null);
	CHECK(Named(shown.scene.Registry(), "highport-hub") == entt::null);
	CHECK(Named(shown.scene.Registry(), "corva-refinery") != entt::null);
	CHECK(shownAt != 0);
	CHECK(shownAt - departed < 45);
	CHECK(traffic() == std::vector{hauler, tanker, courier});
	for (const auto [entity, proxy, transform] :
	     shown.scene.Registry()
	         .view<const Scene::SimProxyComponent, const Scene::TransformComponent>()
	         .each())
		if (proxy.simEntity == courier)
			CHECK(transform.translation == vesperGate);

	// Halcyon's ships went on while it was not shown, and are where every step since put them.
	const std::uint64_t steps = world.Clock().Step();
	const Sim::Snapshot &now = stepper.Current();
	CHECK(now.FindSample(trader)->system == halcyon);
	CHECK(
	    now.Find(trader)->position.x ==
	    doctest::Approx(1.0e11 + 250.0 * kStep * static_cast<double>(steps)));
	CHECK(
	    now.Find(miner)->position.x ==
	    doctest::Approx(1.5e11 - 40.0 * kStep * static_cast<double>(steps)));
	CHECK(
	    now.Find(hauler)->position.x ==
	    doctest::Approx(3.0e10 + 120.0 * kStep * static_cast<double>(steps)));
	CHECK(now.FindSample(courier)->system == vesper);

	// And back: Halcyon's scene again, its traffic as it is now.
	REQUIRE(jump.Begin(assets.galaxy, assets.Index("Halcyon"), "Halcyon Gate"));
	while (jump.Active())
		frame();
	CHECK(world.Clock().Step() == frames);
	CHECK(shown.scene.StarSystem() == "systems/halcyon.yaml");
	CHECK(traffic() == std::vector{trader, miner});
}
