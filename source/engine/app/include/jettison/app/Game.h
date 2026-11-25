#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <jettison/sim/Controller.h>

namespace Jettison::DevTools
{
class Console;
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
struct AppArguments;
struct SmokeActions;

// A scene entity the shell's orbit camera frames, out to three of its radii in metres and no nearer
// than 50 m: C moves the camera from one to the next, in entity order, and the console's orbit names
// one. A game marks them; not written to the scene file.
struct OrbitTarget
{
	std::string name;
	double radius{0.0};
};

// Where a pick is cast (docs/physics.md): the frame of the world's physics it is cast
// in, the entity it passes through, which the camera is inside, and whether what it picks is a target,
// outlined as one (Scene::OutlineStyle::Targeted), rather than a selection.
struct PickSpace
{
	std::uint64_t frame{0};
	entt::entity ignore{entt::null};
	bool targeting{false};
};

// What the shell shows and follows, as a game composed into it (Game) reads it. The shell outlives
// what a hook registers with it (frame systems, console commands, smoke actions), which may keep a
// reference to it; what it returns is not kept past the call.
class Shell
{
  public:
	// The command line the shell was started with.
	virtual const AppArguments &Arguments() const = 0;

	virtual Sim::SimWorld &World() = 0;

	// The snapshots taken again from the world, after a game changed it outside a step.
	virtual void Resync() = 0;

	// The scene shown, which is presentation's world: there once the shell has started
	// (Game::SceneLoaded), and replaced as it reloads or the game loads another.
	virtual Scene::Scene &Scene() = 0;

	// The newest snapshot and the one before it, which presentation shows the world between, Alpha of
	// the way from the earlier to the newer.
	virtual const Sim::Snapshot &Snapshot() const = 0;

	virtual const Sim::Snapshot &Previous() const = 0;

	virtual double Alpha() const = 0;

	// The scene's entity whose camera is active (Scene::CameraComponent::active), or null.
	virtual entt::entity ActiveCamera() const = 0;

	// The frame's debug lines, cleared as each frame starts, and whether the scene's are shown (F2),
	// which a game's own wait for.
	virtual Render::DebugDraw &DebugLines() = 0;

	virtual bool DebugShown() const = 0;

	// The screen faded towards a colour, by 0 (none) to 1 (all of it), until it is set again; nothing
	// until the window is open.
	virtual void SetFade(const glm::vec3 &colour, float amount) = 0;

	// The asset tree, its mounts and the user's writable one, and the resources read through it.
	virtual Vfs::FileSystem &Assets() = 0;

	virtual Resource::ResourceManager &Resources() = 0;

	virtual DevTools::Console &Console() = 0;

	virtual Input::InputSystem &Input() = 0;

	// An input context of the game's, which the shell keeps over its own and under the console's; an
	// empty name for none.
	virtual void SetGameContext(std::string_view context) = 0;

	virtual const Sim::DefinitionRegistry &Definitions() const = 0;

	// The mods' scripts, loaded with the definitions.
	virtual Script::ScriptHost &Scripts() = 0;

	// The player the shell's controls are for.
	virtual Sim::Controller Player() const = 0;

	// Every orbit camera eased onto the entity of the scene, a target the game marks (OrbitTarget).
	virtual void Frame(entt::entity target) = 0;

	// A sequence in cinematics/<name>.sequence.yaml played on the active camera from a time in
	// seconds; the console's sequence does the same.
	virtual void PlaySequence(std::string_view name, double from) = 0;

	// What was picked forgotten, and the pick waiting for its answer too.
	virtual void ClearPick() = 0;

	// The game UI: null until the window is open.
	virtual Ui::UiSystem *GameUi() = 0;

	// The game UI's presentation phase now, its screens made again (Game::UpdateUi) and laid out, as
	// the frame would have them.
	virtual void UpdateGameUi() = 0;

	// A reason as the player reads it, and an entity as the log names it (Game::Describe).
	virtual std::string Word(const Sim::Reason &reason) const = 0;

	virtual std::string Describe(entt::entity entity) const = 0;

