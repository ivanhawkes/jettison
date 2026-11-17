#include <jettison/devtools/Theme.h>

#include <jettison/devtools/Console.h>

#include <imgui.h>

#include <jettison/reflect/Reflect.h>

#include <algorithm>
#include <cmath>

namespace Jettison::DevTools
{
namespace
{
// The palette, as Catppuccin publishes it (palette 1.8.0, https://catppuccin.com): one colour per
// role, in the order Token names them. The roles are the stylesheet's words, so the tables differ
// only in the hex and a variety is data. Spelt as the palette spells it, beside each.
constexpr std::array<Rgb, kTokenCount> kLatte{{
    {0xef, 0xf1, 0xf5}, // Background   #eff1f5 base
    {0xe6, 0xe9, 0xef}, // Surface      #e6e9ef mantle
    {0xdc, 0xe0, 0xe8}, // Sunken       #dce0e8 crust
    {0x4c, 0x4f, 0x69}, // Foreground   #4c4f69 text
    {0x6c, 0x6f, 0x85}, // Muted        #6c6f85 subtext0
    {0x9c, 0xa0, 0xb0}, // Faint        #9ca0b0 overlay0
    {0xcc, 0xd0, 0xda}, // Border       #ccd0da surface0
    {0xac, 0xb0, 0xbe}, // BorderStrong #acb0be surface2
    {0xbc, 0xc0, 0xcc}, // Subtle       #bcc0cc surface1
    {0xac, 0xb0, 0xbe}, // Hover        #acb0be surface2
    {0x1e, 0x66, 0xf5}, // Primary      #1e66f5 blue
    {0xef, 0xf1, 0xf5}, // OnPrimary    #eff1f5 base, the light palette writes pale on dark
    {0x72, 0x87, 0xfd}, // Focus        #7287fd lavender
    {0x40, 0xa0, 0x2b}, // Success      #40a02b green
    {0xdf, 0x8e, 0x1d}, // Warning      #df8e1d yellow
    {0xd2, 0x0f, 0x39}, // Danger       #d20f39 red
    {0x04, 0xa5, 0xe5}, // Info         #04a5e5 sky
    {0x88, 0x39, 0xef}, // Accent       #8839ef mauve
    {0xfe, 0x64, 0x0b}, // Peach        #fe640b peach
    {0x17, 0x92, 0x99}, // Teal         #179299 teal
    {0xdd, 0x78, 0x78}, // Flamingo     #dd7878 flamingo
    {0xea, 0x76, 0xcb}, // Pink         #ea76cb pink
}};

constexpr std::array<Rgb, kTokenCount> kFrappe{{
    {0x30, 0x34, 0x46}, // Background   #303446 base
    {0x29, 0x2c, 0x3c}, // Surface      #292c3c mantle
    {0x23, 0x26, 0x34}, // Sunken       #232634 crust
    {0xc6, 0xd0, 0xf5}, // Foreground   #c6d0f5 text
    {0xa5, 0xad, 0xce}, // Muted        #a5adce subtext0
    {0x73, 0x79, 0x94}, // Faint        #737994 overlay0
    {0x41, 0x45, 0x59}, // Border       #414559 surface0
    {0x62, 0x68, 0x80}, // BorderStrong #626880 surface2
    {0x51, 0x57, 0x6d}, // Subtle       #51576d surface1
    {0x62, 0x68, 0x80}, // Hover        #626880 surface2
    {0x8c, 0xaa, 0xee}, // Primary      #8caaee blue
    {0x23, 0x26, 0x34}, // OnPrimary    #232634 crust, the dark varieties write dark on pale
    {0xba, 0xbb, 0xf1}, // Focus        #babbf1 lavender
    {0xa6, 0xd1, 0x89}, // Success      #a6d189 green
    {0xe5, 0xc8, 0x90}, // Warning      #e5c890 yellow
    {0xe7, 0x82, 0x84}, // Danger       #e78284 red
    {0x99, 0xd1, 0xdb}, // Info         #99d1db sky
    {0xca, 0x9e, 0xe6}, // Accent       #ca9ee6 mauve
    {0xef, 0x9f, 0x76}, // Peach        #ef9f76 peach
    {0x81, 0xc8, 0xbe}, // Teal         #81c8be teal
    {0xee, 0xbe, 0xbe}, // Flamingo     #eebebe flamingo
    {0xf4, 0xb8, 0xe4}, // Pink         #f4b8e4 pink
}};

constexpr std::array<Rgb, kTokenCount> kMacchiato{{
    {0x24, 0x27, 0x3a}, // Background   #24273a base
    {0x1e, 0x20, 0x30}, // Surface      #1e2030 mantle
    {0x18, 0x19, 0x26}, // Sunken       #181926 crust
    {0xca, 0xd3, 0xf5}, // Foreground   #cad3f5 text
    {0xa5, 0xad, 0xcb}, // Muted        #a5adcb subtext0
    {0x6e, 0x73, 0x8d}, // Faint        #6e738d overlay0
    {0x36, 0x3a, 0x4f}, // Border       #363a4f surface0
    {0x5b, 0x60, 0x78}, // BorderStrong #5b6078 surface2
    {0x49, 0x4d, 0x64}, // Subtle       #494d64 surface1
    {0x5b, 0x60, 0x78}, // Hover        #5b6078 surface2
    {0x8a, 0xad, 0xf4}, // Primary      #8aadf4 blue
    {0x18, 0x19, 0x26}, // OnPrimary    #181926 crust
    {0xb7, 0xbd, 0xf8}, // Focus        #b7bdf8 lavender
    {0xa6, 0xda, 0x95}, // Success      #a6da95 green
    {0xee, 0xd4, 0x9f}, // Warning      #eed49f yellow
    {0xed, 0x87, 0x96}, // Danger       #ed8796 red
    {0x91, 0xd7, 0xe3}, // Info         #91d7e3 sky
    {0xc6, 0xa0, 0xf6}, // Accent       #c6a0f6 mauve
    {0xf5, 0xa9, 0x7f}, // Peach        #f5a97f peach
    {0x8b, 0xd5, 0xca}, // Teal         #8bd5ca teal
    {0xf0, 0xc6, 0xc6}, // Flamingo     #f0c6c6 flamingo
    {0xf5, 0xbd, 0xe6}, // Pink         #f5bde6 pink
}};

constexpr std::array<Rgb, kTokenCount> kMocha{{
    {0x1e, 0x1e, 0x2e}, // Background   #1e1e2e base
    {0x18, 0x18, 0x25}, // Surface      #181825 mantle
    {0x11, 0x11, 0x1b}, // Sunken       #11111b crust
    {0xcd, 0xd6, 0xf4}, // Foreground   #cdd6f4 text
    {0xa6, 0xad, 0xc8}, // Muted        #a6adc8 subtext0
    {0x6c, 0x70, 0x86}, // Faint        #6c7086 overlay0
    {0x31, 0x32, 0x44}, // Border       #313244 surface0
    {0x58, 0x5b, 0x70}, // BorderStrong #585b70 surface2
    {0x45, 0x47, 0x5a}, // Subtle       #45475a surface1
    {0x58, 0x5b, 0x70}, // Hover        #585b70 surface2
    {0x89, 0xb4, 0xfa}, // Primary      #89b4fa blue
    {0x11, 0x11, 0x1b}, // OnPrimary    #11111b crust
    {0xb4, 0xbe, 0xfe}, // Focus        #b4befe lavender
    {0xa6, 0xe3, 0xa1}, // Success      #a6e3a1 green
    {0xf9, 0xe2, 0xaf}, // Warning      #f9e2af yellow
    {0xf3, 0x8b, 0xa8}, // Danger       #f38ba8 red
    {0x89, 0xdc, 0xeb}, // Info         #89dceb sky
    {0xcb, 0xa6, 0xf7}, // Accent       #cba6f7 mauve
    {0xfa, 0xb3, 0x87}, // Peach        #fab387 peach
    {0x94, 0xe2, 0xd5}, // Teal         #94e2d5 teal
    {0xf2, 0xcd, 0xcd}, // Flamingo     #f2cdcd flamingo
    {0xf5, 0xc2, 0xe7}, // Pink         #f5c2e7 pink
}};

constexpr std::array<std::pair<Palette, std::string_view>, 4> kPalettes{{
    {Palette::Latte, "latte"},
    {Palette::Frappe, "frappe"},
    {Palette::Macchiato, "macchiato"},
    {Palette::Mocha, "mocha"},
}};

constexpr std::array<Palette, 4> kVariants{
    Palette::Mocha, Palette::Macchiato, Palette::Frappe, Palette::Latte};


const std::array<Rgb, kTokenCount> &Tokens(Palette palette)
{
	switch (palette)
	{
		case Palette::Latte: return kLatte;
		case Palette::Frappe: return kFrappe;
		case Palette::Macchiato: return kMacchiato;
		case Palette::Mocha: return kMocha;
	}
	return kMocha;
}

// The design's body text, Tailwind's text-base of 16 px, and its spacing scale of 0.25 rem, which
// the metrics are stated in: kScale is Tailwind's gap-2 and kFrameX its px-3, so a web developer
// reads the design in the code and not in a translation table. Every one is multiplied by
// ThemeSettings::uiScale, which is the design's root font size.
constexpr float kFont = 16.0f;
constexpr float kPanelPadding = 12.0f; // p-3, inside a panel
constexpr float kGap = 8.0f;           // gap-2, between controls
constexpr float kGapInner = 6.0f;      // gap-1.5, a control's own parts
constexpr float kFrameX = 10.0f;       // px-3, a control's horizontal padding
constexpr float kFrameY = 6.0f;        // py-1.5, its vertical: a 28 px control on 16 px text, h-7
constexpr float kCellX = 10.0f;        // a table cell, px-3
constexpr float kCellY = 4.0f;         // and py-1
constexpr float kIndent = 16.0f;       // pl-4, a tree's step
constexpr float kRound = 8.0f;         // rounded-lg, a panel
constexpr float kRoundFrame = 6.0f;    // rounded-md, a control
constexpr float kBorder = 1.0f;        // border, the hairline
constexpr float kScrollBar = 10.0f;    // a web scrollbar, w-2.5, not ImGui's 14
constexpr float kScrollGap = 3.0f;     // m-0.5, the grab's inset from the trough
constexpr float kOverline = 2.0f;      // border-b-2, the line under a chosen tab
constexpr float kCaret = 2.0f;         // a text field's caret

// The style's translucency. A panel is a little see-through, as a card over a 3D view is in the
// designs this is drawn against; a menu, a window that must hide what is under it, is not.
constexpr float kPanelAlpha = 0.97f;
constexpr float kSunkenAlpha = 0.60f;
constexpr float kSubtleAlpha = 0.35f;   // odd table rows, as bg-muted/50 is
constexpr float kDisabledAlpha = 0.50f; // disabled:opacity-50

// The scale the style is built at: the settings' own, kept inside the band a style survives. The
// band is a measured one, not a taste: a scale of 0.01 gives WindowMinSize {0.8, 0.6}, and
// ImGui::NewFrame asserts "Invalid style setting!" (the cross-review's probe reached this through
// `dev.ui_scale 0.01`, which the dev tools' slider never offers); a non-finite one poisons every
// metric. A scale that is not a positive number is a dev's mistake, and the design's root size is
// the answer to a mistake, as it has been. The clamp is here, where the styles are built, because
// the console's variable, the slider and a test are three paths and only one of them can be
// trusted to have a band.
constexpr float kScaleFloor = 0.25f;
constexpr float kScaleCeiling = 4.0f;


float ScaleFor(const ThemeSettings &settings)
{
	if (!std::isfinite(settings.uiScale) || settings.uiScale <= 0.0f)
		return 1.0f; // the design's root size, which is what a scale that is not a size means
	return std::clamp(settings.uiScale, kScaleFloor, kScaleCeiling);
}

// What the theme was last asked for, so a change is noticed. Dev tools are main-thread only, as the
// console is (Console.h).
struct Applied
{
	ThemeSettings settings;
	bool linearise{true};
	bool themed{false};
};


Applied &GetApplied()
{
	static Applied applied;
	return applied;
}


// Whether the style is what these settings ask for, read from the style and not from a cache: two
// fields a stock context does not have, a panel's padding on the sheet's scale and the panel's
// ground on the palette. Nothing in the engine writes the style — the views push colours, never the
// style (docs/architecture.md, "UI") — so these two stand for the whole of it. This is why the
// applied state is not keyed by the context's address: the standard's rule is that a cache keys on a
// handle with a generation and never on an address, because the next allocation reuses it
// (docs/coding-standards.md), and measured here it does — a context made after the themed one takes
// its address, and starts stock, because ImGuiStyle's constructor writes the stock values over
// whatever the last context left in the block. The address therefore says nothing about the style it
// holds, and an address key would answer "already themed" to that stock style.
bool StyleIsApplied(const ThemeSettings &settings, bool linearise)
{
	const ImGuiStyle &style = ImGui::GetStyle();
	const float scale = ScaleFor(settings);
	const Rgba ground = PaletteColour(settings.palette, Token::Background, kPanelAlpha, linearise);
	// The same expressions ApplyTheme writes, so the comparison is exact; a fresh context fails it,
	// measured, at padding 8 against 12 and a colour of its own against the palette's.
	return style.WindowPadding.x == kPanelPadding * scale && style.WindowRounding == kRound * scale &&
	       style.Colors[ImGuiCol_WindowBg].x == ground.r &&
	       style.Colors[ImGuiCol_WindowBg].w == ground.a;
}
} // namespace


float DecodeSrgb(float encoded)
{
	// sRGB's transfer function, the piecewise one: the linear part for the darkest values, where a
	// power of a near-zero would otherwise lose its precision.
	return encoded <= 0.04045f ? encoded / 12.92f : std::powf((encoded + 0.055f) / 1.055f, 2.4f);
}


void RegisterThemeTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterEnum<Palette>("DevTools.Palette")
		    .Value<Palette::Latte>("latte")
		    .Value<Palette::Frappe>("frappe")
		    .Value<Palette::Macchiato>("macchiato")
		    .Value<Palette::Mocha>("mocha");
		RegisterType<ThemeSettings>("DevTools.Theme")
		    .Field<&ThemeSettings::palette>("palette", Attribute::Console)
		    .Field<&ThemeSettings::uiScale>("ui_scale", Attribute::Console)
		    .Field<&ThemeSettings::consoleMono>("console_mono", Attribute::Console);
		return true;
	}();
	(void)registered;
}


