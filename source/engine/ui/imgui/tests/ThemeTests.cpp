#include <jettison/devtools/Console.h>
#include <jettison/devtools/Theme.h>
#include <jettison/devtools/Views.h>

#include <jettison/reflect/Reflect.h>

#include <doctest/doctest.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
		// fixtures has to say whose frame this is; and it is destroyed by pointer, because
		// DestroyContext(nullptr) takes whichever context is current — with the general allocator
		// handing the same address back (measured), two fixtures would destroy one context twice.
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


// sRGB's encoding, the inverse of DecodeSrgb, so the theme's decode is checked by the conversion
// the attachment does: what the theme hands over, encoded again, is the palette's byte.
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
			const Rgba colour = PaletteColour(palette, Token(token), 0.75f, false);
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
	    PaletteColour(Palette::Mocha, Token::Background, 1.0f, false).r ==
	    doctest::Approx(0x1e / 255.0f)); // #1e1e2e base
	CHECK(
	    PaletteColour(Palette::Mocha, Token::Foreground, 1.0f, false).g ==
	    doctest::Approx(0xd6 / 255.0f)); // #cdd6f4 text
	CHECK(
	    PaletteColour(Palette::Mocha, Token::Primary, 1.0f, false).b ==
	    doctest::Approx(0xfa / 255.0f)); // #89b4fa blue
	CHECK(
	    PaletteColour(Palette::Mocha, Token::OnPrimary, 1.0f, false).b ==
	    doctest::Approx(0x1b / 255.0f)); // #11111b crust, what sits on the blue
	CHECK(
	    PaletteColour(Palette::Mocha, Token::Focus, 1.0f, false).b ==
	    doctest::Approx(0xfe / 255.0f)); // #b4befe lavender
	CHECK(
	    PaletteColour(Palette::Mocha, Token::Danger, 1.0f, false).r ==
	    doctest::Approx(0xf3 / 255.0f)); // #f38ba8 red
	CHECK(
	    PaletteColour(Palette::Latte, Token::OnPrimary, 1.0f, false).r ==
	    doctest::Approx(0xef / 255.0f)); // #eff1f5 base, the light variety writes pale on dark
	CHECK(PaletteName(Palette::Mocha) == "mocha");
	CHECK(PaletteVariants()[0] == Palette::Mocha); // the darkest first, one press to the next step
}