	// Sounds started on the UI bus so far.
	virtual std::uint64_t UiSounds() const = 0;

	// Whether this is the headless render test's run, which shows every screen from its first frame.
	virtual bool SmokeTest() const = 0;

  protected:
	~Shell() = default;
};

// The simulation the shell attaches to its world, which a game's own is built on: physics.
struct ShellWorld
{
	Sim::SimWorld &world;
	Physics::PhysicsWorld &physics;
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
	// systems on the world, and its input actions (Shell::Input), bound before the settings'
	// bindings are read.
	virtual void Register(const ShellWorld &world, Shell &shell);

	// The game's definitions, read through the asset tree before the mods' scripts load.
	virtual void LoadDefinitions(Sim::DefinitionRegistry &definitions);

	// Once the docks have their shapes and layouts: what the game builds on them.
	virtual void Define(const ShellWorld &world, const Sim::DefinitionRegistry &definitions);

	// A save loaded into the world: what an older save of the game lacks, and what the game read of it
	// (ReadSave) applied.
	virtual void Loaded(Shell &shell);

	// The game's console commands, which run with the shell they are given.
	virtual void AddCommands(DevTools::Console &console, Shell &shell);

	// As the shell is made, once its own frame systems are registered: the game's, in the shell's
	// phases, ordered against the shell's by name (Ecs::SystemDescription::after and before). The
	// shell validates the order once this returns, so a system naming none there is, or closing a
	// cycle, throws from the shell's constructor.
	virtual void RegisterFrame(Ecs::Scheduler &frame, Shell &shell);

	// The scene a run of no --scene loads, by virtual path: scene/main.yaml unless the
	// game names another. Without the file the scene is empty.
	virtual std::string_view DefaultScene() const;

	// Each scene the shell loads, once it is loaded, and before the shell replaces it: as it starts and
	// as it reloads.
	virtual void SceneLoaded(Shell &shell);

	virtual void SceneUnloading(Shell &shell);

	// The sync phase's proxies of the world's entities in the scene, made, placed and unmade from the
	// snapshots, where the game keeps them itself: true if it did. False, the default, leaves them to
	// the shell (Scene::SyncSimProxies).
	virtual bool SyncProxies(Shell &shell);

	// As the shell shuts down, before it releases the scene and the resources: what the game holds of
	// either released. Must not throw.
	virtual void ShuttingDown(Shell &shell) noexcept;

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

	// Whether the player hears an event, whose sound the shell then plays (audio/EventSounds.h): one
	// about what they follow, as the game names it (EventEntity). None, the default.
	virtual bool Hears(const Sim::SimEvent &event) const;

	// An entity as the log names it, where the game names it; none leaves it to the shell, which
	// names it by its number.
	virtual std::optional<std::string> Describe(entt::entity entity) const;

	// Where the player's click picks, or none, the default, and the shell does not pick; and where the
	// origin of the frame a hit is kept relative to is, in its frame, at a time in simulation
	// seconds, so a mark on a moving body moves with it.
	virtual std::optional<PickSpace> PickIn(const Shell &shell) const;

	virtual glm::dvec3 PickOrigin(const Shell &shell, double simSeconds) const;

	// What a reason's argument reads as, where the game names it: an entity in the snapshot, or an
	// identifier of one of its definitions' kinds (Reflect's name for the definition). None leaves it
	// to the shell.
	virtual std::optional<std::string> NameEntity(
	    const Sim::Snapshot &snapshot, entt::entity entity) const;

	virtual std::optional<std::string> NameIdentifier(
	    std::string_view definition, const std::string &identifier,
	    const Sim::DefinitionRegistry &definitions, const Ui::StringTable &strings) const;

	// The render test's stages (SmokeSequence) the game drives: the shell's actions, which it replaces
	// where it drives a stage itself. The shell's game UI stage has no screen to drive.
	virtual void DriveSmoke(SmokeActions &actions, Shell &shell);
};

// The entity an event names in a reflected field of that name, or null for an event with no such
// field or one that does not hold an entity.
entt::entity EventEntity(const Sim::SimEvent &event, std::string_view field);

} // namespace Jettison::App