const std::array<Rgb, kTokenCount> &PaletteTokens(Palette palette)
{
	return Tokens(palette);
}


std::string_view PaletteName(Palette palette)
{
	for (const auto &[variant, name] : kPalettes)
		if (variant == palette)
			return name;
	return "mocha";
}


std::span<const Palette> PaletteVariants()
{
	return kVariants;
}


Rgba PaletteColour(Palette palette, Token token, float alpha, bool linearise)
{
	const Rgb &rgb = Tokens(palette)[static_cast<std::size_t>(token)];
	const auto encoded = [](std::uint8_t byte) { return static_cast<float>(byte) / 255.0f; };
	if (!linearise)
		return {encoded(rgb.r), encoded(rgb.g), encoded(rgb.b), alpha};
	return {DecodeSrgb(encoded(rgb.r)), DecodeSrgb(encoded(rgb.g)), DecodeSrgb(encoded(rgb.b)), alpha};
}


Token LineToken(const ConsoleLine &line)
{
	switch (line.kind)
	{
		case ConsoleLine::Kind::Input: return Token::Primary;     // an echoed command: what was sent
		case ConsoleLine::Kind::Error: return Token::Danger;      // a refusal, in the danger colour
		case ConsoleLine::Kind::Output: return Token::Foreground; // what the command answered
		case ConsoleLine::Kind::Log: break;
	}
	switch (line.severity)
	{
		case Core::Severity::Trace:
		case Core::Severity::Debug: return Token::Faint;
		case Core::Severity::Warning: return Token::Warning;
		case Core::Severity::Error:
		case Core::Severity::Fatal: return Token::Danger;
		case Core::Severity::Info: break;
	}
	return Token::Muted;
}


