jettison_add_module(sim
	DETERMINISTIC
	DEPENDS
		core
		math
		time
		ecs
		reflect
		serialise
		vfs
	HEADERS
		include/jettison/sim/CommandQueue.h
		include/jettison/sim/Controller.h
		include/jettison/sim/Events.h
		include/jettison/sim/Reason.h
		include/jettison/sim/SaveGame.h
		include/jettison/sim/SimWorld.h
		include/jettison/sim/DefinitionRegistry.h
		include/jettison/sim/Snapshot.h
		include/jettison/sim/Stepper.h
	SOURCES
		src/CommandQueue.cpp
		src/Controller.cpp
		src/DefinitionRegistry.cpp
		src/Reason.cpp
		src/SaveGame.cpp
		src/SimWorld.cpp
		src/Snapshot.cpp
		src/Stepper.cpp
	TESTS
		tests/DefinitionTests.cpp
		tests/FastForwardTests.cpp
		tests/ReasonTests.cpp
		tests/SaveGameTests.cpp
		tests/SimTests.cpp
		tests/SnapshotTests.cpp
)

if(TARGET jettison_sim_tests)
	# A definition's migration edits a YAML::Node.
	target_link_libraries(jettison_sim_tests PRIVATE yaml-cpp::yaml-cpp)
endif()
