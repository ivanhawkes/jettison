#include <jettison/devtools/Console.h>
#include <jettison/devtools/Views.h>

#include <doctest/doctest.h>

#include <imgui.h>

#include <functional>
#include <string>
#include <vector>

using namespace Jettison::DevTools;

namespace
{
// ImGui with no renderer: a context, a display size and a built font atlas are all a frame needs.
class HeadlessImGui
{
  public:
	HeadlessImGui()
	{
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = {1280.0f, 720.0f};
		io.DeltaTime = 1.0f / 60.0f;
		io.IniFilename = nullptr;
		unsigned char *pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	}
	~HeadlessImGui()
	{
		ImGui::DestroyContext();
	}
	HeadlessImGui(const HeadlessImGui &) = delete;
	HeadlessImGui &operator=(const HeadlessImGui &) = delete;

	void Frame(const std::function<void()> &draw)
	{
		ImGui::NewFrame();
		draw();
		ImGui::Render();
	}

	static void Key(ImGuiKey key)
	{
		ImGui::GetIO().AddKeyEvent(key, true);
	}
	static void Release(ImGuiKey key)
	{
		ImGui::GetIO().AddKeyEvent(key, false);
	}
};
} // namespace

TEST_CASE("Views: the dev tools and every tool they open draw a frame")
{
	HeadlessImGui imgui;
	Console console;
	console.Print("hello");
	DevToolsState state;
	state.showConsole = true;
	state.showDemo = true;
	state.showMetrics = true;
	bool open = true;
	for (int frame = 0; frame < 3; ++frame)
		imgui.Frame([&] { DrawDevTools(console, state, &open); });
	CHECK(open);
	CHECK(state.showConsole);
	CHECK(ImGui::GetDrawData()->CmdListsCount > 0);
}

TEST_CASE("Views: a line typed into the console window and entered is executed")
{
	HeadlessImGui imgui;
	Console console;
	std::vector<std::string> said;
	console.RegisterCommand(
	    "say", "Says the words", [&](Console &, std::span<const std::string> words)
	    { said.assign(words.begin(), words.end()); });
	ConsoleViewState state;
	bool open = true;
	const auto frame = [&] { imgui.Frame([&] { DrawConsole(console, state, &open); }); };

	// The window opens with the input focused.
	frame();
	frame();
	// The opening key is filtered out, as it would be typed by the key that opened the console.
	ImGui::GetIO().AddInputCharactersUTF8("`say hi");
	frame();
	HeadlessImGui::Key(ImGuiKey_Enter);
	frame();
	HeadlessImGui::Release(ImGuiKey_Enter);
	frame();
	CHECK(said == std::vector<std::string>{"hi"});
	CHECK(console.History() == std::vector<std::string>{"say hi"});
	CHECK(std::string{state.input.data()}.empty());

	// Up brings the last line back.
	HeadlessImGui::Key(ImGuiKey_UpArrow);
	frame();
	HeadlessImGui::Release(ImGuiKey_UpArrow);
	frame();
	CHECK(std::string{state.input.data()} == "say hi");
}
