#include <jettison/devtools/Console.h>
#include <jettison/devtools/Theme.h>
#include <jettison/devtools/Views.h>

#include <jettison/reflect/Reflect.h>

#include <doctest/doctest.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Jettison;
using namespace Jettison::DevTools;
using Reflect::Attribute;

namespace
{
// ImGui with no renderer, as ViewTests runs it: a context, a display size and a built font atlas.
// The context is the fixture's own, and destroyed by pointer: ImGui::DestroyContext(nullptr)
// destroys whichever context is *current*, so a case with two fixtures and a context reused at the
// same address (measured, the general allocator hands the same block back) would destroy one
// context twice and leave the other running, crashing in ImGui::Shutdown. Every fixture that owns a
// context owns it by pointer.
class HeadlessImGui
{
  public:
	HeadlessImGui() : m_context{ImGui::CreateContext()}
	{
		// CreateContext hands the previous context back as the current one, so a case with two
		// fixtures has to say whose frame this is.
		ImGui::SetCurrentContext(m_context);
		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = {1280.0f, 720.0f};
		io.DeltaTime = 1.0f / 60.0f;
		io.IniFilename = nullptr;
	}


	~HeadlessImGui()
	{
		ImGuiContext *outer = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_context);
		ImGui::DestroyContext(m_context);
		if (outer != m_context)
			ImGui::SetCurrentContext(outer);
	}


	HeadlessImGui(const HeadlessImGui &) = delete;


	HeadlessImGui &operator=(const HeadlessImGui &) = delete;

  private:
	ImGuiContext *m_context{nullptr};
};


// sRGB's decoding, as the UI's fragment stage does it (colour.glsl's SrgbToLinear), and its encoding,
// as an sRGB attachment does it, so the path a colour takes to the frame is modelled stage by stage.
float DecodeSrgb(float encoded)
{
	return encoded <= 0.04045f ? encoded / 12.92f : std::powf((encoded + 0.055f) / 1.055f, 2.4f);
}


float EncodeSrgb(float linear)
{
	return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::powf(linear, 1.0f / 2.4f) - 0.055f;
}


std::uint8_t ToByte(float encoded)
{
	return static_cast<std::uint8_t>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
}


// The palette's byte for a role, from the table Theme.cpp publishes.
std::uint8_t ByteOf(Palette palette, Token token, int channel)
{
	const Rgb &rgb = PaletteTokens(palette)[static_cast<std::size_t>(token)];
	return channel == 0 ? rgb.r : channel == 1 ? rgb.g : rgb.b;
}


// A face loaded the way the render backend loads them: from bytes, with the name FontFaces.cpp
// writes and without it, so the mono rule is tested against both. The bytes are the test's, as the
// backend's are, because the atlas reads glyphs from them.
std::vector<std::byte> ReadFace(const char *file)
{
	const std::string path = std::string{JETTISON_ASSET_SOURCE_DIR "/fonts/"} + file;
	const std::size_t size = std::filesystem::file_size(path);
	std::ifstream in{path, std::ios::binary};
	std::vector<std::byte> bytes(size);
	in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
	return bytes;
}
} // namespace


