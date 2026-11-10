#include <jettison/devtools/Console.h>

#include <jettison/core/Log.h>
#include <jettison/reflect/Reflect.h>

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison;
using DevTools::Console;
using DevTools::ConsoleLine;
using Reflect::Attribute;

namespace
{
enum class Quality
{
	Low,
	High,
};

struct RenderSettings
{
	bool vsync{true};
	int maxFrames{3};
	std::uint8_t samples{4};
	float exposure{1.0f};
	std::string title{"Jettison"};
	Quality quality{Quality::High};
	int hidden{7}; // Not Console: not a variable.
};

struct Unbindable
{
	std::vector<int> list;
};

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterEnum<Quality>("ConsoleTests.Quality")
		    .Value<Quality::Low>("Low")
		    .Value<Quality::High>("High");
		RegisterType<RenderSettings>("ConsoleTests.RenderSettings")
		    .Field<&RenderSettings::vsync>("vsync", Attribute::Console)
		    .Field<&RenderSettings::maxFrames>("max_frames", Attribute::Console | Attribute::Saved)
		    .Field<&RenderSettings::samples>("samples", Attribute::Console)
		    .Field<&RenderSettings::exposure>("exposure", Attribute::Console)
		    .Field<&RenderSettings::title>("title", Attribute::Console)
		    .Field<&RenderSettings::quality>("quality", Attribute::Console)
		    .Field<&RenderSettings::hidden>("hidden");
		RegisterType<Unbindable>("ConsoleTests.Unbindable")
		    .Field<&Unbindable::list>("list", Attribute::Console);
		return true;
	}();
	(void)registered;
}

std::vector<std::string> Texts(const Console &console)
{
	std::vector<std::string> texts;
	for (const ConsoleLine &line : console.Lines())
		texts.push_back(line.text);
	return texts;
}

std::string Last(const Console &console)
{
	const std::vector<ConsoleLine> lines = console.Lines();
	REQUIRE_FALSE(lines.empty());
	return lines.back().text;
}
} // namespace

