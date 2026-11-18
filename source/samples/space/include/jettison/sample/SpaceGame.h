#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <jettison/app/Game.h>
#include <jettison/input/Actions.h>
#include <jettison/ui/UiSystem.h>

namespace Jettison::Sample
{
struct MarketScreen;

// The space-trading sample (docs/sample-game.md) as a game composed into the shell: goods and their
// markets at the galaxy's stations, the traders that run between them, the player's fleet with its
// shipyard, the ships parked at the stations, and the market screen. Jetsam and the render test run
// the shell with it; Flotsam runs it with none. It is the engine's test coverage of a game, not a
// game's: Manifest's rules, prices and content are Manifest's.
class SpaceGame final : public App::Game
{
  public:
	SpaceGame();

	~SpaceGame() override;

	SpaceGame(const SpaceGame &) = delete;

	SpaceGame &operator=(const SpaceGame &) = delete;

	void Register(const App::ShellWorld &world, Input::InputSystem &input) override;

	void LoadDefinitions(Sim::DefinitionRegistry &definitions) override;

	void Define(const App::ShellWorld &world, const Sim::DefinitionRegistry &definitions) override;

	// The traders, by their behaviour tree and the trade decision, unless it asks for an input neither
	// a trader nor a script answers.
	void OpenGalaxy(
	    const App::ShellWorld &world, std::span<const Celestial::StarSystem> systems,
	    const Sim::DefinitionRegistry &definitions, Script::ScriptHost &scripts) override;

	// The sample's ships parked on two or three pads of each station, leaving at least two free, so a
	// ship flown to one finds some taken: heavy transports on three of Highport's twelve L pads, and
	// at Corva two transports on M pads and a fighter on an S.
	void StationOpened(
	    Sim::SimWorld &world, Dock::Docks &docks, entt::entity station,
	    std::string_view body) override;

	// The markets, bound to their stations, and the traders on their pads.
	void StationsOpened(
	    const App::ShellWorld &world, const Sim::DefinitionRegistry &definitions) override;

	// A small hauler's hold, fitted to trade once the player leaves its helm, and the player's credits,
	// enough to fill most of the hold with what the station has to spare.
	void FitShip(entt::registry &world, entt::entity ship, const Sim::Controller &player) override;

	// Purses kept on ships before they were their owners', and a ship saved before the helm (13d).
	void Loaded(entt::registry &world, entt::entity ship, const Sim::Controller &player) override;

	// traders, buy, trade and market.
	void AddCommands(DevTools::Console &console, App::Shell &shell) override;

	void OpenUi(Ui::UiSystem &ui, App::Shell &shell) override;

	// M shows and hides the market screen; Escape hides it.
	void PollInput(const Input::InputSystem &input, App::Shell &shell) override;

	bool Back(App::Shell &shell) override;

	// The market screen made again from the snapshot, when the step or what the shell shows moved.
	void UpdateUi(App::Shell &shell) override;

	// A trade's answer, shown on the screen, and a ship bought, followed.
	void TakeEvent(const Sim::SimEvent &event, App::Shell &shell) override;

	// A market by its station, and a good by its name.
	std::optional<std::string> NameEntity(
	    const Sim::Snapshot &snapshot, entt::entity entity) const override;

	std::optional<std::string> NameIdentifier(
	    std::string_view definition, const std::string &identifier,
	    const Sim::DefinitionRegistry &definitions, const Ui::StringTable &strings) const override;

	// Trading a good or choosing a run, and the crates in the hold.
	std::string DescribeShip(
	    const Sim::Snapshot &snapshot, entt::entity ship, bool atHelm) const override;

	// The market screen shown, checked against the snapshot's market, and its first Buy pressed; then
	// its answer, shown and heard.
	std::optional<bool> SmokeTrade(App::Shell &shell) override;

	bool SmokeTrading(App::Shell &shell) override;

	bool SmokeTraded(App::Shell &shell) override;

  private:
	// The market the screen shows: the one at the ship's station, else the first in the system shown,
	// else none.
	static entt::entity ShownMarket(App::Shell &shell);

	void ShowMarket(App::Shell &shell, bool shown);

	// A Trade the next step applies, from the screen's buttons: crates bought, or sold when negative.
	static void SendTrade(App::Shell &shell, std::string_view good, std::int64_t crates);

	void LogTraders(App::Shell &shell) const;

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