TEST_CASE("Theme: every palette colours every role, and the palette is what is applied")
{
	RegisterThemeTypes();
	for (const Palette palette : PaletteVariants())
	{
		for (std::size_t token = 0; token < kTokenCount; ++token)
		{
			const Rgba colour = PaletteColour(palette, Token(token), 0.75f);
			CHECK(colour.a == doctest::Approx(0.75f));
			for (const float channel : {colour.r, colour.g, colour.b})
			{
				CHECK(std::isfinite(channel));
				CHECK(channel >= 0.0f);
				CHECK(channel <= 1.0f);
			}
			CHECK(ToByte(colour.r) == ByteOf(palette, Token(token), 0));
		}

		// A role is not a copy of its neighbour's colour by accident: the panels are distinguishable.
		CHECK(
		    PaletteTokens(palette)[static_cast<std::size_t>(Token::Background)].b !=
		    PaletteTokens(palette)[static_cast<std::size_t>(Token::Foreground)].b);
		CHECK(PaletteName(palette).size() > 1);
	}

	// The roles pinned to the hexes the palette publishes, in the order the enum names them: the
	// table is read by index, so an entry out of step with the enum shows up as a control drawn in
	// the wrong colour, and these are the ones that would be missed.
	CHECK(
	    PaletteColour(Palette::Mocha, Token::Background, 1.0f).r ==
	    doctest::Approx(0x1e / 255.0f)); // #1e1e2e base
	CHECK(
	    PaletteColour(Palette::Mocha, Token::Foreground, 1.0f).g ==
	    doctest::Approx(0xd6 / 255.0f)); // #cdd6f4 text
	CHECK(
	    PaletteColour(Palette::Mocha, Token::Primary, 1.0f).b ==
	    doctest::Approx(0xfa / 255.0f)); // #89b4fa blue
	CHECK(
	    PaletteColour(Palette::Mocha, Token::OnPrimary, 1.0f).b ==
	    doctest::Approx(0x1b / 255.0f)); // #11111b crust, what sits on the blue
	CHECK(
	    PaletteColour(Palette::Mocha, Token::Focus, 1.0f).b ==
	    doctest::Approx(0xfe / 255.0f)); // #b4befe lavender
	CHECK(
	    PaletteColour(Palette::Mocha, Token::Danger, 1.0f).r ==
	    doctest::Approx(0xf3 / 255.0f)); // #f38ba8 red
	CHECK(
	    PaletteColour(Palette::Latte, Token::OnPrimary, 1.0f).r ==
	    doctest::Approx(0xef / 255.0f)); // #eff1f5 base, the light variety writes pale on dark
	CHECK(PaletteName(Palette::Mocha) == "mocha");
	CHECK(PaletteVariants()[0] == Palette::Mocha); // the darkest first, one press to the next step
}


TEST_CASE("Theme: the palette goes to ImGui as written, and every hex lands on the frame as written")
{
	// The path a style colour takes to the frame: ImGui packs it into ImDrawVert::col, 8-bit bytes,
	// by ColorConvertFloat4ToU32; on a UNORM attachment those bytes are stored as they are, and on an
	// sRGB one the UI's fragment stage decodes them in floats (assets/shaders/imgui.frag) and the
	// attachment encodes the result. Both are checked over the 264 channels of the 88 colours the
	// four varieties give the 22 roles. Decoded before the packing, as the style was until
	// 2026-11-28, 55 of them landed off by up to four steps, all at the dark end (#1e1e2e as
	// #1c1c2e, docs/history/devtools-theme.md, F5).
	HeadlessImGui imgui;
	int stored = 0;
	int encoded = 0;
	int channels = 0;
	const auto packed = [](Palette palette, Token token)
	{
		const Rgba colour = PaletteColour(palette, token, 1.0f);
		const ImU32 vertex = ImGui::ColorConvertFloat4ToU32({colour.r, colour.g, colour.b, 1.0f});
		return std::array<std::uint8_t, 3>{
		    static_cast<std::uint8_t>(vertex & 0xff), static_cast<std::uint8_t>((vertex >> 8) & 0xff),
		    static_cast<std::uint8_t>((vertex >> 16) & 0xff)};
	};
	const auto landed = [&packed](Palette palette, Token token, int channel)
	{
		const std::uint8_t byte = packed(palette, token)[static_cast<std::size_t>(channel)];
		return ToByte(EncodeSrgb(DecodeSrgb(static_cast<float>(byte) / 255.0f)));
	};
	for (const Palette palette : PaletteVariants())
		for (std::size_t token = 0; token < kTokenCount; ++token)
			for (const int channel : {0, 1, 2})
			{
				const std::uint8_t byte = ByteOf(palette, Token(token), channel);
				stored += packed(palette, Token(token))[static_cast<std::size_t>(channel)] == byte;
				encoded += landed(palette, Token(token), channel) == byte;
				++channels;
			}
	CHECK(channels == 264); // 22 roles, 4 varieties, 3 channels
	CHECK(stored == 264);
	CHECK(encoded == 264);

	// The landings the record named off the palette, by name: the darkest channels, Mocha's crust and
	// Latte's near-black red of its blue, and the panel grounds of the dark varieties.
	CHECK(landed(Palette::Mocha, Token::Sunken, 0) == 0x11); // #11111b crust, was #0d0d1c
	CHECK(landed(Palette::Mocha, Token::Sunken, 2) == 0x1b);
	CHECK(landed(Palette::Latte, Token::Info, 0) == 0x04);           // #04a5e5 sky, was #00a5e5
	CHECK(landed(Palette::Mocha, Token::Background, 0) == 0x1e);     // #1e1e2e base, was #1c1c2e
	CHECK(landed(Palette::Macchiato, Token::Background, 0) == 0x24); // #24273a base, was #22263b
}


