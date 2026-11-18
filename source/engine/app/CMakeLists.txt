# The app shell: argument parsing, the lifecycle and frame loop, and the headless smoke sequence.
# RunRenderApplication is the entry point flotsam, jetsam and render_test call. platform is a
# dependency before the shell calls into it, because linking it is what puts the start-up CPU check
# (platform/Cpu.h) into every executable the shell is in.
jettison_add_module(app
	DEPENDS
		ai
		animation
		audio
		celestial
		core
		devtools
		dock
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
		space
		spatial
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
		include/jettison/app/Stations.h
	SOURCES
		src/AppArguments.cpp
		src/Application.cpp
		src/Application.h
		src/Dressing.cpp
		src/Dressing.h
		src/Game.cpp
		src/GlfwInput.cpp
		src/GlfwInput.h
		src/Mounts.cpp
		src/Piloting.cpp
		src/Piloting.h
		src/Reasons.cpp
		src/RenderApplication.cpp
		src/Saves.cpp
		src/Saves.h
		src/Settings.cpp
		src/Settings.h
		src/SmokeSequence.cpp
		src/Stations.cpp
	TESTS
		tests/AppArgumentsTests.cpp
		tests/DressingTests.cpp
		tests/GameTests.cpp
		tests/LargeWorldTests.cpp
		tests/MountsTests.cpp
		tests/PilotingTests.cpp
		tests/SavesTests.cpp
		tests/SettingsTests.cpp
		tests/SmokeSequenceTests.cpp
		tests/StationsTests.cpp
	PRIVATE_LIBRARIES
		glfw
)

# The large-world, dressing and stations tests read the star systems and definitions the sample's
# content ships; the game tests hold the engine's assets to what the engine ships.
if(TARGET jettison_app_tests)
	target_compile_definitions(jettison_app_tests PRIVATE
		JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets"
		JETTISON_SAMPLE_ASSET_DIR="${PROJECT_SOURCE_DIR}/source/samples/space/assets")
endif()
