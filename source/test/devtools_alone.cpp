// Devtools with no renderer: a console drawn in one ImGui frame, then this process's mappings, which
// must hold no Vulkan loader and no GLFW. CheckDevtoolsAlone.cmake checks the NEEDED entries.

#include <imgui.h>

#include <cstdio>
#include <fstream>
#include <string>

#include <jettison/devtools/Console.h>
#include <jettison/devtools/Views.h>

int main()
{
	Jettison::DevTools::Console console;
	console.Print("devtools alone");
	ImGuiContext *context = ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize = {1280.0f, 720.0f};
	io.IniFilename = nullptr;
	unsigned char *pixels = nullptr;
	int width = 0;
	int height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	Jettison::DevTools::ConsoleViewState state;
	const Jettison::DevTools::ThemeSettings theme;
	bool open = true;
	ImGui::NewFrame();
	Jettison::DevTools::DrawConsole(console, state, theme, &open);
	ImGui::Render();
	ImGui::DestroyContext(context);

	std::ifstream maps{"/proc/self/maps"};
	for (std::string line; std::getline(maps, line);)
	{
		if (line.find("libvulkan") != std::string::npos || line.find("libglfw") != std::string::npos)
		{
			std::fprintf(stderr, "devtools_alone mapped %s\n", line.c_str());
			return 1;
		}
	}
	return 0;
}