TEST_CASE("Theme: the style names every colour a dev tools window draws with")
{
	HeadlessImGui imgui;
	ImGuiStyle dark;
	ImGui::StyleColorsDark(&dark);
	ApplyTheme(ThemeSettings{});
	ImGuiStyle &style = ImGui::GetStyle();

	std::vector<int> left;
	for (int colour = 0; colour < ImGuiCol_COUNT; ++colour)
	{
		const ImVec4 &applied = style.Colors[colour];
		CHECK(std::isfinite(applied.x));
		CHECK(std::isfinite(applied.y));
		CHECK(std::isfinite(applied.z));
		CHECK(std::isfinite(applied.w));
		CHECK(applied.w >= 0.0f);
		CHECK(applied.w <= 1.0f);
		const ImVec4 &stock = dark.Colors[colour];
		const bool same = applied.x == stock.x && applied.y == stock.y && applied.z == stock.z &&
		                  applied.w == stock.w;
		if (same)
			left.push_back(colour);
	}

	// Not one role is left in the stock theme's colours, which is the point of naming them from
	// the dark style rather than painting over it a few at a time.
	CHECK(left.empty());
}


TEST_CASE("Theme: the metrics are the design's spacing scale, and scale with its root size")
{
	HeadlessImGui imgui;
	ApplyTheme(ThemeSettings{});
	ImGuiStyle &style = ImGui::GetStyle();
	CHECK(style.FontSizeBase == 16.0f);      // text-base
	CHECK(style.WindowPadding.x == 12.0f);   // p-3
	CHECK(style.FramePadding.x == 10.0f);    // px-3
	CHECK(style.FramePadding.y == 6.0f);     // py-1.5, so a control is h-7 on 16 px text
	CHECK(style.ItemSpacing.x == 8.0f);      // gap-2
	CHECK(style.ItemInnerSpacing.x == 6.0f); // gap-1.5
	CHECK(style.IndentSpacing == 16.0f);     // pl-4
	CHECK(style.WindowRounding == 8.0f);     // rounded-lg
	CHECK(style.FrameRounding == 6.0f);      // rounded-md
	CHECK(style.WindowBorderSize == 1.0f);   // border
	CHECK(style.ScrollbarSize < 14.0f);      // the web scrollbar, not ImGui's
	CHECK(style.DisabledAlpha == 0.5f);      // disabled:opacity-50
	CHECK(style.TabBarOverlineSize == 2.0f); // border-b-2 under a chosen tab
	CHECK(style.AntiAliasedLines);
	CHECK(style.TreeLinesFlags == ImGuiTreeNodeFlags_DrawLinesFull);

	// The scale is the design's root font size, so everything named above moves together.
	ThemeSettings scaled;
	scaled.uiScale = 1.5f;
	ApplyTheme(scaled);
	CHECK(style.FontSizeBase == 24.0f);
	CHECK(style.WindowPadding.x == 18.0f);
	CHECK(style.FramePadding.x == 15.0f);
	CHECK(style.WindowRounding == 12.0f);
	CHECK(style.ScrollbarSize == 15.0f);

	// A scale of nothing is a dev's mistake, not a style with no size to draw at.
	ThemeSettings broken;
	broken.uiScale = 0.0f;
	ApplyTheme(broken);
	CHECK(style.FontSizeBase == 16.0f);
	CHECK(style.WindowPadding.x == 12.0f);
}


