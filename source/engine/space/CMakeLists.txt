# The space capability, optional over the neutral modules (docs/architecture.md, "Module graph"):
# star systems as physics frames. A game that has no star systems links none of it.
jettison_add_module(space
	DETERMINISTIC
	DEPENDS
		celestial
		core
		math
		physics
		sim
	HEADERS
		include/jettison/space/StarSystemFrame.h
	SOURCES
		src/StarSystemFrame.cpp
	TESTS
		tests/ContactTests.cpp
		tests/FlightTests.cpp
		tests/Halcyon.h
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
)
