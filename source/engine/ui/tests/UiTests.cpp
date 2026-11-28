#include <jettison/ui/UiSystem.h>

#include <jettison/jobs/IoWorker.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/render/Overlay.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
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

// The engine's fonts below a base and a mod mount of the case's own,
// removed when it goes.
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


	void Remove(const std::string &path) const
	{
		std::filesystem::remove(m_directory / path);
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


// The extent the overlay's vertices cover across, in pixels: of every draw, or of the draws of a
// texture that many texels wide; 0 for none.
float DrawnWidth(const Render::Overlay &overlay, std::optional<std::uint32_t> texels = std::nullopt)
{
	float left = std::numeric_limits<float>::max();
	float right = std::numeric_limits<float>::lowest();
	for (const Render::OverlayDraw &draw : overlay.Draws())
	{
		const Render::OverlayTexture *pTexture = overlay.FindTexture(draw.texture);
		if (texels && (pTexture == nullptr || pTexture->width != *texels))
			continue;
		for (std::uint32_t index = 0; index < draw.indexCount; ++index)
		{
			const auto &vertex = overlay.Vertices()
			                         [static_cast<std::size_t>(draw.vertexOffset) +
			                          overlay.Indices()[draw.firstIndex + index]];
			left = std::min(left, vertex.position.x + draw.translation.x);
			right = std::max(right, vertex.position.x + draw.translation.x);
		}
	}
	return right < left ? 0.0f : right - left;
}


// A binary PPM of one grey, which stb_image reads: an image file the suite writes itself.
std::string Ppm(int width, int height)
{
	return "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n" +
	       std::string(static_cast<std::size_t>(width * height * 3), '\x80');
}

constexpr const char *kGalleryDocument =
    "<rml><head><style>img { display: block; } #wide { width: 64px; height: 32px; }"
    " #framed { display: block; width: 40px; height: 20px; decorator: image(frame.ppm); }"
    "</style></head><body><img id=\"wide\" src=\"wide.ppm\"/><img id=\"tall\" src=\"tall.ppm\"/>"
    "<div id=\"framed\"/></body></rml>";


// Big-endian words, as TrueType's tables are written.
class Words
{
  public:
	void U16(std::uint32_t value)
	{
		m_bytes.push_back(static_cast<char>((value >> 8) & 0xff));
		m_bytes.push_back(static_cast<char>(value & 0xff));
	}

	void U32(std::uint32_t value)
	{
		U16(value >> 16);
		U16(value & 0xffff);
	}

	void Pad()
	{
		while (m_bytes.size() % 4 != 0)
			m_bytes.push_back('\0');
	}

	std::string &Bytes()
	{
		return m_bytes;
	}

  private:
	std::string m_bytes;
};


// A TrueType font of the suite's own, written here so it is generated and carries no one's licence:
// the family "Fixture", whose one glyph, a square 800 units wide of a 1000-unit em, is mapped from 日
// (U+65E5), which none of the engine's fonts has.
std::string FixtureFont()
{
	constexpr std::uint32_t kDay = 0x65e5;
	std::vector<std::pair<std::string, std::string>> tables;
	const auto table = [&tables](std::string tag, auto &&write)
	{
		Words words;
		write(words);
		tables.emplace_back(std::move(tag), std::move(words.Bytes()));
	};
	const auto negative = [](int value) { return static_cast<std::uint32_t>(value) & 0xffff; };

	table(
	    "cmap",
	    [&](Words &w)
	    {
		    w.U16(0), w.U16(1), w.U16(3), w.U16(1), w.U32(12); // one subtable, Windows Unicode
		    w.U16(4), w.U16(32), w.U16(0), w.U16(4), w.U16(4), w.U16(1), w.U16(0); // format 4, 2 runs
		    w.U16(kDay), w.U16(0xffff), w.U16(0);                                  // ends, pad
		    w.U16(kDay), w.U16(0xffff);                                            // starts
		    w.U16(negative(1 - static_cast<int>(kDay))), w.U16(1);                 // deltas: glyph 1
		    w.U16(0), w.U16(0);                                                    // range offsets
	    });
	table(
	    "glyf",
	    [&](Words &w)
	    {
		    w.U16(1), w.U16(100), w.U16(0), w.U16(900), w.U16(800); // one contour and its bounds
		    w.U16(3), w.U16(0);                                     // its last point, no instructions
		    w.U16(0x0101), w.U16(0x0101);                           // four points on the curve
		    w.U16(100), w.U16(0), w.U16(800), w.U16(0);             // x, as deltas
		    w.U16(0), w.U16(800), w.U16(0), w.U16(negative(-800));  // y
		    w.U16(0);                                               // to an even length
	    });
	table(
	    "head",
	    [&](Words &w)
	    {
		    w.U32(0x00010000), w.U32(0x00010000), w.U32(0), w.U32(0x5f0f3cf5);
		    w.U16(0x000b), w.U16(1000);                       // flags, units per em
		    w.U32(0), w.U32(0), w.U32(0), w.U32(0);           // created, modified
		    w.U16(0), w.U16(0), w.U16(1000), w.U16(800);      // bounds
		    w.U16(0), w.U16(8), w.U16(2), w.U16(0), w.U16(0); // style, smallest size, hint, short loca
	    });
	table(
	    "hhea",
	    [&](Words &w)
	    {
		    w.U32(0x00010000), w.U16(800), w.U16(negative(-200)), w.U16(0), w.U16(1000);
		    w.U16(0), w.U16(0), w.U16(1000), w.U16(1), w.U16(0), w.U16(0);
		    w.U16(0), w.U16(0), w.U16(0), w.U16(0), w.U16(0), w.U16(2); // two metrics
	    });
	table("hmtx", [&](Words &w) { w.U16(500), w.U16(0), w.U16(1000), w.U16(100); });
	table("loca", [&](Words &w) { w.U16(0), w.U16(0), w.U16(18); }); // halved offsets
	table(
	    "maxp",
	    [&](Words &w)
	    {
		    w.U32(0x00010000), w.U16(2), w.U16(4), w.U16(1), w.U16(0), w.U16(0), w.U16(2);
		    for (int field = 0; field < 8; ++field)
			    w.U16(0);
	    });
	table(
	    "name",
	    [&](Words &w)
	    {
		    const std::string family = "Fixture";
		    const std::string style = "Regular";
		    const auto length = [](const std::string &text)
		    { return static_cast<std::uint32_t>(text.size() * 2); };
		    w.U16(0), w.U16(2), w.U16(30);
		    w.U16(3), w.U16(1), w.U16(0x409), w.U16(1), w.U16(length(family)), w.U16(0);
		    w.U16(3), w.U16(1), w.U16(0x409), w.U16(2), w.U16(length(style)), w.U16(length(family));
		    for (const char letter : family + style)
			    w.U16(static_cast<unsigned char>(letter));
	    });

	Words font;
	const auto count = static_cast<std::uint32_t>(tables.size());
	font.U32(0x00010000), font.U16(count), font.U16(128), font.U16(3), font.U16(count * 16 - 128);
	std::uint32_t offset = 12 + 16 * count;
	for (auto &[tag, bytes] : tables)
	{
		Words padded;
		padded.Bytes() = bytes;
		padded.Pad();
		bytes = padded.Bytes();
		std::uint32_t sum = 0;
		for (std::size_t at = 0; at < bytes.size(); at += 4)
			for (std::size_t byte = 0; byte < 4; ++byte)
				sum += static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + byte]))
				       << (24 - 8 * byte);
		font.Bytes() += tag;
		font.U32(sum), font.U32(offset), font.U32(static_cast<std::uint32_t>(bytes.size()));
		offset += static_cast<std::uint32_t>(bytes.size());
	}
	for (const auto &[tag, bytes] : tables)
		font.Bytes() += bytes;
	return font.Bytes();
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


