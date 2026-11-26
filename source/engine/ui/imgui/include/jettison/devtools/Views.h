#pragma once

#include <jettison/devtools/Theme.h>

#include <array>
#include <cstddef>

struct ImFont;

namespace Jettison::DevTools
{
class Console;

// What the console window keeps between frames.
struct ConsoleViewState
{
	std::array<char, 512> input{};

	// Into Console::History, counted back from the newest; -1 is the line being typed.
	int historyIndex{-1};

	// Set when the window opens, so typing goes straight to the input.
	bool focusInput{true};
	std::size_t linesSeen{0};
};

// The windows the dev tools menu opens.
struct DevToolsState
{
	bool showConsole{false};
	bool showDemo{false};
	bool showMetrics{false};
	ConsoleViewState console;
};

// Inside an ImGui frame. The console window: the lines coloured by kind and severity, kept
// scrolled to the bottom as lines arrive unless the reader has scrolled up, and an input line
// with history on Up and Down and completion on Tab. The lines are a code block, in the mono face
// on the sunken ground when settings says so. *open goes false when it is closed.
void DrawConsole(Console &console, ConsoleViewState &state, const ThemeSettings &settings, bool *open);

// The dev tools menu, the window F1 opens: a checkbox per tool, and the tools that are on, and the
// theme's controls, which write settings as the console's variables do (Theme.h).
void DrawDevTools(Console &console, DevToolsState &state, ThemeSettings &settings, bool *open);

// The mono face among the fonts the renderer loaded, which the console draws its lines in; nothing
// when none is, which is how every test and a fontless run draws. Views.cpp's rule, published for
// the tests to check it against the fonts the assets ship.
ImFont *MonoFont();
} // namespace Jettison::DevTools
