# Platforms' berths and the vehicles on them (docs/history.md, catalogue entry 31): a simulation
# module, above ai, whose steering flies a vehicle in, and physics, which clamps it.
jettison_add_module(dock
	DETERMINISTIC
	DEPENDS
		ai
		core
		math
		physics
		profile
		reflect
		serialise
		sim
		spatial
	HEADERS
		include/jettison/dock/Definitions.h
		include/jettison/dock/Docks.h
	SOURCES
		src/Definitions.cpp
		src/Docks.cpp
		src/Types.cpp
	TESTS
		tests/DefinitionTests.cpp
		tests/PadTests.cpp
		tests/ApproachTests.cpp
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
	PRIVATE_LIBRARIES
		yaml-cpp::yaml-cpp
)

# The suites read the retired space sample's vehicles and platforms (tests/fixtures), and fly in a
# frame of their own (tests/Fixture.h).
if(TARGET jettison_dock_tests)
	target_compile_definitions(jettison_dock_tests PRIVATE
		JETTISON_DOCK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
endif()
