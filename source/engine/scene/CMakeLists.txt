# The presentation scene: its components, its file, the systems that animate it and compose its
# transforms, and the camera rigs that move through it. It names no device; the renderer reads it
# (docs/architecture.md, "Simulation and presentation").
jettison_add_module(scene
	DEPENDS
		animation
		celestial
		core
		ecs
		math
		reflect
		resource
		serialise
		vfs
	HEADERS
		include/jettison/scene/CameraRigs.h
		include/jettison/scene/Components.h
		include/jettison/scene/Scene.h
		include/jettison/scene/SceneSystems.h
		include/jettison/scene/Shape.h
		include/jettison/scene/SystemScene.h
	SOURCES
		src/CameraRigs.cpp
		src/Scene.cpp
		src/SceneSystems.cpp
		src/Shape.cpp
		src/SystemScene.cpp
	TESTS
		tests/CameraRigTests.cpp
		tests/SceneTests.cpp
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
)

# The scene tests load the sample scenes and models the assets ship.
if(TARGET jettison_scene_tests)
	target_compile_definitions(jettison_scene_tests PRIVATE
		JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets")
endif()
