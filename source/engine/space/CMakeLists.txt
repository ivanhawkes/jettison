# The space capability, optional over the neutral modules (docs/architecture.md, "Module graph"):
# star systems as physics frames, and space transit, cruising across a system, lanes between
# systems, the gates that send ships down them, and routes over the galaxy. A game that has no star
# systems links none of it.
jettison_add_module(space
	DETERMINISTIC
	DEPENDS
		ai
		celestial
		core
		math
		physics
		profile
		reflect
		serialise
		sim
		spatial
		transit
	HEADERS
		include/jettison/space/Cruise.h
		include/jettison/space/Gates.h
		include/jettison/space/Lanes.h
		include/jettison/space/Routes.h
		include/jettison/space/StarSystemFrame.h
		include/jettison/space/Types.h
	SOURCES
		src/Cruise.cpp
		src/Gates.cpp
		src/Lanes.cpp
		src/Routes.cpp
		src/StarSystemFrame.cpp
		src/Types.cpp
	TESTS
		tests/ContactTests.cpp
		tests/CruiseTests.cpp
		tests/FlightTests.cpp
		tests/GatesTests.cpp
		tests/Halcyon.h
		tests/LanesTests.cpp
		tests/RoutesTests.cpp
		tests/Systems.h
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
)

# The lanes order themselves after the docks by the system's name, with no link between the modules;
# the suite attaches both to show nothing orders them by chance.
if(TARGET jettison_space_tests)
	target_link_libraries(jettison_space_tests PRIVATE jettison_dock)
endif()
