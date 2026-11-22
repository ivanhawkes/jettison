#include <jettison/app/Reasons.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

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

using namespace Jettison;

namespace
{
// The engine's string tables, and over them a table of the case's own with a reason of plural forms.
struct Words
{
	std::filesystem::path scratch{
	    std::filesystem::temp_directory_path() /
	    ("jettison-reasons-tests-" + std::to_string(getpid()))};
	Vfs::FileSystem assets;
	Sim::DefinitionRegistry definitions{Vfs::FileSystem{}};
	Ui::StringTable strings;


	Words()
	{
		std::filesystem::create_directories(scratch / "strings/en");
		std::ofstream{scratch / "strings/en/test.yaml"}
		    << "version: 1\nreason:\n  test:\n    crates:\n      one: '{0} crate'\n"
		       "      other: '{0} crates'\n";
		assets.AddMount({"engine", JETTISON_ASSET_SOURCE_DIR, false});
		assets.AddMount({"base", scratch, false});
		strings.Load(assets, "en");
	}


	~Words()
	{
		std::error_code ignored;
		std::filesystem::remove_all(scratch, ignored);
	}


	Words(const Words &) = delete;


	Words &operator=(const Words &) = delete;
};
} // namespace


TEST_CASE("Reasons: every reason of the engine's has its words in English and in German")
{
	Sim::RegisterTypes();
	Ai::RegisterTypes();
	Physics::RegisterTypes();
	Dock::RegisterTypes();
	Words words;
	for (const std::string language : {"en", "de"})
	{
		words.strings.Load(words.assets, language);
		for (const entt::meta_type reasons :
		     {entt::resolve<Dock::Reason>(), entt::resolve<Ai::Reason>(),
		      entt::resolve<Physics::Reason>()})
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
	Ai::RegisterTypes();
	Physics::RegisterTypes();
	Dock::RegisterTypes();
	world.ShowInSnapshots<Physics::OnRails>();
	entt::registry &registry = world.Registry();
	const entt::entity station = registry.create();
	registry.emplace<Physics::OnRails>(station, Physics::OnRails{"Ostra Highport", {}, {}});
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
	    word(Sim::MakeReason(Dock::Reason::OtherSystem, {Argument::Entity(nameless)})) ==
	    "the ship is not in something's system");
	CHECK(
	    word(
	        Sim::MakeReason(
	            Ai::Reason::NoLane,
	            {Argument::Identifier("Halcyon"), Argument::Identifier("Vesper Gate")})) ==
	    "no lane leaves Halcyon through Vesper Gate");
	CHECK(word({"reason.test.crates", {Argument::Integer(1)}}) == "1 crate");
	CHECK(word({"reason.test.crates", {Argument::Integer(1200)}}) == "1,200 crates");
	CHECK(words.strings.Problems() == 0);

	words.strings.Load(words.assets, "de");
	CHECK(
	    word(
	        Sim::MakeReason(
	            Dock::Reason::AlreadyHoldsPad, {Argument::Integer(1200), Argument::Entity(station)})) ==
	    "das Schiff hat bereits Landeplattform 1.200 an Ostra Highport");
	CHECK(words.strings.Problems() == 0);

	// A script's reason, which no table words yet, reads as its key and is counted.
	CHECK(word({"reason.script.smoke.too_late", {}}) == "reason.script.smoke.too_late");
	CHECK(words.strings.Problems() == 1);
}
