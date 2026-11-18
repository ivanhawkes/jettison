#include <jettison/sample/SpaceGame.h>

#include <array>
#include <charconv>
#include <format>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/UtilityAi.h>
#include <jettison/app/Stations.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/core/Log.h>
#include <jettison/devtools/Console.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/script/ScriptHost.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include "Fleet.h"
#include "MarketScreen.h"
#include "Traders.h"
#include "Trading.h"

namespace Jettison::Sample
{
namespace
{
constexpr Core::Category kSample{"sample"};
constexpr std::string_view kGood = "Shell.Good";

// The player's first ship as the sample starts it: a small hauler's hold, and the credits to fill most
// of it with what the station it starts at has to spare.
constexpr std::int64_t kShipHold = 20;
constexpr std::int64_t kStartingCredits = 250000;

// The sample's parked ships: on two or three pads of each station, leaving at least two free.
struct Parked
{
	std::string_view station;
	std::uint32_t pad;
	std::string_view hull;
	float mass;
};
constexpr std::array<Parked, 6> kParked{{
    {"Ostra Highport", 2, "heavy-transport", 1.2e7f},
    {"Ostra Highport", 7, "heavy-transport", 1.2e7f},
    {"Ostra Highport", 11, "heavy-transport", 1.2e7f},
    {"Corva Refinery", 2, "transport", 1.5e6f},
    {"Corva Refinery", 3, "transport", 1.5e6f},
    {"Corva Refinery", 6, "fighter", 3.0e4f},
}};
} // namespace


SpaceGame::SpaceGame() : m_pMarketScreen{std::make_unique<MarketScreen>()}
{
}


SpaceGame::~SpaceGame() = default;


void SpaceGame::Register(const App::ShellWorld &world, Input::InputSystem &input)
{
	RegisterTrading(world.world);
	ChargeTolls(world.gates);
	RegisterMarketScreenTypes();
	m_toggleMarket = input.DefineAction("toggle_market", Input::ActionType::Button);
	input.Bind("shell", m_toggleMarket, {Input::Keys::Character('m')});
}


void SpaceGame::LoadDefinitions(Sim::DefinitionRegistry &definitions)
{
	LoadTradeDefinitions(definitions);
	LoadTraderDefinitions(definitions);
	LoadShipyard(definitions);
}


void SpaceGame::Define(const App::ShellWorld &world, const Sim::DefinitionRegistry &definitions)
{
	// The fleet's command sells from the shipyard, of hulls the docks now know.
	RegisterFleet(world.world, world.docks, definitions);
}


void SpaceGame::OpenGalaxy(
    const App::ShellWorld &world, std::span<const Celestial::StarSystem> systems,
    const Sim::DefinitionRegistry &definitions, Script::ScriptHost &scripts)
{
	for (const Celestial::StarSystem &system : systems)
		m_systemNames.emplace(Sim::FrameId(system.name), system.name);
	// The traders' tasks read the systems; their trees name the tasks.
	const auto *decision = definitions.Find<Ai::DecisionDefinition>(kTradeDecision);
	if (!decision)
	{
		Core::Log(
		    Core::Severity::Warning, kSample, "No decision '{}' is defined; the galaxy has no traders",
		    kTradeDecision);
		return;
	}
	if (const std::optional<std::string> unanswered = UnansweredInput(*decision, &scripts))
	{
		Core::Log(
		    Core::Severity::Error, kSample,
		    "The decision '{}' asks for '{}', which neither a trader nor a script answers; the galaxy "
		    "has no traders",
		    kTradeDecision, *unanswered);
		return;
	}
	RegisterTraders(
	    world.world, world.behaviours, world.physics, world.lanes, {systems.begin(), systems.end()},
	    *decision, &scripts);
	world.behaviours.Define(definitions);
}


void SpaceGame::StationOpened(
    Sim::SimWorld &world, Dock::Docks &docks, entt::entity station, std::string_view body)
{
	entt::registry &registry = world.Registry();
	const std::uint64_t system = registry.get<Sim::InFrame>(station).frame;
	for (const Parked &parked : kParked)
	{
		if (parked.station != body || !docks.FindHull(parked.hull))
			continue;
		const entt::entity ship = registry.create();
		registry.emplace<Sim::Transform>(ship, registry.get<Sim::Transform>(station));
		registry.emplace<Sim::InFrame>(ship, system);
		registry.emplace<Physics::LocalFrame>(ship, std::string{body});
		App::MakeShip(registry, docks, ship, parked.hull, parked.mass, false);
		docks.Park(world, ship, registry.get<Dock::Station>(station).pads.at(parked.pad - 1));
	}
}


void SpaceGame::StationsOpened(const App::ShellWorld &world, const Sim::DefinitionRegistry &definitions)
{
	entt::registry &registry = world.world.Registry();
	if (!registry.view<Market>().empty())
		return;
	const std::vector<entt::entity> markets = OpenMarkets(world.world, definitions);
	for (const entt::entity entity : markets)
	{
		Market &market = registry.get<Market>(entity);
		market.stationEntity = App::FindStation(registry, market.system, market.station);
	}
	Core::Log(Core::Severity::Info, kSample, "{} markets open", markets.size());
	if (world.behaviours.HasTree(kTraderTree))
		Core::Log(
		    Core::Severity::Info, kSample, "{} traders open",
		    OpenTraders(world.world, world.docks).size());
}


void SpaceGame::FitShip(entt::registry &world, entt::entity ship, const Sim::Controller &player)
{
	world.emplace<Hold>(ship, Hold{kShipHold, {}});
	// Its behaviour trades with it once the player leaves its helm.
	FitForTrade(world, ship);
	// The player's credits are their own, not the ship's, and outlast it.
	if (FindOwner(world, player) == entt::null)
		MakeOwner(world, player, kStartingCredits);
}


void SpaceGame::Loaded(entt::registry &world, entt::entity ship, const Sim::Controller &player)
{
	// A save from before purses were their owners' holds them on the ships.
	if (const std::size_t moved = MovePursesToOwners(world); moved > 0)
		Core::Log(
		    Core::Severity::Info, kSample, "The save kept {} purses on ships, now their owners'",
		    moved);
	// One from before the helm (13d) has the player's ship flown by no one.
	if (GiveOldShipItsHelm(world, ship, player))
		Core::Log(Core::Severity::Info, kSample, "The save had no helm; the player is at its ship's");
	m_tradeMessage.clear();
	m_marketDirty = true;
}


void SpaceGame::AddCommands(DevTools::Console &console, App::Shell &shell)
{
	console.RegisterCommand(
	    "traders", "Says what each trader is doing, and what its runs have made",
	    [this, &shell](DevTools::Console &, std::span<const std::string>) { LogTraders(shell); });
	console.RegisterCommand(
	    "buy",
	    "Buys a ship at the bound station, where a ship of the fleet is docked, and takes its helm: "
	    "buy shuttle|transport",
	    [&shell](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    if (arguments.size() != 1)
			    throw std::invalid_argument{"takes the hull to buy"};
		    if (shell.Station() == entt::null)
		    {
			    Core::Log(Core::Severity::Warning, kSample, "No station to buy a ship at");
			    return;
		    }
		    shell.World().Commands().Push(BuyShip{shell.Player(), shell.Station(), arguments[0]});
	    });
	console.RegisterCommand(
	    "trade",
	    "Buys crates of a good at the market of the system shown, or sells them with a negative "
	    "count, as the market screen does: trade <good> <crates>",
	    [&shell](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    std::int64_t crates = 0;
		    const auto [end, error] =
		        arguments.size() == 2
		            ? std::from_chars(
		                  arguments[1].data(), arguments[1].data() + arguments[1].size(), crates)
		            : std::from_chars_result{nullptr, std::errc::invalid_argument};
		    if (error != std::errc{} || end != arguments[1].data() + arguments[1].size() || crates == 0)
		    {
			    Core::Log(Core::Severity::Warning, kSample, "trade takes a good and a count of crates");
			    return;
		    }
		    SendTrade(shell, arguments[0], crates);
	    });
	console.RegisterCommand(
	    "market", "Shows or hides the market screen: market [on|off]",
	    [this, &shell](DevTools::Console &, std::span<const std::string> arguments)
	    { ShowMarket(shell, arguments.empty() ? !m_showMarket : arguments[0] != "off"); });
}


void SpaceGame::OpenUi(Ui::UiSystem &ui, App::Shell &shell)
{
	Ui::DataModel &market = ui.CreateDataModel("market");
	m_pMarketModel = &market;
	market.Bind("screen", m_pMarketScreen.get());
	market.BindEvent(
	    "buy",
	    [&shell](std::span<const Ui::EventArgument> arguments)
	    {
		    SendTrade(
		        shell, arguments.empty() ? "" : Ui::AsText(arguments[0]),
		        arguments.size() < 2 ? 1 : Ui::AsInteger(arguments[1]));
	    });
	market.BindEvent(
	    "sell",
	    [&shell](std::span<const Ui::EventArgument> arguments)
	    {
		    SendTrade(
		        shell, arguments.empty() ? "" : Ui::AsText(arguments[0]),
		        -(arguments.size() < 2 ? 1 : Ui::AsInteger(arguments[1])));
	    });
	market.BindEvent(
	    "lot",
	    [this](std::span<const Ui::EventArgument> arguments)
	    {
		    m_lot = std::max<std::int64_t>(arguments.empty() ? 1 : Ui::AsInteger(arguments[0]), 1);
		    m_marketDirty = true;
	    });
	m_marketDocument = ui.LoadDocument("ui/market.rml");
	// The headless run shows it from its first frame, as the shell does its own screens.
	ShowMarket(shell, shell.SmokeTest());
}


void SpaceGame::PollInput(const Input::InputSystem &input, App::Shell &shell)
{
	const Ui::UiSystem *ui = shell.GameUi();
	if (input.WasPressedThisFrame(m_toggleMarket) && !(ui && ui->WantsKeyboard()))
		ShowMarket(shell, !m_showMarket);
}


bool SpaceGame::Back(App::Shell &shell)
{
	if (!m_showMarket)
		return false;
	ShowMarket(shell, false);
	return true;
}


void SpaceGame::UpdateUi(App::Shell &shell)
{
	Ui::UiSystem *ui = shell.GameUi();
	if (!ui || !m_showMarket)
		return;
	// Another ship followed, or the screens read in another language, and the screen is another's.
	if (shell.Ship() != m_marketShip)
	{
		m_marketShip = shell.Ship();
		m_tradeMessage.clear();
		m_marketDirty = true;
	}
	if (ui->Strings().Language() != m_marketLanguage)
	{
		m_marketLanguage = ui->Strings().Language();
		m_marketDirty = true;
	}
	// The screen is made again only when a step brought a new snapshot or the shell's side of it
	// moved, and the model told only when what it shows differs, so a still market costs nothing.
	const Sim::Snapshot &snapshot = shell.Snapshot();
	if (!m_marketDirty && m_marketStep == snapshot.step)
		return;
	MarketView view;
	view.market = ShownMarket(shell);
	view.ship = shell.Ship();
	view.system = shell.ShownSystem();
	view.docked = shell.Docked();
	view.lot = m_lot;
	view.message = m_tradeMessage;
	MarketScreen screen = BuildMarketScreen(snapshot, shell.Definitions(), ui->Strings(), view);
	if (screen != *m_pMarketScreen)
	{
		*m_pMarketScreen = std::move(screen);
		m_pMarketModel->Changed("screen");
	}
	m_marketStep = snapshot.step;
	m_marketDirty = false;
}


void SpaceGame::TakeEvent(const Sim::SimEvent &event, App::Shell &shell)
{
	const Sim::Controller player = shell.Player();
	if (const auto *bought = event.event.try_cast<ShipBought>(); bought && bought->buyer == player)
	{
		Core::Log(
		    Core::Severity::Info, kSample, "Bought a {} at {} for {} credits; {} credits left",
		    bought->hull, shell.Describe(bought->station), FormatCredits(bought->price),
		    FormatCredits(bought->credits));
		shell.AttachShip(bought->ship);
	}
	else if (const auto *notBought = event.event.try_cast<BuyRefused>();
	         notBought && notBought->sender == player)
		Core::Log(
		    Core::Severity::Warning, kSample, "No {} bought: {}", notBought->hull,
		    shell.Word(notBought->reason));
	else if (const auto *traded = event.event.try_cast<Traded>();
	         traded && traded->ship == shell.Ship() && shell.GameUi())
	{
		const Ui::StringTable &strings = shell.GameUi()->Strings();
		const std::int64_t crates = traded->crates < 0 ? -traded->crates : traded->crates;
		++m_tradeAnswers;
		m_tradeMessage = strings.Plural(
		    traded->crates > 0 ? "market.bought" : "market.sold", crates,
		    GoodName(shell.Definitions(), strings, traded->good), strings.Number(traded->price));
		Core::Log(
		    Core::Severity::Info, kSample, "{}; {} credits left", m_tradeMessage,
		    FormatCredits(traded->credits));
	}
	else if (const auto *refused = event.event.try_cast<TradeRefused>();
	         refused && refused->ship == shell.Ship() && shell.GameUi())
	{
		++m_tradeAnswers;
		m_tradeMessage =
		    shell.GameUi()->Strings().Format("market.refused", shell.Word(refused->reason));
		Core::Log(Core::Severity::Info, kSample, "{}", m_tradeMessage);
	}
	if (m_showMarket)
		m_marketDirty = true;
}


std::optional<std::string> SpaceGame::NameEntity(
    const Sim::Snapshot &snapshot, entt::entity entity) const
{
	if (const auto *market = snapshot.Get<Market>(entity))
		return market->station;
	return std::nullopt;
}


std::optional<std::string> SpaceGame::NameIdentifier(
    std::string_view definition, const std::string &identifier,
    const Sim::DefinitionRegistry &definitions, const Ui::StringTable &strings) const
{
	if (definition == kGood)
		return GoodName(definitions, strings, identifier);
	return std::nullopt;
}


std::string SpaceGame::DescribeShip(const Sim::Snapshot &snapshot, entt::entity ship, bool atHelm) const
{
	std::string flown = "at your helm";
	if (!atHelm)
	{
		const auto *trader = snapshot.Get<Trader>(ship);
		flown = trader && !trader->good.empty() ? std::format("trading {}", trader->good)
		                                        : std::string{"choosing a run"};
	}
	const auto *hold = snapshot.Get<Hold>(ship);
	return std::format("{}  {}/{} crates", flown, hold ? hold->Used() : 0, hold ? hold->capacity : 0);
}


std::optional<bool> SpaceGame::SmokeTrade(App::Shell &shell)
{
	Ui::UiSystem *ui = shell.GameUi();
	const entt::entity market = ShownMarket(shell);
	const auto *shown = shell.Snapshot().Get<Market>(market);
	if (!ui || !shown || shown->goods.empty() || shell.Ship() == entt::null)
		return std::nullopt;
	ShowMarket(shell, true);
	// Made from this snapshot and laid out, so what it shows is what the snapshot has.
	shell.UpdateGameUi();
	const std::string station = ui->ElementText(m_marketDocument, "station");
	const std::string stock = ui->ElementText(m_marketDocument, "stock-0");
	if (station != shown->station || stock != std::to_string(shown->goods.front().stock))
	{
		Core::Log(
		    Core::Severity::Error, kSample,
		    "The market screen shows '{}' with {} crates, and the snapshot has '{}' with {}", station,
		    stock, shown->station, shown->goods.front().stock);
		return false;
	}
	m_smokeTradeAnswers = m_tradeAnswers;
	m_smokeUiSounds = shell.UiSounds();
	return ui->Click(m_marketDocument, "buy-0");
}


bool SpaceGame::SmokeTrading(App::Shell & /*shell*/)
{
	return m_tradeAnswers == m_smokeTradeAnswers;
}


bool SpaceGame::SmokeTraded(App::Shell &shell)
{
	const std::string shown = shell.GameUi()->ElementText(m_marketDocument, "message");
	if (shown != m_tradeMessage)
		Core::Log(
		    Core::Severity::Error, kSample,
		    "The market screen says '{}', and the trade's answer was '{}'", shown, m_tradeMessage);
	// The answer is heard as well as shown: filled or refused, it plays on the UI bus.
	if (shell.UiSounds() == m_smokeUiSounds)
		Core::Log(
		    Core::Severity::Error, kSample, "The trade's answer '{}' played no sound", m_tradeMessage);
	return !m_tradeMessage.empty() && shown == m_tradeMessage && shell.UiSounds() > m_smokeUiSounds;
}


entt::entity SpaceGame::ShownMarket(App::Shell &shell)
{
	const Sim::Snapshot &snapshot = shell.Snapshot();
	const auto markets = snapshot.Components<Market>();
	if (const entt::entity station = shell.Station(); station != entt::null)
		for (const auto &market : markets)
			if (market.value.stationEntity == station)
				return market.entity;
	if (const std::string system = shell.ShownSystem(); !system.empty())
	{
		const std::uint64_t shown = Sim::FrameId(system);
		for (const auto &market : markets)
			if (market.value.system == shown)
				return market.entity;
	}
	return entt::null;
}


void SpaceGame::ShowMarket(App::Shell &shell, bool shown)
{
	m_showMarket = shown;
	Ui::UiSystem *ui = shell.GameUi();
	if (!ui)
		return;
	if (shown)
	{
		m_marketDirty = true;
		ui->Show(m_marketDocument);
	}
	else
		ui->Hide(m_marketDocument);
}


void SpaceGame::SendTrade(App::Shell &shell, std::string_view good, std::int64_t crates)
{
	const entt::entity market = ShownMarket(shell);
	if (shell.Ship() == entt::null || market == entt::null || good.empty() || crates == 0)
		return;
	shell.World().Commands().Push(
	    Trade{shell.Player(), shell.Ship(), market, std::string{good}, crates});
}


void SpaceGame::LogTraders(App::Shell &shell) const
{
	// From the snapshot, as the market screen is: presentation sees the traders as they were shown.
	const Sim::Snapshot &snapshot = shell.Snapshot();
	const auto stationOf = [&](entt::entity market)
	{
		const Market *found = market == entt::null ? nullptr : snapshot.Get<Market>(market);
		return found ? found->station : std::string{"nowhere"};
	};
	const std::span<const Sim::ComponentSample<Trader>> traders = snapshot.Components<Trader>();
	if (traders.empty())
		Core::Log(Core::Severity::Info, kSample, "No traders");
	for (const auto &[entity, trader] : traders)
	{
		const auto *hold = snapshot.Get<Hold>(entity);
		const auto *purse = PurseOf(snapshot, entity);
		const Sim::TransformSample *sample = snapshot.FindSample(entity);
		std::string where{"on a lane"};
		if (sample && sample->frame != Sim::kNoFrame)
			if (const auto found = m_systemNames.find(sample->frame); found != m_systemNames.end())
				where = "in " + found->second;
		const std::string run =
		    trader.good.empty()
		        ? std::string{"choosing a run"}
		        : std::format(
		              "{} {} {}, bound for {} with {} jumps to go", trader.good,
		              trader.buyAt == entt::null ? "held, to sell at"
		                                         : "from " + stationOf(trader.buyAt) + " to",
		              stationOf(trader.sellAt), stationOf(trader.bound), trader.jumps.size());
		Core::Log(
		    Core::Severity::Info, kSample,
		    "Trader {:#x}, {}: {}; {} crates aboard, {} credits; {} runs made {}",
		    entt::to_integral(entity), where, run, hold ? hold->Used() : 0, purse ? purse->credits : 0,
		    trader.runs, trader.earned);
	}
}

} // namespace Jettison::Sample
