# The app shell: argument parsing, the lifecycle and frame loop, and the headless smoke sequence.
# RunRenderApplication is the entry point flotsam, jetsam and render_test call.
jettison_add_module(app
	DEPENDS
		core
		render
	HEADERS
		include/jettison/app/AppArguments.h
		include/jettison/app/RenderApplication.h
		include/jettison/app/SmokeSequence.h
	SOURCES
		src/AppArguments.cpp
		src/Application.cpp
		src/Application.h
		src/RenderApplication.cpp
		src/SmokeSequence.cpp
	TESTS
		tests/SmokeSequenceTests.cpp
	PRIVATE_LIBRARIES
		glfw
)
