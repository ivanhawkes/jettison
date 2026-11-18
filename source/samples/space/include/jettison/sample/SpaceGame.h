#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <jettison/app/Game.h>
#include <jettison/input/Actions.h>
#include <jettison/ui/UiSystem.h>

namespace Jettison::Ai
{
class Behaviours;
}
namespace Jettison::Celestial
{
struct StarSystem;
}
namespace Jettison::Dock
{
class Docks;
}
namespace Jettison::Space
{
class Gates;
class LaneTravel;
} // namespace Jettison::Space

namespace Jettison::Sample
{
struct MarketScreen;
class SpaceLayer;

// The simulation the space layer attaches to the world (SpaceLayer), which the sample's own is built
// on: physics, lane and gate travel, the docks and the behaviour trees.
struct SpaceWorld
{
	Sim::SimWorld &world;
	Physics::PhysicsWorld &physics;
	Space::LaneTravel &lanes;
	Space::Gates &gates;
	Dock::Docks &docks;
	Ai::Behaviours &behaviours;
};

// The hull of the sample's content the layer makes the player's first ship and the console's traffic
// of, and its mass in kilograms.
struct ShipKind
{
	std::string hull;
	float mass{0.0f};
};

// The space-trading sample (docs/sample-game.md) as a game composed into the shell: the space layer
// (SpaceLayer), and on it goods and their markets at the galaxy's stations, the traders that run
// between them, the player's fleet with its shipyard, the ships parked at the stations, and the market
// screen. Jetsam and the render test run the shell with it; Flotsam runs it with none. It is the
// engine's test coverage of a game, not a game's: Manifest's rules, prices and content are
// Manifest's.
class SpaceGame final : public App::Game
{
  public:
	SpaceGame();

	~SpaceGame() override;

	SpaceGame(const SpaceGame &) = delete;

	SpaceGame &operator=(const SpaceGame &) = delete;

	// The space layer, then the sample's own (RegisterOwn).
	void Register(const App::ShellWorld &world, App::Shell &shell) override;

	void LoadDefinitions(Sim::DefinitionRegistry &definitions) override;

	// The docks, the sample's own (DefineOwn), then the galaxy (SpaceLayer::OpenGalaxy).
	void Define(const App::ShellWorld &world, const Sim::DefinitionRegistry &definitions) override;

	void Loaded(App::Shell &shell) override;

	// traders, buy, trade, drydock, repair and market, beside the layer's.
	void AddCommands(DevTools::Console &console, App::Shell &shell) override;

	void RegisterFrame(Ecs::Scheduler &frame, App::Shell &shell) override;

	void SceneLoaded(App::Shell &shell) override;

	void SceneUnloading(App::Shell &shell) override;

	bool SyncProxies(App::Shell &shell) override;

	void ShuttingDown(App::Shell &shell) noexcept override;

	void WriteSave(Sim::SaveGame &save, App::Shell &shell) override;

	void ReadSave(const Sim::SaveGame &save, App::Shell &shell) override;

	void OpenUi(Ui::UiSystem &ui, App::Shell &shell) override;

	// frigate shows and hides the market screen; Escape hides it.
	void PollInput(const Input::InputSystem &input, App::Shell &shell) override;

	bool Back(App::Shell &shell) override;

	// The market screen made again from the snapshot, when the step or what the shell shows moved.
	void UpdateUi(App::Shell &shell) override;

	void DrawPanels(App::Shell &shell, bool devTools) override;

	// A trade's answer, shown on the screen, a ship bought, followed, and a repair's answer.
	void TakeEvent(const Sim::SimEvent &event, App::Shell &shell) override;

	// The large-world scene: a station over a planet at real scale, in the galaxy's first system.
	std::string_view DefaultScene() const override;

	entt::entity Followed() const override;

	std::optional<std::string> Describe(entt::entity entity) const override;

	std::optional<App::PickSpace> PickIn(const App::Shell &shell) const override;

	glm::dvec3 PickOrigin(const App::Shell &shell, double simSeconds) const override;

	// A market by its station, and a good or a hull by its name.
	std::optional<std::string> NameEntity(
	    const Sim::Snapshot &snapshot, entt::entity entity) const override;

	std::optional<std::string> NameIdentifier(
	    std::string_view definition, const std::string &identifier,
	    const Sim::DefinitionRegistry &definitions, const Ui::StringTable &strings) const override;