TEST_CASE("Theme: a console change is applied once, and a new context is never skipped")
{
	{
		HeadlessImGui imgui;
		ThemeSettings settings;
		CHECK(ApplyThemeIfChanged(settings)); // this context has no theme yet
		CHECK_FALSE(ApplyThemeIfChanged(settings));
		settings.palette = Palette::Macchiato;
		CHECK(ApplyThemeIfChanged(settings));
		CHECK(AppliedTheme().palette == Palette::Macchiato);
		settings.uiScale = 1.25f;
		CHECK(ApplyThemeIfChanged(settings));
		CHECK(AppliedTheme().uiScale == 1.25f);
		CHECK_FALSE(ApplyThemeIfChanged(settings));
	}

	// A run that tears its renderer down and builds another: the new context has no style, and
	// the remembered settings must not be taken as applied to it.
	HeadlessImGui fresh;
	CHECK(ApplyThemeIfChanged(ThemeSettings{})); // the stock style is padding 8, not 12
	CHECK(ImGui::GetStyle().WindowPadding.x == 12.0f);
	CHECK_FALSE(ApplyThemeIfChanged(ThemeSettings{}));

	// The case a cache keyed by the context's address cannot tell apart: a context made after the
	// themed one reuses its address (the general allocator hands the same block back) and starts
	// stock. The stock start is checked, because it is what makes the address a lie: measured, three
	// recreated contexts at the same address each began at padding 8 and rounding 0.
	ImGuiContext *themedAddress = nullptr;
	{
		HeadlessImGui themed;
		themedAddress = ImGui::GetCurrentContext();
		ApplyThemeIfChanged(ThemeSettings{}); // a context made now: the style is what matters
		CHECK(ImGui::GetStyle().WindowPadding.x == 12.0f);
		CHECK(ImGui::GetStyle().WindowRounding == 8.0f);   // rounded-lg
		CHECK(ImGui::GetStyle().WindowBorderSize == 1.0f); // the hairline
		CHECK_FALSE(ApplyThemeIfChanged(ThemeSettings{})); // and this context says it is themed
	}
	HeadlessImGui recreated;
	MESSAGE(
	    "the recreated context reuses the themed one's address: "
	    << (ImGui::GetCurrentContext() == themedAddress));
	CHECK(ImGui::GetStyle().WindowPadding.x == 8.0f); // the stock padding, in the reused block
	CHECK(ImGui::GetStyle().WindowRounding == 0.0f);  // and none of the theme's rounding
	ApplyThemeIfChanged(ThemeSettings{});
	CHECK(ImGui::GetStyle().WindowPadding.x == 12.0f); // p-3, not the stock 8
	CHECK(ImGui::GetStyle().WindowRounding == 8.0f);   // rounded-lg
	CHECK(ImGui::GetStyle().WindowBorderSize == 1.0f); // the hairline
	CHECK(ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w == doctest::Approx(0.97f));
}


TEST_CASE("Theme: a console line has a colour for every kind and every severity")
{
	const std::array kinds{
	    ConsoleLine::Kind::Log, ConsoleLine::Kind::Input, ConsoleLine::Kind::Output,
	    ConsoleLine::Kind::Error};
	const std::array severities{Core::Severity::Trace,   Core::Severity::Debug, Core::Severity::Info,
	                            Core::Severity::Warning, Core::Severity::Error, Core::Severity::Fatal};
	std::vector<Token> roles;
	for (const ConsoleLine::Kind kind : kinds)
		for (const Core::Severity severity : severities)
		{
			ConsoleLine line;
			line.kind = kind;
			line.severity = severity;
			const Token token = LineToken(line);
			roles.push_back(token);
			CHECK(token < Token::Count);
		}

	// The words of the design: what was sent, what it cost, what went wrong.
	ConsoleLine input;
	input.kind = ConsoleLine::Kind::Input;
	CHECK(LineToken(input) == Token::Primary);
	ConsoleLine refused;
	refused.kind = ConsoleLine::Kind::Error;
	CHECK(LineToken(refused) == Token::Danger);
	ConsoleLine warn;
	warn.kind = ConsoleLine::Kind::Log;
	warn.severity = Core::Severity::Warning;
	CHECK(LineToken(warn) == Token::Warning);
	ConsoleLine fatal;
	fatal.kind = ConsoleLine::Kind::Log;
	fatal.severity = Core::Severity::Fatal;
	CHECK(LineToken(fatal) == Token::Danger);
	ConsoleLine debug;
	debug.kind = ConsoleLine::Kind::Log;
	debug.severity = Core::Severity::Debug;
	CHECK(LineToken(debug) == Token::Faint);
	ConsoleLine answer;
	answer.kind = ConsoleLine::Kind::Output;
	CHECK(LineToken(answer) == Token::Foreground);

	// ColourOf reads the palette the style was built from, so a view cannot drift from the style.
	{
		HeadlessImGui imgui;
		ThemeSettings settings;
		settings.palette = Palette::Frappe;
		ApplyTheme(settings);
		CHECK(ColourOf(Token::Danger).r == doctest::Approx(0xe7 / 255.0f));
	}
}


