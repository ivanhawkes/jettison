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
// The frames are policed by ImGui itself: it records the stack sizes at Begin and checks them at
// End, so a font pushed inside a window and popped after that window ends fails the suite, as the
// smoke run's "Missing PopFont()" did (measured: with the console's input face popped after End, a
// test fails here). A context is owned by pointer and destroyed by pointer, and made current:
// ImGui's DestroyContext(nullptr) takes the *current* context, and CreateContext hands the previous
// one back as current, so two fixtures in one case, with ImGui's general allocator handing the same
// address back (measured), would destroy one context twice.
class HeadlessImGui
{
  public:
	HeadlessImGui() : m_context{ImGui::CreateContext()}
	{
		ImGui::SetCurrentContext(m_context);
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
		ImGuiContext *outer = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_context);
		ImGui::DestroyContext(m_context);
		if (outer != m_context)
			ImGui::SetCurrentContext(outer);
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

  private:
	ImGuiContext *m_context{nullptr};
};
} // namespace

TEST_CASE("Views: the dev tools and every tool they open draw a frame")
{
	HeadlessImGui imgui;
	Console console;
	console.Print("hello");
	DevToolsState state;
	ThemeSettings theme;
	state.showConsole = true;
	state.showDemo = true;
	state.showMetrics = true;
	bool open = true;
	for (int frame = 0; frame < 3; ++frame)
		imgui.Frame([&] { DrawDevTools(console, state, theme, &open); });
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
	ThemeSettings theme;
	bool open = true;
	const auto frame = [&] { imgui.Frame([&] { DrawConsole(console, state, theme, &open); }); };

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