TEST_CASE("Console: a line splits on spaces, with quotes for spaces inside a word")
{
	using DevTools::SplitLine;
	CHECK(SplitLine("  set  a   b ") == std::vector<std::string>{"set", "a", "b"});
	CHECK(
	    SplitLine(R"(say "hello there" "a \"b\" \\ c" "")") ==
	    std::vector<std::string>{"say", "hello there", R"(a "b" \ c)", ""});
	CHECK(SplitLine("") == std::vector<std::string>{});
	CHECK_FALSE(SplitLine(R"(say "open)"));
}

TEST_CASE("Console: a command runs with its arguments, and one that throws prints its error")
{
	Console console;
	std::vector<std::string> received;
	console.RegisterCommand(
	    "say", "Says the words",
	    [&](Console &target, std::span<const std::string> arguments)
	    {
		    received.assign(arguments.begin(), arguments.end());
		    target.Print("said");
	    });
	console.RegisterCommand(
	    "fail", "Fails",
	    [](Console &, std::span<const std::string>) { throw std::runtime_error{"no fuel"}; });

	console.Execute(R"(say hello "wide world")");
	CHECK(received == std::vector<std::string>{"hello", "wide world"});
	CHECK(Texts(console) == std::vector<std::string>{R"(> say hello "wide world")", "said"});

	console.Execute("fail");
	CHECK(Last(console) == "fail: no fuel");
	CHECK(console.Lines().back().kind == ConsoleLine::Kind::Error);
	console.Execute("warp 9");
	CHECK(Last(console) == "there is no command or variable 'warp'");
	console.Execute("say \"open");
	CHECK(Last(console) == "a quote is not closed");

	console.Execute("help say");
	CHECK(Last(console) == "say: Says the words");
	console.Execute("clear");
	CHECK(console.Lines().empty());
}

TEST_CASE("Console: an object's Console fields are variables, read and set as text")
{
	RegisterTypes();
	Console console;
	RenderSettings settings;
	console.BindVariables("render", settings);

	CHECK(console.Value("render.vsync") == "true");
	CHECK(console.Value("render.exposure") == "1");
	CHECK_FALSE(console.Value("render.hidden"));

	console.Execute("render.max_frames 2");
	CHECK(settings.maxFrames == 2);
	CHECK(Last(console) == "render.max_frames = 2");
	console.Execute("render.vsync off");
	CHECK_FALSE(settings.vsync);
	console.Execute("render.exposure 0.25");
	CHECK(settings.exposure == 0.25f);
	console.Execute(R"(render.title "Jettison, again")");
	CHECK(settings.title == "Jettison, again");
	console.Execute("render.quality Low");
	CHECK(settings.quality == Quality::Low);
	console.Execute("render.quality");
	CHECK(Last(console) == "render.quality = Low");

	// Refused, and the value stays.
	console.Execute("render.samples 300");
	CHECK(Last(console) == "render.samples is a whole number from 0, and '300' is not");
	CHECK(settings.samples == 4);
	console.Execute("render.quality Ultra");
	CHECK(Last(console) == "render.quality is one of Low, High, and 'Ultra' is not");
	console.Execute("render.vsync maybe");
	CHECK(Last(console) == "render.vsync is true or false, and 'maybe' is not");
	console.Execute("render.max_frames 1 2");
	CHECK(Last(console) == "render.max_frames takes one value; quote it if it has spaces");
	CHECK(settings.maxFrames == 2);

	console.Execute("vars");
	const std::vector<std::string> texts = Texts(console);
	CHECK(std::ranges::find(texts, "render.title = Jettison, again") != texts.end());
}

TEST_CASE("Console: names are checked, and a type the console cannot set is refused whole")
{
	RegisterTypes();
	Console console;
	RenderSettings settings;
	CHECK_THROWS_AS(
	    console.RegisterCommand("help", "", [](Console &, std::span<const std::string>) {}),
	    std::logic_error);
	CHECK_THROWS_AS(
	    console.RegisterCommand("two words", "", [](Console &, std::span<const std::string>) {}),
	    std::logic_error);
	CHECK_THROWS_AS(console.RegisterCommand("nothing", "", {}), std::logic_error);
	console.BindVariables("render", settings);
	CHECK_THROWS_WITH_AS(
	    console.BindVariables("render", settings), "Console: 'render.vsync' is already taken",
	    std::logic_error);
	Unbindable unbindable;
	CHECK_THROWS_WITH_AS(
	    console.BindVariables("lists", unbindable), doctest::Contains("which the console cannot set"),
	    std::logic_error);
	CHECK_FALSE(console.Value("lists.list"));
}

TEST_CASE("Console: history keeps each line once in a row, and completion extends the first word")
{
	RegisterTypes();
	Console console;
	RenderSettings settings;
	console.BindVariables("render", settings);
	console.Execute("help");
	console.Execute("help");
	console.Execute("   ");
	console.Execute("vars");
	CHECK(console.History() == std::vector<std::string>{"help", "vars"});

	CHECK(console.Complete("render.e") == std::vector<std::string>{"render.exposure"});
	CHECK(console.Complete("").size() == 3 + 6);
	CHECK(console.CompleteLine("render.e") == "render.exposure ");
	CHECK(console.CompleteLine("ren") == "render.");
	CHECK(console.CompleteLine("render.m") == "render.max_frames ");
	CHECK(console.CompleteLine("zzz") == "zzz");
	CHECK(console.CompleteLine("render.vsync of") == "render.vsync of");
}

TEST_CASE("Console: the log is captured into the lines, and a full console drops the oldest")
{
	Console console{3};
	console.CaptureLog();
	constexpr Core::Category kTest{"console_test"};
	Core::Log(Core::Severity::Warning, kTest, "dock {} refused", 7);
	const std::vector<ConsoleLine> lines = console.Lines();
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].kind == ConsoleLine::Kind::Log);
	CHECK(lines[0].severity == Core::Severity::Warning);
	CHECK(lines[0].category == "console_test");
	CHECK(lines[0].text == "dock 7 refused");

	console.Print("one");
	console.Print("two");
	console.Print("three");
	CHECK(Texts(console) == std::vector<std::string>{"one", "two", "three"});
}