TEST_CASE("Theme: a scale typed into the console cannot drive a metric outside what ImGui allows")
{
	// The bytes are declared before the context, so they outlive it, as FontFaces.h requires.
	std::vector<std::byte> scalable = ReadFace("Roboto-Medium.ttf");
	REQUIRE(scalable.size() > 100);
	HeadlessImGui imgui;
	ImGuiIO &io = ImGui::GetIO();

	// A face that rasterises at any size, as the app's do, because the frame below is drawn at the
	// sizes the clamped scales ask for: ImGui's embedded face is ProggyClean, a bitmap face with one
	// native size of 13 px, and a frame at 4 px asks it for glyphs it cannot bake — measured, the
	// bake in a test, not in the app, which has real faces loaded.
	ImFontConfig face{};
	face.FontDataOwnedByAtlas = false; // the bytes are the test's, as they are the backend's
	REQUIRE(
	    io.Fonts->AddFontFromMemoryTTF(
	        scalable.data(), static_cast<int>(scalable.size()), 16.0f, &face) != nullptr);

	// The atlas is built before the frames below, as a renderer's backend does: ImGui asks for it by
	// name, and a frame without it is a user error it reports by asserting.
	REQUIRE(io.Fonts->Build());
	unsigned char *pixels = nullptr;
	int width = 0;
	int height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	REQUIRE(pixels != nullptr);
	RegisterThemeTypes();
	Console console;
	ThemeSettings theme;
	console.BindVariables("dev", theme);

	// The band is measured: 0.01 gives WindowMinSize {0.8, 0.6}, and NewFrame asserts "Invalid style
	// setting!"; the console reaches it, the one path the dev tools' slider cannot bound. A scale of
	// 0, of a negative, of 1e9 and of a word that parses as a float are the same class of mistake.
	for (const char *value : {"0.01", "0", "-4", "1e9", "nan", "1"})
	{
		INFO(value);
		console.Execute("dev.ui_scale " + std::string{value});
		MESSAGE("dev.ui_scale " << value << " styles at " << theme.uiScale);
		CHECK(ApplyThemeIfChanged(theme));
		const ImGuiStyle &style = ImGui::GetStyle();
		CHECK(style.WindowMinSize.x >= 1.0f);
		CHECK(style.WindowMinSize.y >= 1.0f);
		CHECK(style.WindowPadding.x >= 1.0f);
		CHECK(std::isfinite(style.FontSizeBase));
		CHECK(style.FontSizeBase >= 1.0f);
		CHECK(std::isfinite(style.ItemSpacing.x));
		ImGui::NewFrame(); // the assert the probe reached is ImGui's own, and it is here
		ImGui::EndFrame();
	}
}


