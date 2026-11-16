#include <jettison/devtools/Views.h>

#include <jettison/devtools/Console.h>
#include <jettison/devtools/Theme.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace Jettison::DevTools
{
namespace
{
struct InputContext
{
	Console &console;
	ConsoleViewState &state;
};

// One face for the scope, which is what the console's lines and its input want, and for the height
// measured between them. A null face keeps the current one (ImGui::PushFont's word), so the callers
// that have no fonts loaded, which is every test, ask for mono and get the UI face.
// Non-copyable, popped with the scope, as the standard asks of a resource.
class FontScope
{
  public:
	explicit FontScope(ImFont *font, float fontSizeBase) : m_font{font}
	{
		ImGui::PushFont(m_font, fontSizeBase);
	}
	~FontScope()
	{
		ImGui::PopFont();
	}

	FontScope(const FontScope &) = delete;
	FontScope &operator=(const FontScope &) = delete;

  private:
	ImFont *m_font;
};

ImVec4 ToImVec4(Rgba colour)
{
	return {colour.r, colour.g, colour.b, colour.a};
}

// The line's colour, from its role: the echoed command in the theme's one accent, a refusal in its
// danger, the category a log came under in its signature colour (Theme.h, LineToken).
ImVec4 LineColour(const ConsoleLine &line)
{
	return ToImVec4(ColourOf(LineToken(line)));
}

ImVec4 CategoryColour()
{
	return ToImVec4(ColourOf(Token::Accent));
}

// The palette names, one after the other and the list ended, as a combo's items want them.
const char *PaletteItems()
{
	static const std::string items = []
	{
		std::string names;
		for (const Palette palette : PaletteVariants())
			names += std::string{PaletteName(palette)} + '\0';
		names += '\0';
		return names;
	}();
	return items.c_str();
}

int PaletteIndex(Palette palette)
{
	const std::span variants = PaletteVariants();
	const auto found = std::ranges::find(variants, palette);
	return found == variants.end() ? 0 : static_cast<int>(found - variants.begin());
}

void Replace(ImGuiInputTextCallbackData *data, const std::string &text)
{
	data->DeleteChars(0, data->BufTextLen);
	data->InsertChars(0, text.c_str());
}

int OnInput(ImGuiInputTextCallbackData *data)
{
	auto &context = *static_cast<InputContext *>(data->UserData);
	switch (data->EventFlag)
	{
		case ImGuiInputTextFlags_CallbackCharFilter:
			// The key that opens the console is not typed into it.
			return data->EventChar == '`' ? 1 : 0;
		case ImGuiInputTextFlags_CallbackCompletion:
		{
			const std::string line{data->Buf, static_cast<std::size_t>(data->BufTextLen)};
			const std::string completed = context.console.CompleteLine(line);
			if (completed != line)
				Replace(data, completed);
			else if (const std::vector<std::string> candidates = context.console.Complete(line);
			         candidates.size() > 1)
			{
				std::string list;
				for (const std::string &candidate : candidates)
					list += (list.empty() ? "" : "  ") + candidate;
				context.console.Print(list);
			}
			return 0;
		}
		case ImGuiInputTextFlags_CallbackHistory:
		{
			const auto &history = context.console.History();
			const int count = static_cast<int>(history.size());
			int &index = context.state.historyIndex;
			if (data->EventKey == ImGuiKey_UpArrow)
				index = std::min(index + 1, count - 1);
			else if (data->EventKey == ImGuiKey_DownArrow)
				index = std::max(index - 1, -1);
			Replace(
			    data, index < 0 ? std::string{} : history[static_cast<std::size_t>(count - 1 - index)]);
			return 0;
		}
		default: return 0;
	}
}
} // namespace

ImFont *MonoFont()
{
	// ImGui names a font by `ImFontConfig::Name`, and for a face read from memory that name is
	// whatever the loader wrote: the render backend writes the file the face came from, measured as
	// "Hack-Regular.ttf" (render/vulkan/imgui/FontFaces.h), because the console reads names. The face
	// whose name is called for a mono face is the mono face, and no UI face's name is. Nothing when
	// none is loaded, which is how a run with no fonts, and every test, draws: the console then draws
	// in the UI face, its older look.
	constexpr std::array<const char *, 3> kMonoWords{"hack", "mono", "cousine"};
	const auto lower = [](std::string text)
	{
		std::ranges::transform(
		    text, text.begin(), [](char letter) { return static_cast<char>(std::tolower(letter)); });
		return text;
	};
	for (ImFont *font : ImGui::GetIO().Fonts->Fonts)
	{
		const std::string name = lower(font->GetDebugName());
		for (const char *word : kMonoWords)
			if (name.find(word) != std::string::npos)
				return font;
	}
	return nullptr;
}

void DrawConsole(Console &console, ConsoleViewState &state, const ThemeSettings &settings, bool *open)
{
	ImGui::SetNextWindowSize({720.0f, 360.0f}, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Console", open))
	{
		ImGui::End();
		return;
	}

	// What is read, not written, is set in mono and on the ground a code block has, ringed by the
	// hairline. The input line below is in mono too, whatever the setting: what is typed there is a
	// command, and it is read back a character at a time when it is edited.
	const float fontSizeBase = ImGui::GetStyle().FontSizeBase;
	ImFont *mono = MonoFont();
	float inputHeight = 0.0f;
	{
		// Measured in the face the input is drawn in, so the lines above it are not crowded out when
		// mono is the taller face of the two, as Hack at 16 px is of Roboto Medium.
		const FontScope inputFont{mono, fontSizeBase};
		inputHeight = ImGui::GetFrameHeightWithSpacing();
	}
	{
		const FontScope linesFont{settings.consoleMono ? mono : nullptr, fontSizeBase};
		if (ImGui::BeginChild(
		        "lines", {0.0f, -inputHeight}, ImGuiChildFlags_Borders,
		        ImGuiWindowFlags_HorizontalScrollbar))
		{
			const std::vector<ConsoleLine> lines = console.Lines();
			// Follow new lines only for a reader already at the bottom.
			const bool follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
			for (const ConsoleLine &line : lines)
			{
				if (!line.category.empty())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, CategoryColour());
					ImGui::Text("[%s]", line.category.c_str());
					ImGui::PopStyleColor();
					ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
				}
				ImGui::PushStyleColor(ImGuiCol_Text, LineColour(line));
				ImGui::TextUnformatted(line.text.c_str());
				ImGui::PopStyleColor();
			}
			if (follow && lines.size() != state.linesSeen)
				ImGui::SetScrollHereY(1.0f);
			state.linesSeen = lines.size();
		}
		ImGui::EndChild();
	}

	InputContext context{console, state};
	constexpr ImGuiInputTextFlags flags =
	    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
	    ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCharFilter;
	if (state.focusInput)
	{
		ImGui::SetKeyboardFocusHere();
		state.focusInput = false;
	}
	// The scope closes before End(): a window's font pushes are inherited by Begin and must be
	// popped before it ends, which ImGui checks (measured: the smoke run asserts "Missing PopFont()"
	// when the pop lands after End, in a build with ImGui's assertions on).
	{
		const FontScope inputFont{mono, fontSizeBase};
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText(
		        "##input", state.input.data(), state.input.size(), flags, &OnInput, &context))
		{
			console.Execute(state.input.data());
			state.input.fill('\0');
			state.historyIndex = -1;
			// Enter takes the focus away; give it back for the next line.
			ImGui::SetKeyboardFocusHere(-1);
		}
	}
	ImGui::End();
}

