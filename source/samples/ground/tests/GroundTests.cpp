#include "Ground.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <jettison/app/AppArguments.h>
#include <jettison/app/Game.h>
#include <jettison/host/Session.h>
#include <jettison/input/Actions.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sample/GroundGame.h>
#include <jettison/sample/RoverInput.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>
#include <jettison/scene/SimProxies.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;
constexpr Sim::Controller kPlayer{Sim::ControllerKind::Player, 0};

// The ground sample's world, started through a session over its content as the shell starts it.
std::unique_ptr<Host::Session> StartGround()
{
	return std::make_unique<Host::Session>(
	    Host::Options{{{"base", JETTISON_GROUND_CONTENT}}, kStep, 1},
	    std::make_unique<Sample::GroundWorld>());
}


struct Fixture
{
	std::unique_ptr<Host::Session> session{StartGround()};
	Sim::SimWorld &world{session->World()};
	entt::entity rover{Sample::FindRover(world.Registry())};


	void Drive(const Physics::FlightControls &controls, int steps)
	{
		world.Commands().Push(Physics::SetFlightControls{kPlayer, rover, controls});
		for (int step = 0; step < steps; ++step)
			world.Step();
	}


	const Sim::Transform &Rover() const
	{
		return world.Registry().get<Sim::Transform>(rover);
	}
};
} // namespace


TEST_CASE("Ground: the rover drives ahead and turns on the ground, under gravity, at its helm")
{
	Fixture fixture;
	CHECK(Sample::FindRover(fixture.world.Registry()) == fixture.rover);

	// Let go, it stays where it stands.
	fixture.Drive({}, 60);
	CHECK(fixture.Rover().position.y == doctest::Approx(0.4).epsilon(0.02));
	CHECK(std::abs(fixture.Rover().position.z) < 0.01);

	// Five seconds ahead, along its -Z, held on the ground: 6 m/s² towards 12 m/s.
	Physics::FlightControls ahead;
	ahead.linear.z = -1.0f;
	fixture.Drive(ahead, 300);
	CHECK(fixture.Rover().position.z < -30.0);
	CHECK(fixture.Rover().position.y == doctest::Approx(0.4).epsilon(0.02));

	// Turning left as it drives swings its nose towards -X, and keeps it upright.
	Physics::FlightControls turning = ahead;
	turning.angular.y = 1.0f;
	const double x = fixture.Rover().position.x;
	fixture.Drive(turning, 120);
	CHECK(fixture.Rover().position.x < x - 1.0);
	const glm::dvec3 up = fixture.Rover().orientation * glm::dvec3{0.0, 1.0, 0.0};
	CHECK(up.y > 0.99);
}


TEST_CASE("Ground: the same drive gives the same world, and a save taken part-way carries on to it")
{
	Physics::FlightControls ahead;
	ahead.linear.z = -1.0f;
	Physics::FlightControls turning = ahead;
	turning.angular.y = 1.0f;

	Fixture first;
	first.Drive(ahead, 120);
	const std::vector<std::byte> saved = first.world.Save();
	first.Drive(turning, 120);

	Fixture second;
	second.Drive(ahead, 120);
	CHECK(second.world.Save() == saved);
	second.Drive(turning, 120);
	CHECK(second.world.StateHash() == first.world.StateHash());

	Fixture loaded;
	loaded.world.Load(saved);
	loaded.rover = Sample::FindRover(loaded.world.Registry());
	REQUIRE(loaded.rover == first.rover);
	loaded.Drive(turning, 120);
	CHECK(loaded.world.StateHash() == first.world.StateHash());
}


TEST_CASE("Ground: W and S drive the rover, and A and D turn it, in the game's context")
{
	using Input::Keys::Character;
	Input::InputSystem input;
	const Sample::RoverInput keys{input};
	input.PushContext(Sample::RoverInput::kContext);

	input.SetButton(Character('w'), true);
	input.SetButton(Character('a'), true);
	input.Poll();
	Physics::FlightControls controls = keys.Read(input);
	CHECK(controls.linear.z == -1.0f);
	CHECK(controls.angular.y == 1.0f);

	input.SetButton(Character('w'), false);
	input.SetButton(Character('a'), false);
	input.SetButton(Character('s'), true);
	input.SetButton(Character('d'), true);
	input.Poll();
	controls = keys.Read(input);
	CHECK(controls.linear.z == 1.0f);
	CHECK(controls.angular.y == -1.0f);
}

namespace
{
// The shell as the ground game reads it, without a window: the sample's world started through a
// session, its scene,
// the input with the game's context pushed, and the snapshots taken again on a resync. What the game
// does not read throws.
class TestShell final : public App::Shell
{
  public:
	TestShell()
	{
		scene.Load(JETTISON_GROUND_SCENE);
	}


	const App::AppArguments &Arguments() const override
	{
		return arguments;
	}


	Sim::SimWorld &World() override
	{
		return world;
	}


	void Resync() override
	{
		snapshot = Sim::Capture(world);
	}


	Scene::Scene &Scene() override
	{
		return scene;
	}


	const Sim::Snapshot &Snapshot() const override
	{
		return snapshot;
	}


	const Sim::Snapshot &Previous() const override
	{
		return snapshot;
	}


	double Alpha() const override
	{
		return 1.0;
	}


	entt::entity ActiveCamera() const override
	{
		return entt::null;
	}


