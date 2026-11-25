#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Jettison::DevTools
{
struct ConsoleLine;

// The style's colour roles, named as a Tailwind/shadcn stylesheet names them, so a palette is a
// table of colour and not a scattering of literals, and the same word means the same thing in the
// style, the console's lines and the dev tools' own controls (docs/ui.md, "The dev tools' theme").
// Every role is one of Catppuccin's colours; which one is the palette's business.
enum class Token : std::uint8_t
{
	Background,   // the panel's ground, as bg-background is
	Surface,      // a raised panel, a menu, a title bar: bg-card, bg-popover
	Sunken,       // the code block's ground, where text is read rather than written
	Foreground,   // text-foreground
	Muted,        // text-muted-foreground
	Faint,        // a further step down, for what is there to orient by
	Border,       // border-input, the hairline on a resting control
	BorderStrong, // the hairline a hovered or active control takes
	Subtle,       // bg-secondary, a control's resting ground
	Hover,        // hover:bg-accent
	Primary,      // bg-primary: the one colour a control wears when it is the action
	OnPrimary,    // text-primary-foreground, what sits on Primary and must contrast with it
	Focus,        // ring-ring, the outline a control takes as the keyboard reaches it
	Success,
	Warning,
	Danger,
	Info,
	Accent, // a category, a heading: mauve, the palette's other signature
	Peach,
	Teal,
	Flamingo,
	Pink, // the accents a plot, a drag target or a console category picks
	Count
};

// The four Catppuccin varieties. Mocha is the default, the one the design is drawn against; the
// console's `dev.palette` and the dev tools' Theme control switch them between the others.
enum class Palette : std::uint8_t
{
	Latte, // the light one, for a projector or a bright room
	Frappe,
	Macchiato,
	Mocha
};

inline constexpr std::size_t kTokenCount = static_cast<std::size_t>(Token::Count);

// One of the palette's colours as the palette publishes it, and as an alpha'd colour. Plain
// numbers, so no header of the dev tools has to name ImGui (Views.h is the model).
struct Rgb
{
	std::uint8_t r{0};
	std::uint8_t g{0};
	std::uint8_t b{0};
};

struct Rgba
{
	float r{0.0f};
	float g{0.0f};
	float b{0.0f};
	float a{1.0f};
};

// What the theme is set to. The console binds these as `dev.*` variables and the dev tools' own
// controls write the same fields, so the mouse and the console cannot disagree; the shell applies
// them (ApplyThemeIfChanged), which is how a change takes effect mid-run.
struct ThemeSettings
{
	Palette palette{Palette::Mocha};
	// 1 is the design's body text of 16 px, Tailwind's text-base; the spacing and the radii are
	// set against that and scale with it. ApplyTheme keeps it inside the band a style survives
	// (0.25 to 4), which the console's variable cannot be trusted to respect.
	float uiScale{1.0f};
	// The console's lines in a mono face, as a code block is, rather than the UI face.
	bool consoleMono{true};

	bool operator==(const ThemeSettings &other) const = default;
};

// Registers Palette and ThemeSettings with Reflect, so the console can read and set them by name.
// Idempotent, and needed before Console::BindVariables sees a ThemeSettings.
void RegisterThemeTypes();

// The palette's colours, in the order Token names them. Every palette holds every role: the roles
// are not optional, so a variety is a table and not a set of fallbacks.
const std::array<Rgb, kTokenCount> &PaletteTokens(Palette palette);

std::string_view PaletteName(Palette palette);

// In the order the dev tools' control lists them: the darkest first, so switching one step down
// is one press of the control.
std::span<const Palette> PaletteVariants();

// The colour a role is on a palette, at an alpha, in the space the style wants: encoded as the
// palette publishes it when linearise is false, and decoded to linear light when it is true. The
// decode is what makes a hex read as written: the dev tools draw straight into the swapchain, and
// on an sRGB attachment the hardware encodes what it is given (see DecodeSrgb), so an encoded
// colour written to it lands lighter than the palette says. Alpha is not decoded: it is a
// coverage, not a colour.
Rgba PaletteColour(Palette palette, Token token, float alpha, bool linearise);

// The style: colours, spacing, rounding, sizes. It is applied over ImGui's dark style, and names
// every colour role a window of the dev tools can show, which ThemeTests checks, so no part of a
// window is left in the stock theme's colours by not being named. The metrics are the sheet's
// spacing, in px at the design's root size, times `uiScale`. Apply it once the context exists.
//
// swapchainEncodesSrgb is the renderer's word on the attachment the dev tools draw into
// (IRenderer::UiColoursLinearised): true on the sRGB swapchain the engine prefers, false on the
// UNORM fallback it takes when the compositor asks for one. `uiScale` is kept inside the band a
// style survives, so a metric is never handed to ImGui outside it (Theme.cpp says what the band is
// for).
void ApplyTheme(const ThemeSettings &settings, bool swapchainEncodesSrgb);

// Applies the theme when the style is not what the settings ask for, which is how a console variable
// takes effect: true when this call applied one. The style is read, not remembered by the address
// of the context it belongs to, which a new context reuses. Main thread only.
bool ApplyThemeIfChanged(const ThemeSettings &settings, bool swapchainEncodesSrgb);

// What the last ApplyTheme on this context was given, and whether it decoded for the attachment.
// The dev tools' controls read it, and the tests check what was applied.
const ThemeSettings &AppliedTheme();

// A console line's colour, as a log's severity and the line's kind: an echoed command in Primary,
// a refusal in Danger, a warning in Warning, what is only there to orient by in Faint. Total:
// every kind and every severity has a role.
Token LineToken(const ConsoleLine &line);

// A role in the palette the last ApplyTheme applied, in the space it decoded for: what a view
// draws with, so the console's lines and the style cannot drift apart.
Rgba ColourOf(Token token, float alpha = 1.0f);

// sRGB's encoding, per channel: what a hex byte over 255 means as linear light. The contract
// overlay.frag states for the game UI, for the same reason: "an sRGB attachment encodes what is
// written to it, so the colour goes to it in linear light".
float DecodeSrgb(float encoded);
} // namespace Jettison::DevTools