TEST_CASE("Ui: a game's face under fonts/ draws what the engine's lack, and what none has is logged")
{
	Scratch scratch;
	scratch.Put("base/fonts/fixture.ttf", FixtureFont());
	scratch.Put("base/fonts/LICENSE.txt", "Not a face, so not read as one.");
	scratch.Put(
	    "base/ui/day.rml",
	    "<rml><head><style>body { font-family: Droid Sans; font-size: 20px; }</style></head>"
	    "<body><p id=\"day\">日日</p></body></rml>");
	std::size_t problems = 0;
	const auto drawnWidth = [&scratch, &problems](bool gameFonts)
	{
		// The engine's fallback face first, as the shell loads it, then the game's after it.
		Ui::UiSystem ui{scratch.Files(), 640, 480};
		ui.LoadFont("fonts/DroidSans.ttf", true);

		// The engine's own fonts, in the bottom mount, are not read again.
		if (gameFonts)
			CHECK(ui.LoadFallbackFonts("fonts", 1) == 1);
		// Read twice, so its character is met twice.
		ui.LoadDocument("ui/day.rml");
		const Ui::DocumentHandle document = ui.LoadDocument("ui/day.rml");
		ui.Show(document);
		ui.Update(0.0);
		problems = ui.Problems();
		return DrawnWidth(ui.Overlay());
	};

	// Two of the fixture's squares, 16 px each at 20 px and an advance of 20 px apart: 36 px across.
	const float drawn = drawnWidth(true);
	CHECK(problems == 0);
	CHECK(drawn == doctest::Approx(36.0f).epsilon(0.1));

	// Without it the two draw as Droid Sans's replacement, and the character is one problem, logged
	// once however often a document has it.
	CHECK(drawnWidth(false) != doctest::Approx(drawn));
	CHECK(problems == 1);
}


