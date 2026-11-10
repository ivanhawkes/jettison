# The developer console and dev tools over ImGui (docs/architecture.md, ui/imgui). The console
# itself draws nothing and is tested as text; Views.cpp puts it on screen, and its tests run ImGui
# with no renderer. The ImGui renderer, the Vulkan side, stays in render.
jettison_add_module(devtools
	DEPENDS
		core
		reflect
	HEADERS
		include/jettison/devtools/Console.h
		include/jettison/devtools/Views.h
	SOURCES
		src/Console.cpp
		src/Views.cpp
	TESTS
		tests/ConsoleTests.cpp
		tests/ViewTests.cpp
	PUBLIC_LIBRARIES
		imgui
)
