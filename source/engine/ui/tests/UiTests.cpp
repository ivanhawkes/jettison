#include <jettison/ui/UiSystem.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/render/Overlay.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison;

namespace
{
enum class Standing
{
	Lawful,
	Grey,
	Black,
};

struct Good
{
	std::string name;
	std::int64_t ask{0};
	std::uint32_t stock{0};
};

struct Venue
{
	std::string station;
	Standing standing{Standing::Lawful};
	std::int64_t credits{0};
	std::vector<Good> goods;
};

struct Opaque
{
	int value{0};
};

struct HoldsOpaque
{
	Opaque opaque;
};

void RegisterTypes()
{
	static const bool registered = []
	{
		Reflect::RegisterEnum<Standing>("UiTests.Standing")
		    .Value<Standing::Lawful>("Lawful")
		    .Value<Standing::Grey>("Grey")
		    .Value<Standing::Black>("Black");
		Reflect::RegisterType<Good>("UiTests.Good")
		    .Field<&Good::name>("name")
		    .Field<&Good::ask>("ask")
		    .Field<&Good::stock>("stock");
		Reflect::RegisterType<Venue>("UiTests.Venue")
		    .Field<&Venue::station>("station")
		    .Field<&Venue::standing>("standing")
		    .Field<&Venue::credits>("credits")
		    .Field<&Venue::goods>("goods");
		Reflect::RegisterType<HoldsOpaque>("UiTests.HoldsOpaque").Field<&HoldsOpaque::opaque>("opaque");
		return true;
	}();
	(void)registered;
}

// The shipped fonts below a base and a mod mount of the case's own, removed when it goes.
class Scratch
{
  public:
	Scratch()
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-ui-tests-" + std::to_string(getpid()))}
	{
		std::filesystem::remove_all(m_directory);
		std::filesystem::create_directories(m_directory / "base");
		std::filesystem::create_directories(m_directory / "mod");
		m_files.AddMount({"assets", JETTISON_ASSET_SOURCE_DIR, false});
		m_files.AddMount({"base", m_directory / "base", false});
		m_files.AddMount({"mod", m_directory / "mod", false});
	}
	~Scratch()
	{
		std::error_code error;
		std::filesystem::remove_all(m_directory, error);
	}
	Scratch(const Scratch &) = delete;
	Scratch &operator=(const Scratch &) = delete;

	void Put(const std::string &path, const std::string &contents) const
	{
		const auto file = m_directory / path;
		std::filesystem::create_directories(file.parent_path());
		std::ofstream{file, std::ios::binary | std::ios::trunc} << contents;
	}
	const Vfs::FileSystem &Files() const
	{
		return m_files;
	}

  private:
	std::filesystem::path m_directory;
	Vfs::FileSystem m_files;
};

constexpr const char *kMarketDocument = R"rml(<rml>
<head>
	<title>Market</title>
	<style>
		body { font-family: Droid Sans; font-size: 16px; width: 400px; height: 300px; }
		.row { display: block; height: 20px; }
	</style>
</head>
<body data-model="market">
	<p id="station">{{venue.station}}</p>
	<p id="standing">{{venue.standing}}</p>
	<p id="credits">{{venue.credits | thousands}}</p>
	<p id="count">{{venue.goods.size}} goods</p>
	<p id="none" data-if="venue.goods.size == 0">Nothing to trade</p>
	<div class="row" data-for="good, i : venue.goods">
		<span data-attr-id="'name-' + i">{{good.name}}</span>
		<span data-attr-id="'ask-' + i">{{good.ask}}</span>
		<button data-attr-id="'buy-' + i" data-event-click="buy(good.name, 10)">Buy</button>
	</div>
</body>
</rml>
)rml";

// The extent the overlay's vertices cover across, in pixels.
float DrawnWidth(const Render::Overlay &overlay)
{
	float left = std::numeric_limits<float>::max();
	float right = std::numeric_limits<float>::lowest();
	for (const Render::OverlayDraw &draw : overlay.Draws())
		for (std::uint32_t index = 0; index < draw.indexCount; ++index)
		{
			const auto &vertex = overlay.Vertices()
			                         [static_cast<std::size_t>(draw.vertexOffset) +
			                          overlay.Indices()[draw.firstIndex + index]];
			left = std::min(left, vertex.position.x + draw.translation.x);
			right = std::max(right, vertex.position.x + draw.translation.x);
		}
	return right - left;
}
} // namespace

