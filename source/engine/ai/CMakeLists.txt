# The AI that knows no genre (docs/architecture.md, "AI"): utility decisions, behaviour trees, graph
# search, courses round spheres, and steering, which flies a body through physics' flight controls.
# Space transit (cruise, gates, lanes, galaxy routes) is the space module's.
jettison_add_module(ai
	DETERMINISTIC
	DEPENDS
		core
		math
		physics
		profile
		reflect
		serialise
		sim
		spatial
	HEADERS
		include/jettison/ai/BehaviourTree.h
		include/jettison/ai/Courses.h
		include/jettison/ai/Pathfinding.h
		include/jettison/ai/Steering.h
		include/jettison/ai/Types.h
		include/jettison/ai/UtilityAi.h
	SOURCES
		src/Ai.cpp
		src/BehaviourTree.cpp
		src/Courses.cpp
		src/Pathfinding.cpp
		src/Steering.cpp
		src/UtilityAi.cpp
	TESTS
		tests/BehaviourTreeTests.cpp
		tests/CoursesTests.cpp
		tests/PathfindingTests.cpp
		tests/SteeringTests.cpp
		tests/UtilityAiTests.cpp
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
)

# Steering's suite flies round a station on its orbit in the space suite's Halcyon (Systems.h): the
# test's, not the library's, which links no space module.
if(TARGET jettison_ai_tests)
	target_link_libraries(jettison_ai_tests PRIVATE jettison_space)
	target_include_directories(jettison_ai_tests PRIVATE
		"${PROJECT_SOURCE_DIR}/source/engine/space/tests")
endif()
