# The app shell: argument parsing, the lifecycle and frame loop, and the headless smoke sequence.
# RunRenderApplication is the entry point flotsam, jetsam and render_test call. platform is a
# dependency before the shell calls into it, because linking it is what puts the start-up CPU check
# (platform/Cpu.h) into every executable the shell is in.
jettison_add_module(app
	DEPENDS
		core
		devtools
		ecs
		input
		platform
		profile
		render
		sim
		time
	HEADERS
		include/jettison/app/AppArguments.h
		include/jettison/app/RenderApplication.h
		include/jettison/app/SimPresentation.h
		include/jettison/app/SmokeSequence.h
	SOURCES
		src/AppArguments.cpp
		src/Application.cpp
		src/Application.h
		src/GlfwInput.cpp
		src/GlfwInput.h
		src/RenderApplication.cpp
		src/SimPresentation.cpp
		src/SmokeSequence.cpp
	TESTS
		tests/SimPresentationTests.cpp
		tests/SmokeSequenceTests.cpp
	PRIVATE_LIBRARIES
		glfw
)
