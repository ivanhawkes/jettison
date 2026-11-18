# The space capability's presentation (docs/architecture.md, "Module graph"): a star system's scene,
# its bodies on their orbits and the simulation's traffic in it, and the jump transition between two
# systems' scenes. A game that has no star systems links none of it.
jettison_add_module(spacescene
	NAMESPACE SpaceScene
	DEPENDS
		celestial
		core
		math
		resource
		scene
		sim
		vfs
	HEADERS
		include/jettison/spacescene/JumpTransition.h
		include/jettison/spacescene/SystemScene.h
	SOURCES
		src/JumpTransition.cpp
		src/SystemScene.cpp
	TESTS
		tests/JumpTransitionTests.cpp
		tests/SystemSceneTests.cpp
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
)

# The suites read the sample's star systems, galaxy and scenes.
if(TARGET jettison_spacescene_tests)
	target_compile_definitions(jettison_spacescene_tests PRIVATE
		JETTISON_SAMPLE_ASSET_DIR="${PROJECT_SOURCE_DIR}/source/samples/space/assets")
endif()
