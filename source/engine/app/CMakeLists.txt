# The app shell: argument parsing, the lifecycle and frame loop, and the headless smoke sequence.
# RunRenderApplication is the entry point flotsam, jetsam and render_test call. platform is a
# dependency before the shell calls into it, because linking it is what puts the start-up CPU check
# (platform/Cpu.h) into every executable the shell is in.
jettison_add_module(app
	DEPENDS
		animation
		celestial
		core
		devtools
		ecs
		input
		jobs
		physics
		platform
		profile
		render
		resource
		scene
		sim
		time
		vfs
	HEADERS
		include/jettison/app/AppArguments.h
		include/jettison/app/RenderApplication.h
		include/jettison/app/SmokeSequence.h
	SOURCES
		src/AppArguments.cpp
		src/Application.cpp
		src/Application.h
		src/Docking.cpp
		src/Docking.h
		src/GlfwInput.cpp
		src/GlfwInput.h
		src/Piloting.cpp
		src/Piloting.h
		src/RenderApplication.cpp
		src/Saves.cpp
		src/Saves.h
		src/Settings.cpp
		src/Settings.h
		src/SmokeSequence.cpp
	TESTS
		tests/DockingTests.cpp
		tests/LargeWorldTests.cpp
		tests/PilotingTests.cpp
		tests/SavesTests.cpp
		tests/SettingsTests.cpp
		tests/SmokeSequenceTests.cpp
	PRIVATE_LIBRARIES
		glfw
)

# The large-world test reads the sample star system the assets ship.
if(TARGET jettison_app_tests)
	target_compile_definitions(jettison_app_tests PRIVATE
		JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets")
endif()
