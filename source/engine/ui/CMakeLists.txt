# The game UI: RmlUi behind UiSystem.h and DataModel.h, which name no RmlUi type, so RmlUi is a
# private library. It draws into render's Overlay, a plain draw list the renderer reads, so the edge
# to render is to its CPU side only (docs/architecture.md, "UI"). The tests run RmlUi with no device.
jettison_add_module(ui
	DEPENDS
		core
		input
		reflect
		render
		vfs
	HEADERS
		include/jettison/ui/DataModel.h
		include/jettison/ui/StringTable.h
		include/jettison/ui/UiSystem.h
	SOURCES
		src/DataModel.cpp
		src/RmlInterfaces.cpp
		src/RmlInterfaces.h
		src/StringTable.cpp
		src/UiState.h
		src/UiSystem.cpp
	TESTS
		tests/StringTableTests.cpp
		tests/UiTests.cpp
	PUBLIC_LIBRARIES
		EnTT::EnTT
		glm
	PRIVATE_LIBRARIES
		RmlUi::Core
		yaml-cpp::yaml-cpp
)

# The tests read the fonts the assets ship.
if(TARGET jettison_ui_tests)
	target_compile_definitions(jettison_ui_tests PRIVATE
		JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets")
endif()
