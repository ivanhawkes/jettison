#include <jettison/devtools/Views.h>

#include <jettison/devtools/Console.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>
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

ImVec4 Colour(const ConsoleLine &line)
{
	switch (line.kind)
	{
		case ConsoleLine::Kind::Input: return {0.55f, 0.75f, 1.0f, 1.0f};
		case ConsoleLine::Kind::Error: return {1.0f, 0.45f, 0.4f, 1.0f};
		case ConsoleLine::Kind::Output: return {0.9f, 0.9f, 0.9f, 1.0f};
		case ConsoleLine::Kind::Log: break;
	}
	switch (line.severity)
	{
		case Core::Severity::Trace:
		case Core::Severity::Debug: return {0.6f, 0.6f, 0.6f, 1.0f};
		case Core::Severity::Warning: return {1.0f, 0.8f, 0.35f, 1.0f};
		case Core::Severity::Error:
		case Core::Severity::Fatal: return {1.0f, 0.45f, 0.4f, 1.0f};
		case Core::Severity::Info: break;
	}
	return {0.8f, 0.8f, 0.8f, 1.0f};
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

void DrawConsole(Console &console, ConsoleViewState &state, bool *open)
{
	ImGui::SetNextWindowSize({720.0f, 360.0f}, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Console", open))
	{
		ImGui::End();
		return;
	}

	const float inputHeight = ImGui::GetFrameHeightWithSpacing();
	if (ImGui::BeginChild(
	        "lines", {0.0f, -inputHeight}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
	{
		const std::vector<ConsoleLine> lines = console.Lines();
		// Follow new lines only for a reader already at the bottom.
		const bool follow = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
		for (const ConsoleLine &line : lines)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Colour(line));
			if (line.category.empty())
				ImGui::TextUnformatted(line.text.c_str());
			else
				ImGui::Text("[%s] %s", line.category.c_str(), line.text.c_str());
			ImGui::PopStyleColor();
		}
		if (follow && lines.size() != state.linesSeen)
			ImGui::SetScrollHereY(1.0f);
		state.linesSeen = lines.size();
	}
	ImGui::EndChild();

	InputContext context{console, state};
	constexpr ImGuiInputTextFlags flags =
	    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
	    ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCharFilter;
	if (state.focusInput)
	{
		ImGui::SetKeyboardFocusHere();
		state.focusInput = false;
	}
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::InputText("##input", state.input.data(), state.input.size(), flags, &OnInput, &context))
	{
		console.Execute(state.input.data());
		state.input.fill('\0');
		state.historyIndex = -1;
		// Enter takes the focus away; give it back for the next line.
		ImGui::SetKeyboardFocusHere(-1);
	}
	ImGui::End();
}

void DrawDevTools(Console &console, DevToolsState &state, bool *open)
{
	ImGui::SetNextWindowSize({240.0f, 0.0f}, ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Dev tools", open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (ImGui::Checkbox("Console (`)", &state.showConsole) && state.showConsole)
			state.console.focusInput = true;
		ImGui::Checkbox("ImGui demo", &state.showDemo);
		ImGui::Checkbox("ImGui metrics", &state.showMetrics);
	}
	ImGui::End();

	if (state.showConsole)
		DrawConsole(console, state.console, &state.showConsole);
	if (state.showDemo)
		ImGui::ShowDemoWindow(&state.showDemo);
	if (state.showMetrics)
		ImGui::ShowMetricsWindow(&state.showMetrics);
}
} // namespace Jettison::DevTools
