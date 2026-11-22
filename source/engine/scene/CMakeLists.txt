# The presentation scene: its components, its file, the systems that animate it and compose its
# transforms, the camera rigs and the cinematics that move through it, and the proxies of the
# simulation's entities from its snapshots. It names no device; the renderer reads it
# (docs/architecture.md, "Simulation and presentation").
jettison_add_module(scene
	DEPENDS
		animation
		core
		ecs
		math
		reflect
		resource
		serialise
		sim
		vfs
	HEADERS
		include/jettison/scene/CameraRigs.h
		include/jettison/scene/Components.h
		include/jettison/scene/Scene.h
		include/jettison/scene/SceneSystems.h
		include/jettison/scene/Sequence.h
		include/jettison/scene/Shape.h
		include/jettison/scene/SimProxies.h
		include/jettison/scene/Wave.h
	SOURCES
		src/CameraRigs.cpp
		src/Scene.cpp
		src/SceneSystems.cpp
		src/Sequence.cpp
		src/Shape.cpp
		src/SimProxies.cpp
		src/Wave.cpp
	TESTS
		tests/CameraRigTests.cpp
		tests/SceneTests.cpp
		tests/SequenceTests.cpp
		tests/SimProxiesTests.cpp
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
	PRIVATE_LIBRARIES
		yaml-cpp::yaml-cpp
)

# The scene tests load the test content's scenes and models (source/test/content).
if(TARGET jettison_scene_tests)
	target_compile_definitions(jettison_scene_tests PRIVATE
		JETTISON_TEST_CONTENT_DIR="${PROJECT_SOURCE_DIR}/source/test/content")
endif()