TEST_CASE("Ui: a document shows what its data model is bound to, and reads it again once changed")
{
	RegisterTypes();
	Scratch scratch;
	scratch.Put("base/ui/market.rml", kMarketDocument);
	Ui::UiSystem ui{scratch.Files(), 640, 480};
	ui.LoadFont("fonts/DroidSans.ttf");

	Venue venue{"Ostra Highport", Standing::Grey, 1250000, {{"Copper", 12, 400}, {"Tin", 41, 90}}};
	Ui::DataModel &model = ui.CreateDataModel("market");
	model.Bind("venue", &venue);
	std::vector<std::string> bought;
	model.BindEvent(
	    "buy",
	    [&bought](std::span<const Ui::EventArgument> arguments)
	    {
		    REQUIRE(arguments.size() == 2);
		    bought.push_back(
		        Ui::AsText(arguments[0]) + " x" + std::to_string(Ui::AsInteger(arguments[1])));
	    });

	const Ui::DocumentHandle document = ui.LoadDocument("ui/market.rml");
	CHECK(ui.OpenDocuments() == 1);
	CHECK_FALSE(ui.IsShown(document));
	ui.Show(document);
	ui.Update(0.0);
	CHECK(ui.IsShown(document));

	CHECK(ui.ElementText(document, "station") == "Ostra Highport");
	CHECK(ui.ElementText(document, "standing") == "Grey");
	CHECK(ui.ElementText(document, "credits") == "1,250,000");
	CHECK(ui.ElementText(document, "name-1") == "Tin");
	CHECK(ui.ElementText(document, "ask-1") == "41");
	CHECK(ui.ElementText(document, "count") == "2 goods");

	// A value moved: nothing reads it again until the model is told.
	venue.goods[1].ask = 44;
	venue.goods.push_back({"Gold", 130000, 2});
	model.Changed("venue");
	ui.Update(0.1);
	CHECK(ui.ElementText(document, "ask-1") == "44");
	CHECK(ui.ElementText(document, "name-2") == "Gold");
	CHECK(ui.ElementText(document, "count") == "3 goods");

	CHECK(ui.Click(document, "buy-2"));
	ui.Update(0.2);
	CHECK(bought == std::vector<std::string>{"Gold x10"});
	CHECK_FALSE(ui.Click(document, "no-such-element"));

	CHECK(ui.Problems() == 0);
}

TEST_CASE("Ui: a document draws into the overlay, and its glyphs are textures the overlay holds")
{
	RegisterTypes();
	Scratch scratch;
	scratch.Put("base/ui/market.rml", kMarketDocument);
	Ui::UiSystem ui{scratch.Files(), 640, 480};
	ui.LoadFont("fonts/DroidSans.ttf");
	Venue venue{"Corva Refinery", Standing::Lawful, 0, {{"Copper", 12, 400}}};
	ui.CreateDataModel("market").Bind("venue", &venue);
	ui.CreateDataModel("market-unused");
	CHECK_THROWS_AS(ui.CreateDataModel("market"), std::logic_error);

	const Ui::DocumentHandle document = ui.LoadDocument("ui/market.rml");
	ui.Update(0.0);
	// Hidden, it draws nothing.
	CHECK(ui.Overlay().Draws().empty());

	ui.Show(document);
	ui.Update(0.0);
	CHECK_FALSE(ui.Overlay().Draws().empty());
	CHECK_FALSE(ui.Overlay().Textures().empty());
	const bool textured = std::ranges::any_of(
	    ui.Overlay().Draws(), [&ui](const Render::OverlayDraw &draw)
	    { return ui.Overlay().FindTexture(draw.texture) != nullptr; });
	CHECK(textured);

	// Each frame's draws replace the last's.
	const std::size_t draws = ui.Overlay().Draws().size();
	ui.Update(0.1);
	CHECK(ui.Overlay().Draws().size() == draws);

	ui.Close(document);
	ui.Update(0.2);
	CHECK(ui.OpenDocuments() == 0);
	CHECK(ui.Overlay().Draws().empty());
	CHECK(ui.ElementText(document, "station").empty());
	// A handle to a closed document names nothing, whatever reuses its slot.
	const Ui::DocumentHandle again = ui.LoadDocument("ui/market.rml");
	CHECK_FALSE(again == document);
	CHECK_FALSE(ui.IsShown(document));
}