TEST_CASE(
    "Theme: a face named for its file answers to the console's mono rule, and an unnamed one does not")
{
	// Loaded as the render backend loads them, from bytes, because the dev tools read through the
	// mounts: ImGui's memory loader supplies no name of its own — only AddFontFromFileTTF derives
	// one from the file (imgui_draw.cpp) — so the names FontFaces.cpp writes are all the rule has.
	// The bytes are declared before the context, so they outlive it, as FontFaces.h requires.
	std::vector<std::byte> hack = ReadFace("Hack-Regular.ttf");
	std::vector<std::byte> roboto = ReadFace("Roboto-Medium.ttf");
	HeadlessImGui imgui;
	ImGuiIO &io = ImGui::GetIO();
	REQUIRE(hack.size() > 100); // ImGui's own heuristic for a font file
	REQUIRE(roboto.size() > 100);
	ImFontConfig named{};
	std::snprintf(named.Name, sizeof(named.Name), "%s", "Hack-Regular.ttf");
	ImFontConfig unnamed{};
	named.FontDataOwnedByAtlas = false; // the bytes are the test's, as they are the backend's
	unnamed.FontDataOwnedByAtlas = false;
	ImFont *const mono =
	    io.Fonts->AddFontFromMemoryTTF(hack.data(), static_cast<int>(hack.size()), 16.0f, &named);
	ImFont *const ui =
	    io.Fonts->AddFontFromMemoryTTF(roboto.data(), static_cast<int>(roboto.size()), 16.0f, &unnamed);
	REQUIRE(mono != nullptr);
	REQUIRE(ui != nullptr);
	REQUIRE(io.Fonts->Build());

	MESSAGE(
	    "mono: " << std::string{mono->GetDebugName()} << ", unnamed: \""
	             << std::string{ui->GetDebugName()} << "\"");
	CHECK(MonoFont() == mono);

	// The two facts the render module's own test proves its loader settles, recorded here so the
	// rule's contract is read against them: a memory load has no name of its own, and the face drawn
	// with is the first one added, not the last (io.FontDefault is unset in this context).
	CHECK(std::string{ui->GetDebugName()}.empty());
	ImGui::NewFrame();
	CHECK(ImGui::GetFont() == mono);
	ImGui::EndFrame();

	// The console draws a frame with the mono face pushed for its lines, which is what the design
	// asks for, and the input line in mono with it.
	Console console;
	console.Print("dock refused");
	ConsoleViewState state;
	ThemeSettings theme;
	bool open = true;
	ImGui::NewFrame();
	DrawConsole(console, state, theme, &open); // Begin and End inside it, as a frame loop calls it
	ImGui::Render();
	CHECK(ImGui::GetDrawData()->CmdListsCount > 0);

	// How a run with no fonts draws, and every other test: no mono face, so the current face.
	HeadlessImGui none;
	CHECK(MonoFont() == nullptr);
}


TEST_CASE("Theme: the game UI's stylesheets colour with the dev tools' palette, Mocha")
{
	// Every colour the engine's screens are drawn in is one of Mocha's roles, so the game UI and the
	// tools cannot drift apart; an alpha, where one is written, is the stylesheet's own.
	const auto &mocha = PaletteTokens(Palette::Mocha);
	int colours = 0;
	for (const char *name : {"base.rcss", "saves.rcss"})
	{
		std::ifstream stream{std::filesystem::path{JETTISON_ASSET_SOURCE_DIR "/ui/"} / name};
		REQUIRE(stream);
		const std::string text{std::istreambuf_iterator<char>{stream}, {}};
		for (std::size_t at = text.find('#'); at != std::string::npos; at = text.find('#', at + 1))
		{
			std::size_t length = 0;
			while (at + 1 + length < text.size() &&
			       std::isxdigit(static_cast<unsigned char>(text[at + 1 + length])) != 0)
				++length;
			if (length != 6 && length != 8)
				continue; // an id selector, as #window is
			const auto byte = [&](std::size_t offset)
			{
				return static_cast<std::uint8_t>(
				    std::stoi(text.substr(at + 1 + offset, 2), nullptr, 16));
			};
			const Rgb rgb{byte(0), byte(2), byte(4)};
			INFO(name << ": " << text.substr(at, length + 1));
			CHECK(
			    std::ranges::any_of(
			        mocha, [&](const Rgb &role)
			        { return role.r == rgb.r && role.g == rgb.g && role.b == rgb.b; }));
			++colours;
		}
	}
	CHECK(colours > 0);
}