TEST_CASE("Theme: the palette goes to an sRGB attachment in linear light, so a hex reads as written")
{
	// The pairs the style is built from, measured: sRGB's darkest quarters decode far below the
	// byte they come from, which is why feeding the byte itself lands lighter than the palette.
	CHECK(DecodeSrgb(0x1e / 255.0f) == doctest::Approx(0.012983f).epsilon(0.001f)); // base
	CHECK(DecodeSrgb(0x11 / 255.0f) == doctest::Approx(0.005605f).epsilon(0.001f)); // crust
	CHECK(DecodeSrgb(0xcd / 255.0f) == doctest::Approx(0.610496f).epsilon(0.001f)); // text
	CHECK(DecodeSrgb(0x00 / 255.0f) == 0.0f);
	CHECK(DecodeSrgb(0xff / 255.0f) == doctest::Approx(1.0f));
	// The linear part, under sRGB's knee, where a power of a near-zero loses its precision.
	CHECK(DecodeSrgb(0.04045f / 2.0f) == doctest::Approx(0.04045f / 2.0f / 12.92f));

	HeadlessImGui imgui;
	constexpr float kAlpha = 0.97f;
	const Rgba linear = PaletteColour(Palette::Mocha, Token::Background, kAlpha, true);
	const Rgba authored = PaletteColour(Palette::Mocha, Token::Background, kAlpha, false);
	CHECK(ToByte(authored.r) == 0x1e);
	// What the attachment does to what the theme hands it: the hex again, and not lighter.
	for (const int channel : {0, 1, 2})
		CHECK(
		    ToByte(EncodeSrgb(
		        channel == 0   ? linear.r
		        : channel == 1 ? linear.g
		                       : linear.b)) == ByteOf(Palette::Mocha, Token::Background, channel));
	CHECK(linear.r < authored.r); // the naive feed would sit above the palette

	// The vertex's packing is in the path, so the test that claims this contract goes through it: a
	// style colour is quantised into ImDrawVert::col, 8-bit bytes, by ColorConvertFloat4ToU32, and
	// the attachment encodes that. Measured over the 264 channels of the 88 colours the four
	// varieties give the 22 roles, and recorded in docs/architecture.md ("UI"): 209 channels land on
	// the palette byte, 33 are one step away, 15 two, 2 three, 5 four, so the bound is 4. The
	// four-step cases are the darkest channels, Mocha's crust and Latte's near-black blue; a one-step
	// case reaches the mid tones of the three lighter varieties, and Mocha's mid and light tones,
	// which is what a captured frame reads, land exact. The bound is not the contract: the landings
	// the document names are asserted by name below.
	int worst = 0;
	std::array<int, 5> steps{}; // how many channels survive at 0, 1, 2, 3 and 4 byte-steps
	int channels = 0;
	for (const Palette palette : PaletteVariants())
		for (std::size_t token = 0; token < kTokenCount; ++token)
		{
			const Rgba decoded = PaletteColour(palette, Token(token), 1.0f, true);
			const ImU32 vertex =
			    ImGui::ColorConvertFloat4ToU32({decoded.r, decoded.g, decoded.b, 1.0f});
			const std::array<std::uint8_t, 4> packed{
			    static_cast<std::uint8_t>(vertex & 0xff),
			    static_cast<std::uint8_t>((vertex >> 8) & 0xff),
			    static_cast<std::uint8_t>((vertex >> 16) & 0xff),
			    static_cast<std::uint8_t>((vertex >> 24) & 0xff)};
			for (const int channel : {0, 1, 2})
			{
				const float asFloat =
				    static_cast<float>(packed[static_cast<std::size_t>(channel)]) / 255.0f;
				const int deviation = std::abs(
				    static_cast<int>(ToByte(EncodeSrgb(asFloat))) -
				    static_cast<int>(ByteOf(palette, Token(token), channel)));
				worst = std::max(worst, deviation);
				++steps[static_cast<std::size_t>(std::min(deviation, 4))];
				++channels;
			}
		}
	MESSAGE("worst byte-step deviation through the vertex packing: " << worst);
	CHECK(worst <= 4);
	// The distribution the document quotes, counted here rather than asserted from an example.
	CHECK(channels == 264); // 22 roles, 4 varieties, 3 channels
	CHECK(steps[0] == 209);
	CHECK(steps[1] == 33);
	CHECK(steps[2] == 15);
	CHECK(steps[3] == 2);
	CHECK(steps[4] == 5);

	// The landings the document names, by name and not by the bound. A colour goes through the
	// packing the way a draw list carries it, then through the attachment's encode, and the byte that
	// reaches the framebuffer is what is asserted.
	const auto landed = [](Palette palette, Token token, int channel) -> std::uint8_t
	{
		const Rgba decoded = PaletteColour(palette, token, 1.0f, true);
		const ImU32 vertex = ImGui::ColorConvertFloat4ToU32({decoded.r, decoded.g, decoded.b, 1.0f});
		const std::array<std::uint8_t, 3> bytes{
		    static_cast<std::uint8_t>(vertex & 0xff), static_cast<std::uint8_t>((vertex >> 8) & 0xff),
		    static_cast<std::uint8_t>((vertex >> 16) & 0xff)};
		return ToByte(
		    EncodeSrgb(static_cast<float>(bytes[static_cast<std::size_t>(channel)]) / 255.0f));
	};
	// The four-step cases, the darkest channels of all: Mocha's crust, on which the primary colour's
	// text sits, and Latte's near-black red of its blue.
	CHECK(landed(Palette::Mocha, Token::Sunken, 0) == 0x0d); // #11111b crust
	CHECK(landed(Palette::Mocha, Token::Sunken, 2) == 0x1c); // ... to #0d0d1c
	CHECK(landed(Palette::Latte, Token::Info, 0) == 0x00);   // #04a5e5 blue, to #00a5e5
	// Two steps, at the panel grounds of the dark varieties: Mocha's base and Macchiato's.
	CHECK(landed(Palette::Mocha, Token::Background, 0) == 0x1c);     // #1e1e2e to #1c1c2e
	CHECK(landed(Palette::Macchiato, Token::Background, 0) == 0x22); // #24273a
	// One step, and it reaches the mid tones of the lighter varieties: the colour the reviewer named.
	CHECK(landed(Palette::Macchiato, Token::Subtle, 2) == 0x63); // #494d64 to #494d63
	CHECK(landed(Palette::Frappe, Token::Subtle, 1) == 0x56);    // #51576d to #51566d
	CHECK(landed(Palette::Latte, Token::Foreground, 0) == 0x4b); // #4c4f69 to #4b4f69
	// Mocha's mid and light tones, the two the captured frame measures, land on the palette itself.
	CHECK(landed(Palette::Mocha, Token::Foreground, 0) == ByteOf(Palette::Mocha, Token::Foreground, 0));
	CHECK(landed(Palette::Mocha, Token::Subtle, 2) == ByteOf(Palette::Mocha, Token::Subtle, 2));
	CHECK(
	    landed(Palette::Macchiato, Token::Foreground, 2) ==
	    ByteOf(Palette::Macchiato, Token::Foreground, 2)); // #cad3f5, the live switch's text
}


