# Stations' pads and the ships on them (docs/history.md, catalogue entry 31): a simulation module,
# above ai, whose steering flies a ship in, and physics, which clamps it.
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
		include/jettison/dock/Roads.h
	SOURCES
		src/Definitions.cpp
		src/Deck.cpp
		src/Docks.cpp
		src/Roads.cpp
		src/Types.cpp
	TESTS
		tests/DefinitionTests.cpp
		tests/PadTests.cpp
		tests/ApproachTests.cpp
		tests/DeckTests.cpp
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
	PRIVATE_LIBRARIES
		yaml-cpp::yaml-cpp
)

# The suites read the sample's hulls and stations, and the star system they are in, which space
# makes a physics frame.
if(TARGET jettison_dock_tests)
	target_compile_definitions(jettison_dock_tests PRIVATE
		JETTISON_SAMPLE_ASSET_DIR="${PROJECT_SOURCE_DIR}/source/samples/space/assets")
	target_link_libraries(jettison_dock_tests PRIVATE jettison_space)
endif()