TEST_CASE("Ui: a mod's mount over the base restyles a screen by shadowing its stylesheet")
{
	Scratch scratch;
	scratch.Put(
	    "base/ui/panel.rml", "<rml><head><link type=\"text/rcss\" href=\"panel.rcss\"/></head>"
	                         "<body><div id=\"panel\"/></body></rml>");
	scratch.Put(
	    "base/ui/panel.rcss",
	    "#panel { display: block; width: 100px; height: 50px; background-color: #336699; }");
	const auto drawnWidth = [&scratch]
	{
		Ui::UiSystem ui{scratch.Files(), 640, 480};
		const Ui::DocumentHandle document = ui.LoadDocument("ui/panel.rml");
		ui.Show(document);
		ui.Update(0.0);
		CHECK(ui.Problems() == 0);
		return DrawnWidth(ui.Overlay());
	};
	CHECK(drawnWidth() == doctest::Approx(100.0f));

	// RmlUi keeps a stylesheet for the run once it has read it, so a mod is mounted before the UI is
	// made, as the shell mounts its mounts.
	scratch.Put(
	    "mod/ui/panel.rcss",
	    "#panel { display: block; width: 300px; height: 50px; background-color: #993366; }");
	CHECK(drawnWidth() == doctest::Approx(300.0f));
}

TEST_CASE("Ui: the mouse over a shown document's element is the UI's, and elsewhere the scene's")
{
	Scratch scratch;
	scratch.Put(
	    "base/ui/panel.rml",
	    "<rml><head><style>#panel { display: block; position: absolute; left: 10px; top: 10px;"
	    " width: 100px; height: 100px; }</style></head><body><div id=\"panel\"/></body></rml>");
	Ui::UiSystem ui{scratch.Files(), 640, 480};
	const Ui::DocumentHandle document = ui.LoadDocument("ui/panel.rml");
	ui.Show(document);
	ui.Update(0.0);

	ui.MouseMove({50.0f, 50.0f}, {});
	CHECK(ui.WantsMouse());
	ui.MouseMove({400.0f, 400.0f}, {});
	CHECK_FALSE(ui.WantsMouse());
	// No element that takes text has focus, so keys are the game's.
	CHECK_FALSE(ui.WantsKeyboard());
	CHECK_FALSE(ui.Text(U'x'));

	ui.Hide(document);
	ui.Update(0.1);
	ui.MouseMove({50.0f, 50.0f}, {});
	CHECK_FALSE(ui.WantsMouse());
}

TEST_CASE("Ui: what cannot work is refused when it is set up, by name")
{
	RegisterTypes();
	Scratch scratch;
	Ui::UiSystem ui{scratch.Files(), 640, 480};
	// RmlUi is one instance a process.
	CHECK_THROWS_AS(Ui::UiSystem(scratch.Files(), 640, 480), std::logic_error);

	CHECK_THROWS_AS(ui.LoadDocument("ui/missing.rml"), std::runtime_error);
	CHECK_THROWS_AS(ui.LoadFont("fonts/missing.ttf"), std::runtime_error);

	Ui::DataModel &model = ui.CreateDataModel("checks");
	Venue venue;
	model.Bind("venue", &venue);
	CHECK_THROWS_AS(model.Bind("venue", &venue), std::logic_error);
	model.BindEvent("buy", [](std::span<const Ui::EventArgument>) {});
	CHECK_THROWS_AS(
	    model.BindEvent("buy", [](std::span<const Ui::EventArgument>) {}), std::logic_error);

	HoldsOpaque holds;
	CHECK_THROWS_WITH_AS(
	    model.Bind("holds", &holds),
	    "DataModel: 'holds.opaque' is of a type the UI cannot show (one Reflect has not registered)",
	    std::logic_error);
}

TEST_CASE("Ui: an event's arguments read as numbers and as text")
{
	CHECK(Ui::AsInteger(Ui::EventArgument{std::int64_t{40}}) == 40);
	CHECK(Ui::AsInteger(Ui::EventArgument{10.0}) == 10);
	CHECK(Ui::AsInteger(Ui::EventArgument{10.5}) == 0);
	CHECK(Ui::AsInteger(Ui::EventArgument{std::string{"ten"}}) == 0);
	CHECK(Ui::AsText(Ui::EventArgument{std::string{"ItemCopper"}}) == "ItemCopper");
	CHECK(Ui::AsText(Ui::EventArgument{std::int64_t{7}}) == "7");
	CHECK(Ui::AsText(Ui::EventArgument{true}) == "true");
}