TEST_CASE("Ui: the theme set picks the base stylesheet's @media block of its name, Mocha with none")
{
	Scratch scratch;
	scratch.Put(
	    "base/ui/themed.rml", "<rml><head><link type=\"text/rcss\" href=\"base.rcss\"/></head>"
	                          "<body><p>Corva</p></body></rml>");
	Ui::UiSystem ui{scratch.Files(), 640, 480};
	ui.LoadFont("fonts/Roboto-Medium.ttf");
	const Ui::DocumentHandle document = ui.LoadDocument("ui/themed.rml");
	ui.Show(document);
	double time = 0.0;
	const auto drawnIn = [&ui, &time](const std::array<std::uint8_t, 4> &colour)
	{
		ui.Update(time += 0.1);
		return std::ranges::any_of(
		    ui.Overlay().Vertices(),
		    [&colour](const Render::OverlayVertex &vertex) { return vertex.color == colour; });
	};

	// The text's role, Foreground: Mocha's #cdd6f4, Latte's #4c4f69.
	const std::array<std::uint8_t, 4> mocha{0xcd, 0xd6, 0xf4, 0xff};
	const std::array<std::uint8_t, 4> latte{0x4c, 0x4f, 0x69, 0xff};
	CHECK(drawnIn(mocha));
	ui.SetTheme("latte");
	CHECK(drawnIn(latte));
	CHECK_FALSE(drawnIn(mocha));

	// Mocha has no block of its own: its name, or none, is the plain rules.
	ui.SetTheme("mocha");
	CHECK(drawnIn(mocha));
	CHECK_FALSE(drawnIn(latte));
	ui.SetTheme("latte");
	ui.SetTheme("");
	CHECK(drawnIn(mocha));
	CHECK(ui.Problems() == 0);
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


TEST_CASE("Ui: a mod's patch.rcss is appended to the stylesheet below it, and its rules win")
{
	Scratch scratch;
	scratch.Put(
	    "base/ui/panel.rml", "<rml><head><link type=\"text/rcss\" href=\"panel.rcss\"/></head>"
	                         "<body><div id=\"panel\"/></body></rml>");
	scratch.Put(
	    "base/ui/panel.rcss",
	    "#panel { display: block; width: 100px; height: 50px; background-color: #336699; }");

	// The patch comes after the base's rule, so its width wins.
	scratch.Put("mod/ui/panel.patch.rcss", "#panel { width: 200px; }");
	const auto drawnWidth = [&scratch]
	{
		Ui::UiSystem ui{scratch.Files(), 640, 480};
		const Ui::DocumentHandle document = ui.LoadDocument("ui/panel.rml");
		ui.Show(document);
		ui.Update(0.0);
		CHECK(ui.Problems() == 0);
		return DrawnWidth(ui.Overlay());
	};
	CHECK(drawnWidth() == doctest::Approx(200.0f));
}


TEST_CASE("Ui: a stylesheet changed on the disk restyles the document that links it, read again")
{
	Scratch scratch;
	scratch.Put(
	    "base/ui/panel.rml", "<rml><head><link type=\"text/rcss\" href=\"panel.rcss\"/></head>"
	                         "<body><div id=\"panel\"/></body></rml>");
	scratch.Put(
	    "base/ui/panel.rcss",
	    "#panel { display: block; width: 100px; height: 50px; background-color: #336699; }");
	Ui::UiSystem ui{scratch.Files(), 640, 480};
	const Ui::DocumentHandle document = ui.LoadDocument("ui/panel.rml");
	ui.Show(document);
	ui.Update(0.0);
	CHECK(DrawnWidth(ui.Overlay()) == doctest::Approx(100.0f));
	CHECK(ui.ReloadChanged() == 0);

	// A different length, so the change shows in the file's size whatever the clock's resolution.
	scratch.Put(
	    "base/ui/panel.rcss",
	    "#panel { display: block; width: 50px; height: 50px; background-color: #336699; }");
	CHECK(ui.ReloadChanged() == 1);
	ui.Update(0.1);
	CHECK(DrawnWidth(ui.Overlay()) == doctest::Approx(50.0f));
	CHECK(ui.IsShown(document));
	CHECK(ui.ReloadChanged() == 0);

	// A patch a mod adds beside it is a change too.
	scratch.Put("mod/ui/panel.patch.rcss", "#panel { width: 200px; }");
	CHECK(ui.ReloadChanged() == 1);
	ui.Update(0.2);
	CHECK(DrawnWidth(ui.Overlay()) == doctest::Approx(200.0f));
	CHECK(ui.Problems() == 0);
}


TEST_CASE("Ui: a document read again keeps its handle, its data model and the model's events")
{
	RegisterTypes();
	Scratch scratch;
	scratch.Put("base/ui/market.rml", kMarketDocument);
	Ui::UiSystem ui{scratch.Files(), 640, 480};
	ui.LoadFont("fonts/DroidSans.ttf");
	Venue venue{"Ostra Highport", Standing::Grey, 1250000, {{"Copper", 12, 400}}};
	Ui::DataModel &model = ui.CreateDataModel("market");
	model.Bind("venue", &venue);
	std::vector<std::string> bought;
	model.BindEvent(
	    "buy", [&bought](std::span<const Ui::EventArgument> arguments)
	    { bought.push_back(Ui::AsText(arguments[0])); });
	const Ui::DocumentHandle document = ui.LoadDocument("ui/market.rml");
	ui.Show(document);
	ui.Update(0.0);

	std::string edited = kMarketDocument;
	edited.replace(edited.find("goods</p>"), 9, "goods for sale</p>");
	scratch.Put("base/ui/market.rml", edited);
	CHECK(ui.ReloadChanged() == 1);
	ui.Update(0.1);
	CHECK(ui.IsShown(document));
	CHECK(ui.ElementText(document, "count") == "1 goods for sale");
	CHECK(ui.ElementText(document, "station") == "Ostra Highport");

	venue.station = "Corva Refinery";
	model.Changed("venue");
	ui.Update(0.2);
	CHECK(ui.ElementText(document, "station") == "Corva Refinery");
	CHECK(ui.Click(document, "buy-0"));
	ui.Update(0.3);
	CHECK(bought == std::vector<std::string>{"Copper"});
	CHECK(ui.Problems() == 0);

	// A document that no longer loads leaves the one read before, and is not tried again until it
	// changes again.
	scratch.Remove("base/ui/market.rml");
	CHECK(ui.ReloadChanged() == 0);
	ui.Update(0.4);
	CHECK(ui.IsShown(document));
	CHECK(ui.ElementText(document, "count") == "1 goods for sale");
	CHECK(ui.ReloadChanged() == 0);
	scratch.Put("base/ui/market.rml", kMarketDocument);
	CHECK(ui.ReloadChanged() == 1);
	ui.Update(0.5);
	CHECK(ui.ElementText(document, "count") == "1 goods");
}


TEST_CASE("Ui: RmlUi's debugger draws into the overlay while it is shown, and nothing once hidden")
{
	Scratch scratch;
	Ui::UiSystem ui{scratch.Files(), 640, 480};
	ui.LoadFont("fonts/DroidSans.ttf");
	ui.Update(0.0);
	CHECK(ui.Overlay().Draws().empty());

	ui.ShowDebugger(true);
	ui.Update(0.1);
	CHECK_FALSE(ui.Overlay().Draws().empty());
	CHECK(ui.OpenDocuments() == 0);

	ui.ShowDebugger(false);
	ui.Update(0.2);
	CHECK(ui.Overlay().Draws().empty());
	ui.ShowDebugger(true);
	ui.Update(0.3);
	CHECK_FALSE(ui.Overlay().Draws().empty());
	CHECK(ui.Problems() == 0);
}


TEST_CASE("Ui: a document's image is decoded on the I/O worker, and drawn once it is there")
{
	Scratch scratch;
	scratch.Put("base/ui/wide.ppm", Ppm(4, 2));
	scratch.Put("base/ui/tall.ppm", Ppm(3, 6));
	scratch.Put("base/ui/frame.ppm", Ppm(5, 5));
	scratch.Put("base/ui/gallery.rml", kGalleryDocument);
	Jobs::IoWorker worker;
	Resource::ResourceManager resources{scratch.Files(), worker, {.hotReload = false}};
	Ui::UiSystem ui{scratch.Files(), 640, 480, &resources};
	const Ui::DocumentHandle document = ui.LoadDocument("ui/gallery.rml");
	ui.Show(document);
	ui.Update(0.0);

	// Until the manager has them the images draw nothing. A decorator's is decoded as its element is
	// first drawn, since RmlUi keeps the size it reads then.
	CHECK(resources.Loading() == 2);
	CHECK(DrawnWidth(ui.Overlay(), 4) == 0.0f);
	CHECK(DrawnWidth(ui.Overlay(), 3) == 0.0f);
	CHECK(DrawnWidth(ui.Overlay(), 5) == doctest::Approx(40.0f));
	ui.Update(0.1);
	CHECK(DrawnWidth(ui.Overlay(), 4) == 0.0f);

	worker.WaitIdle();
	resources.Update();
	CHECK(resources.Loading() == 0);
	ui.Update(0.2);
	CHECK(DrawnWidth(ui.Overlay(), 4) == doctest::Approx(64.0f));

	// Laid out again: with no size of its own, it has the image's.
	CHECK(DrawnWidth(ui.Overlay(), 3) == doctest::Approx(3.0f));
	CHECK(DrawnWidth(ui.Overlay(), 5) == doctest::Approx(40.0f));

	// The document read again draws them at once: RmlUi keeps the textures by path.
	scratch.Put("base/ui/gallery.rml", std::string{kGalleryDocument} + "\n");
	CHECK(ui.ReloadChanged() == 1);
	ui.Update(0.3);
	CHECK(resources.Loading() == 0);
	CHECK(DrawnWidth(ui.Overlay(), 4) == doctest::Approx(64.0f));
	CHECK(DrawnWidth(ui.Overlay(), 3) == doctest::Approx(3.0f));
	CHECK(ui.Problems() == 0);
}


TEST_CASE("Ui: an image still decoding when its document is read again is drawn in the one read again")
{
	Scratch scratch;
	scratch.Put("base/ui/wide.ppm", Ppm(4, 2));
	scratch.Put("base/ui/tall.ppm", Ppm(3, 6));
	scratch.Put("base/ui/frame.ppm", Ppm(5, 5));
	scratch.Put("base/ui/gallery.rml", kGalleryDocument);
	Jobs::IoWorker worker;
	Resource::ResourceManager resources{scratch.Files(), worker, {.hotReload = false}};
	Ui::UiSystem ui{scratch.Files(), 640, 480, &resources};
	const Ui::DocumentHandle document = ui.LoadDocument("ui/gallery.rml");
	ui.Show(document);
	ui.Update(0.0);
	CHECK(resources.Loading() == 2);

	scratch.Put("base/ui/gallery.rml", std::string{kGalleryDocument} + "\n");
	CHECK(ui.ReloadChanged() == 1);
	ui.Update(0.1);
	worker.WaitIdle();
	resources.Update();
	ui.Update(0.2);
	CHECK(ui.IsShown(document));
	CHECK(DrawnWidth(ui.Overlay(), 4) == doctest::Approx(64.0f));
	CHECK(DrawnWidth(ui.Overlay(), 3) == doctest::Approx(3.0f));
	CHECK(ui.Problems() == 0);
}


TEST_CASE("Ui: an image missing or that does not decode is logged once, and stays blank")
{
	Scratch scratch;
	scratch.Put("base/ui/broken.ppm", "not an image");
	const std::string document =
	    "<rml><head><style>img { display: block; width: 20px; height: 20px; }</style></head><body>"
	    "<img src=\"missing.ppm\"/><img src=\"broken.ppm\"/></body></rml>";
	scratch.Put("base/ui/broken.rml", document);
	Jobs::IoWorker worker;
	Resource::ResourceManager resources{scratch.Files(), worker, {.hotReload = false}};
	Ui::UiSystem ui{scratch.Files(), 640, 480, &resources};
	ui.Show(ui.LoadDocument("ui/broken.rml"));
	ui.Update(0.0);
	worker.WaitIdle();
	resources.Update();
	CHECK(resources.Loading() == 0);
	ui.Update(0.1);
	ui.Update(0.2);
	CHECK(ui.Problems() == 2);
	CHECK(ui.Overlay().Textures().empty());

	// Nor again for the document read again.
	scratch.Put("base/ui/broken.rml", document + "\n");
	CHECK(ui.ReloadChanged() == 1);
	ui.Update(0.3);
	worker.WaitIdle();
	resources.Update();
	ui.Update(0.4);
	CHECK(ui.Problems() == 2);
	CHECK(ui.Overlay().Textures().empty());
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
