#include <jettison/ui/StringTable.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/ui/UiSystem.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison;
using Ui::StringTable;

namespace
{
// A base mount and a mod's over it, of the case's own; with fonts, the shipped assets below them.
class Scratch
{
  public:
	explicit Scratch(bool fonts = false)
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-strings-tests-" + std::to_string(getpid()))}
	{
		std::filesystem::remove_all(m_directory);
		std::filesystem::create_directories(m_directory / "base");
		std::filesystem::create_directories(m_directory / "mod");
		if (fonts)
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

constexpr const char *kEnglish = R"yaml(version: 1
market:
  title: Market
  docked: Docked at {0}
  pays: "{0} pays {1}"
  crossing:
    one: "; {0} crate crossing the deck"
    other: "; {0} crates crossing the deck"
  hold: "Hold {{screen.used}} / {{screen.capacity}} crates"
)yaml";

struct Screen
{
	std::string station;
	std::int64_t used{0};
	double price{0.0};
	std::int64_t day{0};
	std::int64_t minutes{0};
};


void RegisterScreen()
{
	static const bool registered = []
	{
		Reflect::RegisterType<Screen>("StringTableTests.Screen")
		    .Field<&Screen::station>("station")
		    .Field<&Screen::used>("used")
		    .Field<&Screen::price>("price")
		    .Field<&Screen::day>("day")
		    .Field<&Screen::minutes>("minutes");
		return true;
	}();
	(void)registered;
}
} // namespace


TEST_CASE("Strings: a table's nested keys read by dots, and a key no table has reads as itself")
{
	const Scratch scratch;
	scratch.Put("base/strings/en/market.yaml", kEnglish);
	StringTable strings;
	strings.Load(scratch.Files(), "en");
	CHECK(strings.Language() == "en");
	CHECK(strings.Count() == 6);
	CHECK(strings.Text("market.title") == "Market");
	CHECK(strings.Find("market.crossing.one") == "; {0} crate crossing the deck");
	CHECK(!strings.Find("market.nothing"));
	CHECK(strings.Problems() == 0);

	CHECK(strings.Text("market.nothing") == "market.nothing");
	CHECK(strings.Text("market.nothing") == "market.nothing");
	CHECK(strings.Problems() == 1);
	CHECK(StringTable::Languages(scratch.Files()) == std::vector<std::string>{"en"});
}


TEST_CASE("Strings: arguments format by position, and a count chooses its plural form")
{
	const Scratch scratch;
	scratch.Put("base/strings/en/market.yaml", kEnglish);
	scratch.Put(
	    "base/strings/ru/market.yaml", "version: 1\n"
	                                   "market:\n"
	                                   "  pays: \"{1} платит {0}\"\n"
	                                   "  crossing:\n"
	                                   "    one: \"; {0} ящик\"\n"
	                                   "    few: \"; {0} ящика\"\n"
	                                   "    many: \"; {0} ящиков\"\n");
	StringTable strings;
	strings.Load(scratch.Files(), "en");
	CHECK(strings.Format("market.docked", "Ostra Highport") == "Docked at Ostra Highport");
	CHECK(strings.Format("market.pays", "Corva Refinery", "81,200") == "Corva Refinery pays 81,200");
	CHECK(strings.Plural("market.crossing", 1) == "; 1 crate crossing the deck");
	CHECK(strings.Plural("market.crossing", 0) == "; 0 crates crossing the deck");
	CHECK(strings.Plural("market.crossing", 12) == "; 12 crates crossing the deck");

	strings.Load(scratch.Files(), "ru");
	CHECK(strings.Problems() == 0);
	CHECK(strings.Format("market.pays", "Corva Refinery", "81,200") == "81,200 платит Corva Refinery");
	CHECK(strings.Plural("market.crossing", 21) == "; 21 ящик");
	CHECK(strings.Plural("market.crossing", 23) == "; 23 ящика");
	CHECK(strings.Plural("market.crossing", 12) == "; 12 ящиков");
	CHECK(strings.Plural("market.crossing", 25) == "; 25 ящиков");

	// What the translation lacks is the base's.
	CHECK(strings.Text("market.title") == "Market");

	CHECK(StringTable::PluralCategory("en", 1) == "one");
	CHECK(StringTable::PluralCategory("fr", 0) == "one");
	CHECK(StringTable::PluralCategory("pt-PT", 0) == "other");
	CHECK(StringTable::PluralCategory("pl", 22) == "few");
	CHECK(StringTable::PluralCategory("pl", 21) == "many");
	CHECK(StringTable::PluralCategory("cs", 3) == "few");
	CHECK(StringTable::PluralCategory("ja", 1) == "other");
}


TEST_CASE("Strings: arguments known only as the game runs format as a call's do")
{
	const Scratch scratch;
	scratch.Put("base/strings/en/market.yaml", kEnglish);
	scratch.Put(
	    "base/strings/ru/market.yaml", "version: 1\n"
	                                   "market:\n"
	                                   "  pays: \"{1} платит {0}\"\n"
	                                   "  crossing:\n"
	                                   "    few: \"; {0} ящика\"\n");
	StringTable strings;
	strings.Load(scratch.Files(), "ru");
	const std::vector<std::string> pays{"Corva Refinery", "81,200"};
	CHECK(strings.FormatArguments("market.pays", pays) == "81,200 платит Corva Refinery");

	// The count chooses the form, and the arguments say what it reads.
	CHECK(
	    strings.PluralArguments("market.crossing", 23, std::vector<std::string>{"23"}) == "; 23 ящика");
	CHECK(strings.Problems() == 0);

	// Too few for the text is a problem, and the text stands; too many, past the most, another.
	CHECK(strings.FormatArguments("market.pays", std::vector<std::string>{"x"}) == "{1} платит {0}");
	CHECK(strings.Problems() == 1);
	const std::vector<std::string> many(StringTable::kMaxArguments + 1, "z");
	CHECK(strings.FormatArguments("market.pays", many) == "z платит z");
	CHECK(strings.Problems() == 2);
}


TEST_CASE("Strings: a regional language is read over its primary language, over the base")
{
	const Scratch scratch;
	scratch.Put("base/strings/en/market.yaml", kEnglish);
	scratch.Put(
	    "base/strings/pt/market.yaml",
	    "version: 1\nmarket:\n  title: Mercado\n  docked: Atracado em {0}\n");
	scratch.Put("base/strings/pt-BR/market.yaml", "version: 1\nmarket: {title: Mercadão}\n");
	StringTable strings;
	strings.Load(scratch.Files(), "pt-BR");
	CHECK(strings.Text("market.title") == "Mercadão");
	CHECK(strings.Format("market.docked", "Ostra") == "Atracado em Ostra");
	CHECK(strings.Text("market.pays") == "{0} pays {1}");
	CHECK(StringTable::Languages(scratch.Files()) == std::vector<std::string>{"en", "pt", "pt-BR"});

	// A language with no tables reads as the base.
	strings.Load(scratch.Files(), "de");
	CHECK(strings.Language() == "de");
	CHECK(strings.Text("market.title") == "Market");
	CHECK(strings.Problems() == 0);
}


TEST_CASE("Strings: a mod's file changes the strings it names, and a mod's new file adds strings")
{
	const Scratch scratch;
	scratch.Put("base/strings/en/market.yaml", kEnglish);
	scratch.Put(
	    "mod/strings/en/market.yaml", "version: 1\nmarket:\n  title: Exchange\n  sold: Sold out\n");
	scratch.Put("mod/strings/en/mod.yaml", "version: 1\nmod.greeting: Hello\n");
	StringTable strings;
	strings.Load(scratch.Files(), "en");
	CHECK(strings.Text("market.title") == "Exchange");
	CHECK(strings.Text("market.sold") == "Sold out");
	CHECK(strings.Text("market.docked") == "Docked at {0}");
	CHECK(strings.Text("mod.greeting") == "Hello");
	CHECK(strings.Problems() == 0);

	// One key in two files of a language is a mistake the files' order would settle.
	scratch.Put("mod/strings/en/again.yaml", "version: 1\nmarket.title: Bazaar\n");
	CHECK_THROWS_WITH_AS(
	    strings.Load(scratch.Files(), "en"),
	    doctest::Contains("'market.title' is in both mod:strings/en/again.yaml and"),
	    std::runtime_error);

	// And the table before it stays.
	CHECK(strings.Text("market.title") == "Exchange");
}


TEST_CASE("Strings: a file that is not a table is refused, naming it and its line")
{
	const Scratch scratch;
	StringTable strings;
	scratch.Put("base/strings/en/market.yaml", "version: 1\nmarket:\n  title: [Market]\n");
	CHECK_THROWS_WITH_AS(
	    strings.Load(scratch.Files(), "en"),
	    doctest::Contains(
	        "base:strings/en/market.yaml:3: 'market.title' is neither a text nor a mapping"),
	    std::runtime_error);
	scratch.Put("base/strings/en/market.yaml", "market: {title: Market}\n");
	CHECK_THROWS_WITH_AS(
	    strings.Load(scratch.Files(), "en"), doctest::Contains("a table is of version 1"),
	    std::runtime_error);
	scratch.Put("base/strings/en/market.yaml", "version: 1\nmarket: {title: Market\n");
	CHECK_THROWS_WITH_AS(
	    strings.Load(scratch.Files(), "en"), doctest::Contains("base:strings/en/market.yaml:"),
	    std::runtime_error);
	scratch.Put("base/strings/en/market.yaml", "version: 1\nmarket: {title: \"Market {\"}\n");
	CHECK_THROWS_WITH_AS(
	    strings.Load(scratch.Files(), "en"),
	    doctest::Contains("'market.title' with a brace that is not an argument"), std::runtime_error);
	CHECK_THROWS_WITH_AS(
	    strings.Load(scratch.Files(), "German"), doctest::Contains("'German' is not a language"),
	    std::runtime_error);
	std::filesystem::remove(
	    std::filesystem::temp_directory_path() /
	    ("jettison-strings-tests-" + std::to_string(getpid())) / "base/strings/en/market.yaml");
	CHECK_THROWS_WITH_AS(
	    strings.Load(scratch.Files(), "en"),
	    doctest::Contains("there are no tables for the base language under strings/en/"),
	    std::runtime_error);
}


TEST_CASE(
    "Strings: a translation that names what its base text does not is refused, and the base stands")
{
	const Scratch scratch;
	scratch.Put("base/strings/en/market.yaml", kEnglish);
	scratch.Put(
	    "base/strings/de/market.yaml", "version: 1\n"
	                                   "market:\n"
	                                   "  title: Markt\n"
	                                   "  docked: Angedockt an {0} um {1}\n"
	                                   "  pays: \"{0} zahlt {\"\n"
	                                   "  hold: \"Laderaum {{screen.used}} / {{screen.room}}\"\n"
	                                   "  unknown: Unbekannt\n"
	                                   "  crossing:\n"
	                                   "    one: \"; eine Kiste\"\n");
	StringTable strings;
	strings.Load(scratch.Files(), "de");
	CHECK(strings.Problems() == 4);
	CHECK(strings.Text("market.title") == "Markt");
	CHECK(strings.Text("market.docked") == "Docked at {0}");
	CHECK(strings.Text("market.pays") == "{0} pays {1}");
	CHECK(strings.Text("market.hold") == "Hold {{screen.used}} / {{screen.capacity}} crates");
	CHECK(!strings.Find("market.unknown"));

	// A form may name fewer arguments than its base: "a crate" for one.
	CHECK(strings.Plural("market.crossing", 1) == "; eine Kiste");
	CHECK(strings.Plural("market.crossing", 2) == "; 2 crates crossing the deck");
}


TEST_CASE("Strings: a fraction, a date and a time in each of the engine's languages, from its tables")
{
	using namespace std::chrono;
	const Scratch scratch{true};
	struct Expected
	{
		const char *language;
		const char *price;
		const char *large;
		const char *date;
		const char *afternoon;
		const char *midnight;
	};
	for (const Expected &expected : {
	         Expected{"en", "1,250.50", "-1,234,567.9", "11/27/2026", "3:05 PM", "12:05 AM"},
	         Expected{"de", "1.250,50", "-1.234.567,9", "27.11.2026", "15:05", "00:05"},
	         Expected{"fr", "1\u00A0250,50", "-1\u00A0234\u00A0567,9", "27/11/2026", "15:05", "00:05"},
	         Expected{"ja", "1,250.50", "-1,234,567.9", "2026/11/27", "15:05", "0:05"},
	     })
	{
		CAPTURE(expected.language);
		StringTable strings;
		strings.Load(scratch.Files(), expected.language);
		CHECK(strings.Decimal(1250.5, 2) == expected.price);
		CHECK(strings.Decimal(-1234567.89, 1) == expected.large);
		CHECK(strings.Date(2026y / November / 27d) == expected.date);
		CHECK(strings.Time(15h + 5min) == expected.afternoon);
		CHECK(strings.Time(5min) == expected.midnight);
		CHECK(strings.Problems() == 0);
	}
}


TEST_CASE("Strings: a fraction rounds to the digits asked, and a table with no forms reads ISO's")
{
	using namespace std::chrono;
	const Scratch scratch;
	scratch.Put("base/strings/en/market.yaml", kEnglish);
	StringTable strings;
	strings.Load(scratch.Files(), "en");
	CHECK(strings.Decimal(0.125, 2) == "0.12");
	CHECK(strings.Decimal(2.5, 0) == "2");
	CHECK(strings.Decimal(-0.001, 2) == "0.00");
	CHECK(strings.Decimal(999.999, 2) == "1,000.00");
	CHECK(strings.Date(2026y / March / 4d) == "2026-03-04");
	CHECK(strings.Time(15h + 5min) == "15:05");
	CHECK(strings.Time(-1min) == "23:59");
	CHECK(strings.Time(25h) == "01:00");
	CHECK(strings.Problems() == 0);
}


TEST_CASE("Strings: a text's [[keys]] are replaced, and what is not a key is left")
{
	const Scratch scratch;
	scratch.Put("base/strings/en/market.yaml", kEnglish);
	StringTable strings;
	strings.Load(scratch.Files(), "en");
	int replaced = 0;
	CHECK(
	    strings.Translate("[[market.title]]: [[market.hold]]", replaced) ==
	    "Market: Hold {{screen.used}} / {{screen.capacity}} crates");
	CHECK(replaced == 2);
	CHECK(strings.Translate("a [[ list ]] and [[unclosed", replaced) == "a [[ list ]] and [[unclosed");
	CHECK(replaced == 0);
	CHECK(strings.Translate("[[market.none]]!", replaced) == "market.none!");
	CHECK(replaced == 1);
	CHECK(strings.Problems() == 1);
}


TEST_CASE("Ui: a document's keys read in the language set, and again in another, the handle kept")
{
	RegisterScreen();
	const Scratch scratch{true};
	scratch.Put(
	    "base/strings/en/test.yaml",
	    "version: 1\n"
	    "test:\n"
	    "  title: Market\n"
	    "  name: Your name\n"
	    "  close: Close the market\n"
	    "  docked: \"Docked at <span id='at'>{{screen.station}}</span>\"\n");
	scratch.Put(
	    "base/strings/de/test.yaml",
	    "version: 1\n"
	    "test:\n"
	    "  title: Markt\n"
	    "  name: Ihr Name\n"
	    "  close: Markt schließen\n"
	    "  docked: \"<span id='at'>{{screen.station}}</span>: angedockt\"\n");
	scratch.Put(
	    "base/test.rml", R"rml(<rml><head><style>body { font-family: Droid Sans; }</style></head>
<body data-model="test"><h1 id="title">[[test.title]]</h1><p id="docked">[[test.docked]]</p>
<p id="missing">[[test.missing]]</p>
<input id="name" type="text" placeholder="[[test.name]]" value="[[test.title]], [[test.title]]"/>
<button id="close" title="[[test.close]]" data-attr-tag="'[[test.close]]'">x</button>
<p id="price">{{screen.price | decimal(2)}}</p><p id="whole">{{screen.price | decimal}}</p>
<p id="day">{{screen.day | date}}</p><p id="time">{{screen.minutes | time}}</p></body></rml>)rml");

	Ui::UiSystem ui{scratch.Files(), 640, 480};
	ui.LoadFont("fonts/DroidSans.ttf", true);
	Screen screen{"Ostra Highport", 3, 1250.5, 20'784, 15 * 60 + 5};
	ui.CreateDataModel("test").Bind("screen", &screen);
	ui.SetLanguage("en");
	const Ui::DocumentHandle document = ui.LoadDocument("test.rml");
	ui.Show(document);
	ui.Update(0.0);
	CHECK(ui.ElementText(document, "title") == "Market");
	CHECK(ui.ElementText(document, "at") == "Ostra Highport");
	CHECK(ui.ElementText(document, "docked").starts_with("Docked at <span"));
	CHECK(ui.ElementText(document, "missing") == "test.missing");
	CHECK(ui.ElementAttribute(document, "name", "placeholder") == "Your name");
	CHECK(ui.ElementAttribute(document, "name", "value") == "Market, Market");
	CHECK(ui.ElementAttribute(document, "close", "title") == "Close the market");
	CHECK(ui.ElementAttribute(document, "close", "data-attr-tag") == "'[[test.close]]'");
	CHECK(ui.ElementAttribute(document, "close", "tag") == "[[test.close]]");
	CHECK(ui.ElementText(document, "price") == "1,250.50");
	CHECK(ui.ElementText(document, "whole") == "1,250");
	CHECK(ui.ElementText(document, "day") == "11/27/2026");
	CHECK(ui.ElementText(document, "time") == "3:05 PM");
	CHECK(ui.Problems() == 1);

	ui.SetLanguage("de");
	ui.Update(0.1);
	CHECK(ui.Strings().Language() == "de");
	CHECK(ui.IsShown(document));
	CHECK(ui.OpenDocuments() == 1);
	CHECK(ui.ElementText(document, "title") == "Markt");
	CHECK(ui.ElementText(document, "at") == "Ostra Highport");
	CHECK(ui.ElementText(document, "docked").ends_with("</span>: angedockt"));
	CHECK(ui.ElementAttribute(document, "name", "placeholder") == "Ihr Name");
	CHECK(ui.ElementAttribute(document, "close", "title") == "Markt schließen");
	CHECK(ui.ElementText(document, "price") == "1.250,50");
	CHECK(ui.ElementText(document, "day") == "27.11.2026");
	CHECK(ui.ElementText(document, "time") == "15:05");
	CHECK_THROWS_AS(ui.SetLanguage("not a language"), std::runtime_error);
	CHECK(ui.Strings().Language() == "de");
}
