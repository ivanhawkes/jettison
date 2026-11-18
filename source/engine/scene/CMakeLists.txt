# The presentation scene: its components, its file, the systems that animate it and compose its
# transforms, the camera rigs and the cinematics that move through it, and the proxies of the
# simulation's entities from its snapshots. It names no device; the renderer reads it
# (docs/architecture.md, "Simulation and presentation"). A star system's scene is spacescene's.
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
)

# The scene tests load the sample's scenes and models.
if(TARGET jettison_scene_tests)
	target_compile_definitions(jettison_scene_tests PRIVATE
		JETTISON_SAMPLE_ASSET_DIR="${PROJECT_SOURCE_DIR}/source/samples/space/assets")
endif()
