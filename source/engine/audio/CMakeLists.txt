# The game's sound: miniaudio behind AudioSystem.h, which names no miniaudio type, so miniaudio is a
# private library. It reads sounds through the VFS, the simulation's events through sim (an edge
# down a tier: it reads events and definitions, and nothing in sim sees it), and the presentation
# world's transforms through scene. The tests run miniaudio with no device and mix its output.
jettison_add_module(audio
	DEPENDS
		core
		reflect
		vfs
		sim
		scene
	HEADERS
		include/jettison/audio/AudioSystem.h
		include/jettison/audio/EventSounds.h
		include/jettison/audio/SoundSources.h
	SOURCES
		src/AudioSystem.cpp
		src/EventSounds.cpp
		src/SoundSources.cpp
	TESTS
		tests/AudioTests.cpp
	PUBLIC_LIBRARIES
		EnTT::EnTT
		glm
	PRIVATE_LIBRARIES
		miniaudio
)

# The tests play the sounds the assets ship.
if(TARGET jettison_audio_tests)
	target_compile_definitions(jettison_audio_tests PRIVATE
		JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets")
endif()
