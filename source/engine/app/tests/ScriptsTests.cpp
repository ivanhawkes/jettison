#include "../src/Mounts.h"
#include "../src/Traders.h"
#include "../src/Trading.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <jettison/ai/Types.h>
#include <jettison/ai/UtilityAi.h>
#include <jettison/core/Log.h>
#include <jettison/dock/Definitions.h>
#include <jettison/script/ScriptHost.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison;

namespace
{
std::string ReadText(const std::filesystem::path &path)
{
	std::ifstream file{path, std::ios::binary};
	std::ostringstream text;
	text << file.rdbuf();
	return text.str();
}

// A user's data directory with the smoke mod in its mods, unpacked, as source/test stages it.
struct ModdedUser
{
	std::filesystem::path root{std::filesystem::path{JETTISON_APP_TEST_OUTPUT} / "scripts-user"};

	ModdedUser()
	{
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root / "mods");
		std::filesystem::copy(
		    JETTISON_SMOKE_MOD_DIR, root / "mods/smoke", std::filesystem::copy_options::recursive);
	}
	~ModdedUser()
	{
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}
	ModdedUser(const ModdedUser &) = delete;
	ModdedUser &operator=(const ModdedUser &) = delete;
};
} // namespace

TEST_CASE("Scripts: the shell's Rust bindings, which its scripts are built against, are this build's")
{
	Sim::SimWorld world{1.0 / 60.0, 1};
	App::RegisterTrading(world);
	Ai::RegisterTypes();
	Dock::RegisterTypes();
	const Vfs::FileSystem files;
	Script::ScriptHost host{files};
	host.SendCommandsTo(world);

	const std::string module = host.WriteRustModule();
	CHECK(module.find("pub struct Shell_Traded {") != std::string::npos);
	CHECK(module.find("pub struct Dock_Landed {") != std::string::npos);
	CHECK(module.find("#[link_name = \"send:Shell.Trade\"]") != std::string::npos);
	CHECK(
	    host.WriteHeader().find("jettison_send_Shell_Trade(const Shell_Trade *command)") !=
	    std::string::npos);

	const std::filesystem::path checked =
	    std::filesystem::path{JETTISON_TEST_SCRIPT_DIR} / "jettison_script.rs";
	if (ReadText(checked) != module)
	{
		const std::filesystem::path written =
		    std::filesystem::path{JETTISON_APP_TEST_OUTPUT} / "jettison_script.rs";
		std::ofstream{written, std::ios::binary} << module;
		FAIL_CHECK(
		    "source/test/scripts/jettison_script.rs is not what this build's types make (the console's "
		    "script_bindings writes it); what they make is at ",
		    written.string());
	}
}

TEST_CASE(
    "Scripts: a script mod in the user's mods hears the trades filled and answers the traders' input")
{
	const ModdedUser user;
	Vfs::FileSystem files;
	App::MountAssets(files, JETTISON_ASSET_SOURCE_DIR, user.root);
	Sim::SimWorld world{1.0 / 60.0, 1};
	App::RegisterTrading(world);
	Sim::DefinitionRegistry definitions{files};
	App::LoadTradeDefinitions(definitions);
	App::LoadTraderDefinitions(definitions);
	Script::LoadDefinitions(definitions);

	// The mod's patch weighs a run by the script's input, which a trader does not answer.
	const auto *decision = definitions.Find<Ai::DecisionDefinition>(App::kTradeDecision);
	REQUIRE(decision != nullptr);
	CHECK(decision->actions[0].considerations.back().input == "crowding");
	CHECK(App::UnansweredInput(*decision, nullptr) == "crowding");

	Script::ScriptHost host{files};
	host.SendCommandsTo(world);
	REQUIRE(host.LoadAll(definitions) == 1);
	CHECK_FALSE(App::UnansweredInput(*decision, &host).has_value());

	std::vector<std::string> said;
	const Core::LogCallbackId listening = Core::AddLogCallback(
	    [&said](const Core::LogRecord &record)
	    {
		    if (record.category == "script")
			    said.emplace_back(record.message);
	    });
	host.Dispatch(
	    Sim::SimEvent{
	        1, entt::meta_any{App::Traded{entt::entity{3}, entt::entity{4}, "beer", 3, 290, 1000}}});
	host.Dispatch(
	    Sim::SimEvent{
	        2, entt::meta_any{App::Traded{entt::entity{3}, entt::entity{5}, "beer", -3, 410, 1360}}});
	Core::RemoveLogCallback(listening);
	CHECK(
	    said == std::vector<std::string>{
	                "smoke: heard trade 1: bought 3 crate(s) of beer at 290 credits",
	                "smoke: heard trade 2: sold 3 crate(s) of beer at 410 credits"});

	const Script::Inputs inputs =
	    host.WithScripts([](std::string_view input) { return input == "rivals" ? 1.0 : 0.0; });
	CHECK(inputs("crowding") == 0.5);
	CHECK(inputs("rivals") == 1.0);
}