void DrawDevTools(Console &console, DevToolsState &state, ThemeSettings &settings, bool *open)
{
	ImGui::SetNextWindowSize({240.0f, 0.0f}, ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Dev tools", open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (ImGui::Checkbox("Console (`)", &state.showConsole) && state.showConsole)
			state.console.focusInput = true;
		ImGui::Checkbox("ImGui demo", &state.showDemo);
		ImGui::Checkbox("ImGui metrics", &state.showMetrics);

		ImGui::SeparatorText("Theme");
		int index = PaletteIndex(settings.palette);
		if (ImGui::Combo("Palette", &index, PaletteItems()) && index >= 0 &&
		    static_cast<std::size_t>(index) < PaletteVariants().size())
			settings.palette = PaletteVariants()[static_cast<std::size_t>(index)];
		ImGui::Checkbox("Console mono", &settings.consoleMono);
		// The design's root font size, which the spacing and the radii go with: a dev on a 4K panel
		// reads the console without squinting, and the whole style stays in proportion. The band is
		// what a developer is offered; `dev.ui_scale` answers to any number, and ApplyTheme keeps the
		// style inside the band a style survives (Theme.cpp measures what happens outside it).
		ImGui::SliderFloat("Scale", &settings.uiScale, 0.75f, 2.0f, "%.2f");
	}
	ImGui::End();

	if (state.showConsole)
		DrawConsole(console, state.console, settings, &state.showConsole);
	if (state.showDemo)
		ImGui::ShowDemoWindow(&state.showDemo);
	if (state.showMetrics)
		ImGui::ShowMetricsWindow(&state.showMetrics);
}
} // namespace Jettison::DevTools
