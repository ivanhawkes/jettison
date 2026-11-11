# The clip player and its rig, with the rig's loader: a glTF file's hierarchy, skin and clips, read
# through the resource manager. Nothing here knows the device, so the scene's systems sample clips
# without the renderer.
jettison_add_module(animation
	DEPENDS
		resource
	HEADERS
		include/jettison/animation/Components.h
		include/jettison/animation/Playback.h
		include/jettison/animation/Rig.h
		include/jettison/animation/RigLoader.h
	SOURCES
		src/Playback.cpp
		src/Rig.cpp
		src/RigLoader.cpp
	TESTS
		tests/AnimationTests.cpp
	PUBLIC_LIBRARIES
		glm
	PRIVATE_LIBRARIES
		fastgltf
)

# The rig loader's test reads the models the assets ship.
if(TARGET jettison_animation_tests)
	target_compile_definitions(jettison_animation_tests PRIVATE
		JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets")
endif()