	// The layer's stages, piloting and the jump, and the game UI's: the market screen shown, checked
	// against the snapshot's market, and its first Buy pressed; then its answer, shown and heard.
	void DriveSmoke(App::SmokeActions &actions, App::Shell &shell) override;

	// What the layer asks of the sample, and what its suite composes it from without one.

	// The sample's types, components, commands and systems on the world, and its actions.
	void RegisterOwn(const SpaceWorld &world, Input::InputSystem &input);

	// The fleet's command, which sells from the shipyard, of hulls the docks now know.
	static void DefineOwn(const SpaceWorld &world, const Sim::DefinitionRegistry &definitions);

	// The traders, by their behaviour tree and the trade decision, unless it asks for an input neither
	// a trader nor a script answers.
	void OpenGalaxy(
	    const SpaceWorld &world, std::span<const Celestial::StarSystem> systems,
	    const Sim::DefinitionRegistry &definitions, Script::ScriptHost &scripts);

	// The sample's ships parked on two or three pads of each station, leaving at least two free, so a
	// ship flown to one finds some taken: heavy transports on three of Highport's twelve destroyer
	// pads, and at Corva two transports on frigate pads and a fighter on a corvette, each with its
	// hull's drive.
	static void StationOpened(
	    Sim::SimWorld &world, Dock::Docks &docks, const Sim::DefinitionRegistry &definitions,
	    entt::entity station, std::string_view body);

	// The markets, bound to their stations, and the traders on their pads.
	static void StationsOpened(const SpaceWorld &world, const Sim::DefinitionRegistry &definitions);

	// The shuttle, about ten metres long as its proxy draws it, whose hull (defs/hulls/shuttle.yaml)
	// is the proxy's bounds.
	static ShipKind StarterShip();

	// A small hauler's hold, fitted to trade once the player leaves its helm, and the player's credits,
	// enough to fill most of the hold with what the station has to spare.
	static void FitShip(entt::registry &world, entt::entity ship, const Sim::Controller &player);

	// Purses kept on ships before they were their owners', pads of the classes' old names, and a ship
	// saved before the helm (13d), the player's as the save's shell section names it.
	static void UpgradeSave(entt::registry &world, entt::entity ship, const Sim::Controller &player);

	// Who flies a ship of the player's fleet and what it carries, for the console's fleet list: the
	// player, at its helm, or its behaviour, trading a good or choosing a run, and the crates in the
	// hold.
	static std::string DescribeShip(const Sim::Snapshot &snapshot, entt::entity ship, bool atHelm);

  private:
	std::optional<bool> SmokeMarket(App::Shell &shell);

	bool SmokeTradeShown(App::Shell &shell);

	// The market the screen shows: the one at the ship's station, else the first in the system shown,
	// else none.
	entt::entity ShownMarket(App::Shell &shell) const;

	void ShowMarket(App::Shell &shell, bool shown);

	// A Trade the next step applies, from the screen's buttons: crates bought, or sold when negative.
	void SendTrade(App::Shell &shell, std::string_view good, std::int64_t crates) const;

	void LogTraders(App::Shell &shell) const;

	std::unique_ptr<SpaceLayer> m_pLayer;
	Input::ActionId m_toggleMarket{};
	// The galaxy's systems' names, by their identifiers, for where a trader is.
	std::map<std::uint64_t, std::string> m_systemNames;

	Ui::DataModel *m_pMarketModel{nullptr};
	Ui::DocumentHandle m_marketDocument;
	bool m_showMarket{false};
	// The screen's data, which the "market" model is bound to, the step it was made at, and what made
	// it: a click's answer, the lot, the ship and the language it was made for.
	std::unique_ptr<MarketScreen> m_pMarketScreen;
	std::optional<std::uint64_t> m_marketStep;
	std::int64_t m_lot{1};
	std::string m_tradeMessage;
	entt::entity m_marketShip{entt::null};
	std::string m_marketLanguage;
	bool m_marketDirty{true};
	// The ship's trades answered, filled or refused, and how many there were, and the UI's sounds,
	// when the smoke run's trade was sent.
	std::uint64_t m_tradeAnswers{0};
	std::uint64_t m_smokeTradeAnswers{0};
	std::uint64_t m_smokeUiSounds{0};
};

} // namespace Jettison::Sample
