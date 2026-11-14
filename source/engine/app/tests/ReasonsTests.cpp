#include "Reasons.h"
#include "Trading.h"

#include <jettison/ai/Types.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Reason.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/ui/StringTable.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <format>
#include <string>

using namespace Jettison;

namespace
{
struct Words
{
	Vfs::FileSystem assets;
	Sim::DefinitionRegistry definitions{Vfs::FileSystem{}};
	Ui::StringTable strings;

	Words()
	{
		assets.AddMount({"base", JETTISON_ASSET_SOURCE_DIR, false});
		strings.Load(assets, "en");
	}
};
} // namespace

TEST_CASE("Reasons: every reason the step gives has its words in English and in German")
{
	Sim::SimWorld world{1.0 / 60.0, 1};
	App::RegisterTrading(world);
	Ai::RegisterTypes();
	Dock::RegisterTypes();
	Words words;
	for (const std::string language : {"en", "de"})
	{
		words.strings.Load(words.assets, language);
		for (const entt::meta_type reasons :
		     {entt::resolve<Dock::Reason>(), entt::resolve<Ai::Reason>(),
		      entt::resolve<Physics::Reason>(), entt::resolve<App::TradeReason>()})
			for (const std::string &key : Sim::ReasonKeys(reasons))
			{
				INFO(language, " ", key);
				CHECK((words.strings.Find(key) || words.strings.Find(key + ".other")));
			}
		CHECK(words.strings.Problems() == 0);
	}
}

TEST_CASE("Reasons: a reason's arguments are worded, its entities by name and its count by form")
{
	Sim::SimWorld world{1.0 / 60.0, 1};
	Sim::RegisterTypes();
	world.ShowInSnapshots<Physics::OnRails>();
	App::RegisterTrading(world);
	Dock::RegisterTypes();
	entt::registry &registry = world.Registry();
	const entt::entity station = registry.create();
	registry.emplace<Physics::OnRails>(station, Physics::OnRails{"Ostra Highport", {}, {}});
	const entt::entity market = registry.create();
	registry.emplace<App::Market>(market, App::Market{"Corva Refinery", 0, entt::null, 150, {}});
	const entt::entity nameless = registry.create();
	const Sim::Snapshot snapshot = Sim::Capture(world);
	Words words;
	const auto word = [&](const Sim::Reason &reason)
	{ return App::WordReason(reason, snapshot, words.definitions, words.strings); };
	using Argument = Sim::ReasonArgument;

	CHECK(
	    word(
	        Sim::MakeReason(
	            Dock::Reason::AlreadyHoldsPad, {Argument::Integer(12), Argument::Entity(station)})) ==
	    "the ship already holds pad 12 at Ostra Highport");
	CHECK(
	    word(
	        Sim::MakeReason(
	            App::TradeReason::ShortOfStock, {Argument::Entity(market), Argument::Integer(1)})) ==
	    "Corva Refinery has 1 crate");
	CHECK(
	    word(Sim::MakeReason(App::TradeReason::TooDear, {Argument::Integer(1200)})) ==
	    "1,200 crates cost more than the purse holds");
	CHECK(
	    word(Sim::MakeReason(Dock::Reason::OtherSystem, {Argument::Entity(nameless)})) ==
	    "the ship is not in something's system");
	CHECK(
	    word(
	        Sim::MakeReason(
	            Ai::Reason::NoLane,
	            {Argument::Identifier("Halcyon"), Argument::Identifier("Vesper Gate")})) ==
	    "no lane leaves Halcyon through Vesper Gate");
	CHECK(words.strings.Problems() == 0);

	words.strings.Load(words.assets, "de");
	CHECK(
	    word(Sim::MakeReason(App::TradeReason::TooDear, {Argument::Integer(1)})) ==
	    "1 Kiste kostet mehr als das Guthaben");
	CHECK(
	    word(
	        Sim::MakeReason(
	            App::TradeReason::NotDealt,
	            {Argument::Entity(market), Argument::Identifier("beer", "Shell.Good")})) ==
	    "Corva Refinery handelt nicht mit Bier");
	CHECK(words.strings.Problems() == 0);

	// A script's reason, which no table words yet, reads as its key and is counted.
	CHECK(word({"reason.script.smoke.too_late", {}}) == "reason.script.smoke.too_late");
	CHECK(words.strings.Problems() == 1);
}
