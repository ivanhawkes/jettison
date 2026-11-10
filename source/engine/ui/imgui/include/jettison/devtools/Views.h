#pragma once

#include <array>
#include <cstddef>

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
// with history on Up and Down and completion on Tab. *open goes false when it is closed.
void DrawConsole(Console &console, ConsoleViewState &state, bool *open);

// The dev tools menu, the window F1 opens: a checkbox per tool, then the tools that are on.
void DrawDevTools(Console &console, DevToolsState &state, bool *open);
} // namespace Jettison::DevTools