Rgba ColourOf(Token token, float alpha)
{
	Applied &applied = GetApplied();
	return PaletteColour(applied.settings.palette, token, alpha, applied.linearise);
}


void ApplyTheme(const ThemeSettings &settings, bool swapchainEncodesSrgb)
{
	ImGuiStyle &style = ImGui::GetStyle();
	ImGui::StyleColorsDark(&style); // The baseline for the fields this theme does not name.
	const bool linearise = swapchainEncodesSrgb;
	const Palette palette = settings.palette;
	const float scale = ScaleFor(settings);

	const auto set = [palette, linearise, &style](ImGuiCol colour, Token token, float alpha = 1.0f)
	{
		const Rgba rgba = PaletteColour(palette, token, alpha, linearise);
		style.Colors[colour] = {rgba.r, rgba.g, rgba.b, rgba.a};
	};

	// The sizes, then the rhythm the web layout's spacing scale gives them.
	style.FontSizeBase = kFont * scale;
	style.FontScaleMain = 1.0f;
	style.Alpha = 1.0f;
	style.DisabledAlpha = kDisabledAlpha;
	style.WindowPadding = {kPanelPadding * scale, kPanelPadding * scale};
	style.WindowRounding = kRound * scale;
	style.WindowBorderSize = kBorder;
	style.WindowMinSize = {80.0f * scale, 60.0f * scale};
	style.WindowTitleAlign = {0.0f, 0.5f}; // a header's text, left as a card header's is
	style.WindowMenuButtonPosition = ImGuiDir_Left;
	style.ChildRounding = kRound * scale;
	style.ChildBorderSize = kBorder; // a region of a panel, ringed like the code block in the console
	style.PopupRounding = kRound * scale;
	style.PopupBorderSize = kBorder;
	style.FramePadding = {kFrameX * scale, kFrameY * scale};
	style.FrameRounding = kRoundFrame * scale;
	style.FrameBorderSize = kBorder; // every control wears a hairline, as an input field does
	style.ItemSpacing = {kGap * scale, kGap * scale};
	style.ItemInnerSpacing = {kGapInner * scale, kGapInner * scale};
	style.CellPadding = {kCellX * scale, kCellY * scale};
	style.IndentSpacing = kIndent * scale;
	style.ColumnsMinSpacing = kGap * scale;
	style.ScrollbarSize = kScrollBar * scale;
	style.ScrollbarPadding = kScrollGap * scale;
	style.ScrollbarRounding = kRoundFrame * scale;
	style.GrabMinSize = kGap * scale + 4.0f;
	style.GrabRounding = kRoundFrame * scale;
	style.TabRounding = kRoundFrame * scale;
	style.TabBorderSize = 0.0f;
	style.TabBarBorderSize = 0.0f;
	style.TabBarOverlineSize = kOverline * scale; // a chosen tab is marked by a line, not a box
	style.MenuItemRounding = kRoundFrame * scale;
	style.SelectableRounding = kRoundFrame * scale;
	style.DragDropTargetRounding = kRoundFrame * scale;
	style.DragDropTargetBorderSize = kBorder;
	style.InputTextCursorSize = kCaret * scale;
	style.SeparatorSize = kBorder;
	style.SeparatorTextBorderSize = kBorder;
	style.SeparatorTextPadding = {kGap * scale, kFrameY * scale};
	style.DisplayWindowPadding = {kPanelPadding * scale, kPanelPadding * scale};
	style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesFull; // a tree's guides, as a file tree's are
	style.TreeLinesSize = kBorder;
	style.TreeLinesRounding = 3.0f * scale;
	style.ButtonTextAlign = {0.5f, 0.5f}; // a button's label, centred as a web button's is
	style.AntiAliasedLines = true;        // the radii need anti-aliasing to read as radii
	style.AntiAliasedLineEnds = true;
	style.AntiAliasedFill = true;

	// The panels: a window's ground, its header flat and its border the hairline, as a card is.
	set(ImGuiCol_WindowBg, Token::Background, kPanelAlpha);
	set(ImGuiCol_TitleBg, Token::Surface);
	set(ImGuiCol_TitleBgActive, Token::Surface); // focused or not, a header is one colour
	set(ImGuiCol_TitleBgCollapsed, Token::Sunken);
	set(ImGuiCol_MenuBarBg, Token::Sunken);
	set(ImGuiCol_PopupBg, Token::Surface); // a menu hides what is under it
	set(ImGuiCol_ChildBg, Token::Sunken, kSunkenAlpha);
	set(ImGuiCol_Border, Token::Border);
	set(ImGuiCol_BorderShadow, Token::Border, 0.0f); // shadows are the stylesheet's, not the style's

	// Text, at three weights of the palette's greys, and its selection.
	set(ImGuiCol_Text, Token::Foreground);
	set(ImGuiCol_TextDisabled, Token::Faint);
	set(ImGuiCol_TextLink, Token::Primary);
	set(ImGuiCol_TextSelectedBg, Token::Primary, 0.35f);
	set(ImGuiCol_InputTextCursor, Token::Primary);
	style.Colors[ImGuiCol_ScrollbarBg] = {0.0f, 0.0f, 0.0f, 0.0f}; // the trough is the panel showing
	set(ImGuiCol_ScrollbarGrab, Token::Border);
	set(ImGuiCol_ScrollbarGrabHovered, Token::BorderStrong);
	set(ImGuiCol_ScrollbarGrabActive, Token::Primary);

	// The controls: a sunken field, a secondary button, the palette's blue for the action and for
	// what the keyboard has reached.
	set(ImGuiCol_FrameBg, Token::Sunken);
	set(ImGuiCol_FrameBgHovered, Token::Border);
	set(ImGuiCol_FrameBgActive, Token::Subtle);
	set(ImGuiCol_CheckMark, Token::Primary);
	set(ImGuiCol_CheckboxSelectedBg, Token::Primary, 0.22f);
	set(ImGuiCol_SliderGrab, Token::Primary);
	set(ImGuiCol_SliderGrabActive, Token::Focus);
	set(ImGuiCol_Button, Token::Subtle);
	set(ImGuiCol_ButtonHovered, Token::Hover);
	set(ImGuiCol_ButtonActive, Token::Primary);
	set(ImGuiCol_Header, Token::Subtle);
	set(ImGuiCol_HeaderHovered, Token::Hover);
	set(ImGuiCol_HeaderActive, Token::Primary);

	// Tabs, marked by an overline in the primary colour, dimmed when their window is not focused.
	set(ImGuiCol_Tab, Token::Surface, 0.0f);
	set(ImGuiCol_TabHovered, Token::Hover);
	set(ImGuiCol_TabSelected, Token::Subtle);
	set(ImGuiCol_TabSelectedOverline, Token::Primary);
	set(ImGuiCol_TabDimmed, Token::Surface, 0.0f);
	set(ImGuiCol_TabDimmedSelected, Token::Subtle);
	set(ImGuiCol_TabDimmedSelectedOverline, Token::Faint);

	set(ImGuiCol_Separator, Token::Border);
	set(ImGuiCol_SeparatorHovered, Token::Primary);
	set(ImGuiCol_SeparatorActive, Token::Focus);
	set(ImGuiCol_ResizeGrip, Token::BorderStrong);
	set(ImGuiCol_ResizeGripHovered, Token::Primary);
	set(ImGuiCol_ResizeGripActive, Token::Focus);

	// A table, striped as odd:bg-muted/50 is, and the plots the demo and the metrics windows draw.
	set(ImGuiCol_TableHeaderBg, Token::Surface);
	set(ImGuiCol_TableBorderStrong, Token::Border);
	set(ImGuiCol_TableBorderLight, Token::Border, 0.6f);
	set(ImGuiCol_TableRowBg, Token::Background, 0.0f);
	set(ImGuiCol_TableRowBgAlt, Token::Subtle, kSubtleAlpha);
	set(ImGuiCol_PlotLines, Token::Info);
	set(ImGuiCol_PlotLinesHovered, Token::Primary);
	set(ImGuiCol_PlotHistogram, Token::Teal);
	set(ImGuiCol_PlotHistogramHovered, Token::Peach);

	set(ImGuiCol_TreeLines, Token::Border);
	set(ImGuiCol_DragDropTarget, Token::Focus);
	set(ImGuiCol_DragDropTargetBg, Token::Focus, 0.15f);
	set(ImGuiCol_UnsavedMarker, Token::Peach);
	set(ImGuiCol_NavCursor, Token::Focus);
	set(ImGuiCol_NavWindowingHighlight, Token::Foreground, 0.7f);
	set(ImGuiCol_NavWindowingDimBg, Token::Sunken, 0.8f);
	set(ImGuiCol_ModalWindowDimBg, Token::Sunken, 0.6f);

	Applied &applied = GetApplied();
	applied.settings = settings;
	applied.linearise = linearise;
	applied.themed = true;
}


bool ApplyThemeIfChanged(const ThemeSettings &settings, bool swapchainEncodesSrgb)
{
	// The style is read, not remembered: a context made after the themed one reuses its address
	// (StyleIsApplied says why that matters), so "the settings are the ones I applied" is not enough
	// to say the style is themed. A run that tears its renderer down, and every test that makes a
	// context, gets the theme again, settings unchanged and all.
	const Applied &applied = GetApplied();
	if (applied.themed && applied.settings == settings && applied.linearise == swapchainEncodesSrgb &&
	    StyleIsApplied(settings, swapchainEncodesSrgb))
		return false;
	ApplyTheme(settings, swapchainEncodesSrgb);
	return true;
}


const ThemeSettings &AppliedTheme()
{
	return GetApplied().settings;
}
} // namespace Jettison::DevTools
