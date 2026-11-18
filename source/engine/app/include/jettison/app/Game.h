#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <jettison/sim/Controller.h>

namespace Jettison::Ai
{
class Behaviours;
}

namespace Jettison::Space
{
class Gates;
class LaneTravel;
} // namespace Jettison::Space

namespace Jettison::Celestial
{
struct StarSystem;
}
namespace Jettison::DevTools
{
class Console;
}
namespace Jettison::Dock
{
class Docks;
}
namespace Jettison::Ecs
{
class Scheduler;
}
namespace Jettison::Input
{
class InputSystem;
}
namespace Jettison::Physics
{
class PhysicsWorld;
}
namespace Jettison::Render
{
class DebugDraw;
}
namespace Jettison::Resource
{
class ResourceManager;
}
namespace Jettison::Scene
{
class Scene;
}
namespace Jettison::Script
{
class ScriptHost;
}
namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
struct Reason;
struct SaveGame;
struct SimEvent;
struct Snapshot;
} // namespace Jettison::Sim
namespace Jettison::Ui
{
class StringTable;
class UiSystem;
} // namespace Jettison::Ui
namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::App
{
struct SmokeActions;

// What the shell shows and follows, as a game composed into it (Game) reads it. The shell outlives
// what a hook registers with it (frame systems, console commands, smoke actions), which may keep a
// reference to it; what it returns is not kept past the call.
class Shell
{
  public:
	virtual Sim::SimWorld &World() = 0;

	// The scene shown, which is presentation's world: there once the shell has started
	// (Game::SceneLoaded), and replaced as it reloads or a jump arrives.
	virtual Scene::Scene &Scene() = 0;

	// The newest snapshot and the one before it, which presentation shows the world between, Alpha of
	// the way from the earlier to the newer.
	virtual const Sim::Snapshot &Snapshot() const = 0;

	virtual const Sim::Snapshot &Previous() const = 0;

	virtual double Alpha() const = 0;

	// The scene's entity whose camera is active (Scene::CameraComponent::active), or null.
	virtual entt::entity ActiveCamera() const = 0;

	// The frame's debug lines, cleared as each frame starts.
	virtual Render::DebugDraw &DebugLines() = 0;

	// The screen faded towards a colour, by 0 (none) to 1 (all of it), until it is set again; nothing
	// until the window is open.
	virtual void SetFade(const glm::vec3 &colour, float amount) = 0;

	// The asset tree, its mounts and the user's writable one, and the resources read through it.
	virtual Vfs::FileSystem &Assets() = 0;

	virtual Resource::ResourceManager &Resources() = 0;

	virtual DevTools::Console &Console() = 0;

	virtual Input::InputSystem &Input() = 0;

	virtual const Sim::DefinitionRegistry &Definitions() const = 0;

	// The player the shell's controls are for, the entity of theirs it follows and the station that
	// entity is bound to, as simulation entities (null for none), and whether it is docked there.
	virtual Sim::Controller Player() const = 0;

	virtual entt::entity Followed() const = 0;

	virtual entt::entity Station() const = 0;

	virtual bool Docked() const = 0;

	// The name of the star system the scene shows, or empty.
	virtual std::string ShownSystem() const = 0;

	// The game UI: null until the window is open.
	virtual Ui::UiSystem *GameUi() = 0;

	// The game UI's presentation phase now, its screens made again (Game::UpdateUi) and laid out, as
	// the frame would have them.
	virtual void UpdateGameUi() = 0;

	// Follows the entity, as ] and [ do.
	virtual void Follow(entt::entity entity) = 0;

	// A reason as the player reads it, and an entity as the log names it.
	virtual std::string Word(const Sim::Reason &reason) const = 0;

	virtual std::string Describe(entt::entity entity) const = 0;

	// Sounds started on the UI bus so far.
	virtual std::uint64_t UiSounds() const = 0;

	// Whether this is the headless render test's run, which shows every screen from its first frame.
	virtual bool SmokeTest() const = 0;

  protected:
	~Shell() = default;
};

// The simulation the shell attaches to its world, which a game's own is built on: physics, lane and
// gate travel, the docks and the behaviour trees.
struct ShellWorld
{
	Sim::SimWorld &world;
	Physics::PhysicsWorld &physics;
	Space::LaneTravel &lanes;
	Space::Gates &gates;
	Dock::Docks &docks;
	Ai::Behaviours &behaviours;
};

struct ShellShip
{
	std::string hull;
	float mass{0.0f};
};

// A game composed into the shell (RunRenderApplication): its types, definitions and world, its
// commands and its UI, at the shell's points. The shell without one is the engine with no game: no
// market, trader or fleet of the game's is registered, and nothing here is called. Every hook does
// nothing by default.
class Game
{
  public:
	virtual ~Game() = default;

	// As the shell is made, after its own systems attach: the game's types, components, commands and
	// systems on the world, and its input actions, bound before the settings' bindings are read.
	virtual void Register(const ShellWorld &world, Input::InputSystem &input);

	// The game's definitions, read through the asset tree before the mods' scripts load.
	virtual void LoadDefinitions(Sim::DefinitionRegistry &definitions);

	// Once the docks have their hulls and layouts: what the game builds on them.
	virtual void Define(const ShellWorld &world, const Sim::DefinitionRegistry &definitions);

	// Once the galaxy's systems are known, before the scene loads: what the game runs across them,
	// whose inputs the mods' scripts may answer.
	virtual void OpenGalaxy(
	    const ShellWorld &world, std::span<const Celestial::StarSystem> systems,
	    const Sim::DefinitionRegistry &definitions, Script::ScriptHost &scripts);

	// Each station the shell builds to its layout, as it is built, and then all of them once they are:
	// once a run, as a load brings its own.
	virtual void StationOpened(
	    Sim::SimWorld &world, Dock::Docks &docks, entt::entity station, std::string_view body);

	virtual void StationsOpened(const ShellWorld &world, const Sim::DefinitionRegistry &definitions);

	// The hull of the game's content the shell makes the player's first ship and the console's
	// traffic of, and its mass in kilograms; with none, the default, it makes neither.
	virtual std::optional<ShellShip> StarterShip() const;

	// The player's first ship as the shell makes it, before they take its helm.
	virtual void FitShip(entt::registry &world, entt::entity ship, const Sim::Controller &player);

	// A save loaded into the world, the player's ship as its shell section names it: what an older
	// save of the game lacks.
	virtual void Loaded(entt::registry &world, entt::entity ship, const Sim::Controller &player);

	// The game's console commands, which run with the shell they are given.
	virtual void AddCommands(DevTools::Console &console, Shell &shell);

	// As the shell is made, once its own frame systems are registered: the game's, in the shell's
	// phases, ordered against the shell's by name (Ecs::SystemDescription::after and before). The
	// shell validates the order once this returns, so a system naming none there is, or closing a
	// cycle, throws from the shell's constructor.
	virtual void RegisterFrame(Ecs::Scheduler &frame, Shell &shell);

	// Each scene the shell shows, once it is loaded, and before the shell replaces it: as it starts, as
	// it reloads and as a jump arrives. Not as the shell shuts down.
	virtual void SceneLoaded(Shell &shell);

	virtual void SceneUnloading(Shell &shell);

	// A save the shell writes: the game's sections beside the shell's, and its details, which the
	// saves' list shows and a load reads first. A section of a name already taken throws, and nothing
	// is saved.
	virtual void WriteSave(Sim::SaveGame &save, Shell &shell);

	// A save the shell is about to load, before anything of the world is replaced: the game reads its
	// sections and checks its details, and throws, saying why, for a save not its own or one that does
	// not fit, which leaves the world as it was. What it read it applies once the world has loaded
	// (Loaded).
	virtual void ReadSave(const Sim::SaveGame &save, Shell &shell);

	// Once the game UI is made: the game's data models and documents.
	virtual void OpenUi(Ui::UiSystem &ui, Shell &shell);

	// The input phase, the game's actions read; and Escape, which closes what the game has open
	// before the shell's panels, saying whether it closed anything.
	virtual void PollInput(const Input::InputSystem &input, Shell &shell);

	virtual bool Back(Shell &shell);

	// The presentation phase, before the game UI lays out.
	virtual void UpdateUi(Shell &shell);

	// The game's dev-tools windows, drawn with ImGui each frame after the shell's: devTools says
	// whether the dev tools are shown (F1), which a panel of the tools' own waits for.
	virtual void DrawPanels(Shell &shell, bool devTools);

	// Each event of the steps, after the shell's own handling of it.
	virtual void TakeEvent(const Sim::SimEvent &event, Shell &shell);

	// What a reason's argument reads as, where the game names it: an entity in the snapshot, or an
	// identifier of one of its definitions' kinds (Reflect's name for the definition). None leaves it
	// to the shell.
	virtual std::optional<std::string> NameEntity(
	    const Sim::Snapshot &snapshot, entt::entity entity) const;

	virtual std::optional<std::string> NameIdentifier(
	    std::string_view definition, const std::string &identifier,
	    const Sim::DefinitionRegistry &definitions, const Ui::StringTable &strings) const;

	// Who flies a ship of the player's fleet and what it carries, for the console's fleet list: the
	// player, at its helm, or its behaviour.
	virtual std::string DescribeShip(
	    const Sim::Snapshot &snapshot, entt::entity ship, bool atHelm) const;

	// The render test's stages (SmokeSequence) the game drives: the shell's actions, which it replaces
	// where it drives a stage itself. The shell's game UI stage has no screen to drive.
	virtual void DriveSmoke(SmokeActions &actions, Shell &shell);
};

} // namespace Jettison::App