	Render::DebugDraw &DebugLines() override
	{
		throw std::logic_error{"not read"};
	}


	bool DebugShown() const override
	{
		return false;
	}


	void SetFade(const glm::vec3 & /*colour*/, float /*amount*/) override
	{
	}


	bool SceneReady() const override
	{
		return true;
	}


	Vfs::FileSystem &Assets() override
	{
		throw std::logic_error{"not read"};
	}


	Resource::ResourceManager &Resources() override
	{
		throw std::logic_error{"not read"};
	}


	DevTools::Console &Console() override
	{
		throw std::logic_error{"not read"};
	}


	Input::InputSystem &Input() override
	{
		return input;
	}


	void SetGameContext(std::string_view context) override
	{
		input.PushContext(context);
	}


	const Sim::DefinitionRegistry &Definitions() const override
	{
		throw std::logic_error{"not read"};
	}


	Script::ScriptHost &Scripts() override
	{
		throw std::logic_error{"not read"};
	}


	Sim::Controller Player() const override
	{
		return kPlayer;
	}


	void Frame(entt::entity /*target*/) override
	{
	}


	void PlaySequence(std::string_view /*name*/, double /*from*/) override
	{
	}


	void ClearPick() override
	{
	}


	Ui::UiSystem *GameUi() override
	{
		return nullptr;
	}


	void UpdateGameUi() override
	{
	}


	std::string Word(const Sim::Reason & /*reason*/) const override
	{
		throw std::logic_error{"not read"};
	}


	std::string Describe(entt::entity /*entity*/) const override
	{
		return {};
	}


	std::uint64_t UiSounds() const override
	{
		return 0;
	}


	bool SmokeTest() const override
	{
		return false;
	}

	App::AppArguments arguments;
	std::unique_ptr<Host::Session> session{StartGround()};
	Sim::SimWorld &world{session->World()};
	Scene::Scene scene;
	Input::InputSystem input;
	Sim::Snapshot snapshot;
};


// A frame of the shell's: the keys polled, read by the game, a step, and the proxies placed from the
// snapshots, as the shell's sync does.
void Frame(Sample::GroundGame &game, TestShell &shell)
{
	shell.input.Poll();
	game.PollInput(shell.input, shell);
	shell.world.Step();
	const Sim::Snapshot previous = shell.snapshot;
	shell.Resync();
	Scene::SyncSimProxies(shell.scene.Registry(), previous, shell.snapshot, 1.0);
}
} // namespace


TEST_CASE("Ground: the game drives its rover from the keys, through a save held at full ahead")
{
	using Input::Keys::Character;
	TestShell shell;
	Sample::GroundGame game;
	game.Register(shell);
	game.SceneLoaded(shell);
	const entt::entity rover = Sample::FindRover(shell.world.Registry());
	REQUIRE(rover != entt::null);
	CHECK(game.Describe(rover) == "the rover");
	CHECK(game.Describe(Sample::FindGround(shell.world.Registry())) == "the ground");
	const auto controls = [&] { return shell.world.Registry().get<Physics::FlightControls>(rover); };

	// The rover's proxy is the game's, built rather than authored, so the scene written while the rover
	// drives is the scene loaded.
	entt::registry &scene = shell.scene.Registry();
	const auto proxies = scene.view<const Scene::SimProxyComponent>();
	REQUIRE(proxies.size() == 1);
	const entt::entity proxy = proxies.front();
	CHECK(proxies.get<const Scene::SimProxyComponent>(proxy).simEntity == rover);
	CHECK(scene.all_of<Scene::BuiltComponent>(proxy));
	const std::string authored = shell.scene.SaveText();

	shell.input.SetButton(Character('w'), true);
	for (int frame = 0; frame < 60; ++frame)
		Frame(game, shell);
	CHECK(controls().linear.z == -1.0f);
	CHECK(scene.get<Scene::TransformComponent>(proxy).translation.z < -1.0);
	CHECK(shell.scene.SaveText() == authored);

	// Saved with W held, then let go, then loaded: the keys, still let go, stop the rover again.
	const std::vector<std::byte> saved = shell.world.Save();
	shell.input.SetButton(Character('w'), false);
	Frame(game, shell);
	CHECK(controls().linear.z == 0.0f);
	shell.world.Load(saved);
	game.Loaded(shell);
	REQUIRE(controls().linear.z == -1.0f);
	Frame(game, shell);
	CHECK(controls().linear.z == 0.0f);
	CHECK(proxies.size() == 1);

	// The rover's events are the player's to hear, and another's are not.
	const entt::entity other = shell.world.Registry().create();
	CHECK(game.Hears({0, entt::meta_any{Physics::FlightControlsRefused{kPlayer, rover, {}}}}));
	CHECK_FALSE(game.Hears({0, entt::meta_any{Physics::FlightControlsRefused{kPlayer, other, {}}}}));
}


TEST_CASE("Ground: the game loads its own saves, and refuses another game's")
{
	TestShell shell;
	Sample::GroundGame game;
	Sim::SaveGame own;
	game.WriteSave(own, shell);
	CHECK_NOTHROW(game.ReadSave(own, shell));
	Sim::SaveGame another;
	another.info.details.push_back({"galaxy", "galaxy"});
	CHECK_THROWS_WITH_AS(
	    game.ReadSave(another, shell), "the save is not the ground sample's", std::runtime_error);
}