TEST_CASE("Scripts: a script's keys and the mods enabled go into a save and come out of a load")
{
	const ModdedUser user;
	Vfs::FileSystem files;
	const std::vector<Sim::ModVersion> mods =
	    App::MountAssets(files, JETTISON_ASSET_SOURCE_DIR, user.root);
	CHECK(mods == std::vector<Sim::ModVersion>{{"smoke", "1.0.0"}});
	Sim::SimWorld world{1.0 / 60.0, 1};
	App::RegisterTrading(world);
	Sim::DefinitionRegistry definitions{files};
	App::LoadTradeDefinitions(definitions);
	App::LoadTraderDefinitions(definitions);
	Script::LoadDefinitions(definitions);

	std::vector<std::string> said;
	const Core::LogCallbackId listening = Core::AddLogCallback(
	    [&said](const Core::LogRecord &record)
	    {
		    if (record.category == "script" && record.message.starts_with("smoke:"))
			    said.emplace_back(record.message);
	    });
	const auto trade = [](std::uint64_t step)
	{
		return Sim::SimEvent{
		    step, entt::meta_any{App::Traded{entt::entity{3}, entt::entity{4}, "beer", 2, 300, 600}}};
	};

	// The shell's save: the world, the script's keys in their section, and the mods.
	std::vector<std::byte> bytes;
	{
		Script::ScriptHost host{files};
		host.SendCommandsTo(world);
		REQUIRE(host.LoadAll(definitions) == 1);
		host.Dispatch(trade(1));
		host.Dispatch(trade(2));
		Sim::SaveGame save = Sim::SaveGame::Of(world, "Two trades");
		save.info.mods = mods;
		save.AddSection(std::string{Script::kStoreSection}, Script::WriteStore(host.Store()));
		bytes = save.Write();
		// It counts on past the save.
		host.Dispatch(trade(3));
	}

	// A new run's load: the count is the save's, though the script's instance never heard a trade.
	const Sim::SaveGame loaded = Sim::SaveGame::Read(bytes);
	Script::ScriptHost host{files};
	host.SendCommandsTo(world);
	REQUIRE(host.LoadAll(definitions) == 1);
	const Sim::SaveSection *section = loaded.FindSection(Script::kStoreSection);
	REQUIRE(section != nullptr);
	host.SetStore(Script::ReadStore(section->data));
	host.Dispatch(trade(4));
	Core::RemoveLogCallback(listening);
	REQUIRE(said.size() == 4);
	CHECK(said[2] == "smoke: heard trade 3: bought 2 crate(s) of beer at 300 credits");
	CHECK(said[3] == "smoke: heard trade 3: bought 2 crate(s) of beer at 300 credits");

	// The mods it was made with, against the same, none and others.
	CHECK(loaded.info.mods == mods);
	CHECK(Sim::ModDifferences(loaded.info.mods, mods).empty());
	CHECK(
	    Sim::ModDifferences(loaded.info.mods, {}) ==
	    std::vector<std::string>{"'smoke' 1.0.0 was enabled and is not"});
	const std::vector<Sim::ModVersion> others{{"smoke", "1.1.0"}, {"wine", ""}};
	CHECK(
	    Sim::ModDifferences(loaded.info.mods, others) ==
	    std::vector<std::string>{"'smoke' was 1.0.0 and is 1.1.0", "'wine' is enabled and was not"});
}