TEST_CASE("Theme: the style names every colour a dev tools window draws with")
{
	HeadlessImGui imgui;
	ImGuiStyle dark;
	ImGui::StyleColorsDark(&dark);
	ApplyTheme(ThemeSettings{}, true);
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
	ApplyTheme(ThemeSettings{}, true);
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
	ApplyTheme(scaled, true);
	CHECK(style.FontSizeBase == 24.0f);
	CHECK(style.WindowPadding.x == 18.0f);
	CHECK(style.FramePadding.x == 15.0f);
	CHECK(style.WindowRounding == 12.0f);
	CHECK(style.ScrollbarSize == 15.0f);
	// A scale of nothing is a dev's mistake, not a style with no size to draw at.
	ThemeSettings broken;
	broken.uiScale = 0.0f;
	ApplyTheme(broken, true);
	CHECK(style.FontSizeBase == 16.0f);
	CHECK(style.WindowPadding.x == 12.0f);
}


TEST_CASE("Theme: a console change is applied once, and a new context is never skipped")
{
	{
		HeadlessImGui imgui;
		ThemeSettings settings;
		CHECK(ApplyThemeIfChanged(settings, true)); // this context has no theme yet
		CHECK_FALSE(ApplyThemeIfChanged(settings, true));
		CHECK(ApplyThemeIfChanged(settings, false)); // the attachment changed under it
		settings.palette = Palette::Macchiato;
		CHECK(ApplyThemeIfChanged(settings, false));
		CHECK(AppliedTheme().palette == Palette::Macchiato);
		settings.uiScale = 1.25f;
		CHECK(ApplyThemeIfChanged(settings, false));
		CHECK(AppliedTheme().uiScale == 1.25f);
		CHECK_FALSE(ApplyThemeIfChanged(settings, false));
	}
	// A run that tears its renderer down and builds another: the new context has no style, and
	// the remembered settings must not be taken as applied to it.
	HeadlessImGui fresh;
	CHECK(ApplyThemeIfChanged(ThemeSettings{}, true)); // the stock style is padding 8, not 12
	CHECK(ImGui::GetStyle().WindowPadding.x == 12.0f);
	CHECK_FALSE(ApplyThemeIfChanged(ThemeSettings{}, true));

	// The case a cache keyed by the context's address cannot tell apart. Measured with the fixture
	// owning its context: a context made after the themed one reuses the themed one's address — the
	// general allocator hands the same block back — and starts stock, because ImGuiStyle's
	// constructor writes the stock padding, rounding and colours over whatever the last context left
	// in the block. So an address says nothing about the style it holds, which is the standard's
	// point, and the app's question is "is the style I am drawing with themed?", read from the
	// style; an address-keyed cache is the other way to answer it, and answers "already themed" here.
	// The stock start is checked, because it is what makes the address a lie: measured, three
	// recreated contexts at the same address each began at padding 8 and rounding 0.
	ImGuiContext *themedAddress = nullptr;
	{
		HeadlessImGui themed;
		themedAddress = ImGui::GetCurrentContext();
		ApplyThemeIfChanged(ThemeSettings{}, true); // a context made now: the style is what matters
		CHECK(ImGui::GetStyle().WindowPadding.x == 12.0f);
		CHECK(ImGui::GetStyle().WindowRounding == 8.0f);         // rounded-lg
		CHECK(ImGui::GetStyle().WindowBorderSize == 1.0f);       // the hairline
		CHECK_FALSE(ApplyThemeIfChanged(ThemeSettings{}, true)); // and this context says it is themed
	}
	HeadlessImGui recreated;
	MESSAGE(
	    "the recreated context reuses the themed one's address: "
	    << (ImGui::GetCurrentContext() == themedAddress));
	CHECK(ImGui::GetStyle().WindowPadding.x == 8.0f); // the stock padding, in the reused block
	CHECK(ImGui::GetStyle().WindowRounding == 0.0f);  // and none of the theme's rounding
	ApplyThemeIfChanged(ThemeSettings{}, true);
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
		ApplyTheme(settings, false);
		CHECK(ColourOf(Token::Danger).r == doctest::Approx(0xe7 / 255.0f));
		ApplyTheme(settings, true);
		CHECK(ColourOf(Token::Danger).r == doctest::Approx(DecodeSrgb(0xe7 / 255.0f)));
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
	// The band is a measured one, not a guess: 0.01 gives WindowMinSize {0.8, 0.6}, and NewFrame
	// asserts "Invalid style setting!" — the cross-review's probe reached it through the console, the
	// one path the dev tools' own slider cannot bound. A scale of 0, of a negative, of 1e9 and of a
	// word that parses as a float are the same class of mistake, and the style is what has to stand.
	for (const char *value : {"0.01", "0", "-4", "1e9", "nan", "1"})
	{
		INFO(value);
		console.Execute("dev.ui_scale " + std::string{value});
		MESSAGE("dev.ui_scale " << value << " styles at " << theme.uiScale);
		CHECK(ApplyThemeIfChanged(theme, true));
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
