# The app shell: argument parsing, the lifecycle and frame loop, and the headless smoke sequence.
# RunRenderApplication is the entry point flotsam, jetsam and render_test call. platform is a
# dependency before the shell calls into it, because linking it is what puts the start-up CPU check
# (platform/Cpu.h) into every executable the shell is in.
jettison_add_module(app
	DEPENDS
		animation
		audio
		core
		devtools
		ecs
		host
		input
		jobs
		physics
		platform
		profile
		render
		resource
		scene
		script
		sim
		time
		ui
		vfs
	HEADERS
		include/jettison/app/AppArguments.h
		include/jettison/app/Game.h
		include/jettison/app/Mounts.h
		include/jettison/app/Reasons.h
		include/jettison/app/RenderApplication.h
		include/jettison/app/SmokeSequence.h
	SOURCES
		src/AppArguments.cpp
		src/Application.cpp
		src/Application.h
		src/Game.cpp
		src/GlfwInput.cpp
		src/GlfwInput.h
		src/Mounts.cpp
		src/Reasons.cpp
		src/RenderApplication.cpp
		src/Saves.cpp
		src/Saves.h
		src/Settings.cpp
		src/Settings.h
		src/SmokeSequence.cpp
	TESTS
		tests/AppArgumentsTests.cpp
		tests/GameTests.cpp
		tests/MountsTests.cpp
		tests/SavesTests.cpp
		tests/SettingsTests.cpp
		tests/SmokeSequenceTests.cpp
	PRIVATE_LIBRARIES
		glfw
)

# The game tests hold the engine's assets to what the engine ships, and the sample's content apart.
if(TARGET jettison_app_tests)
	target_compile_definitions(jettison_app_tests PRIVATE
		JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets"
		JETTISON_SAMPLE_ASSET_DIR="${PROJECT_SOURCE_DIR}/source/samples/space/assets")
endif()
