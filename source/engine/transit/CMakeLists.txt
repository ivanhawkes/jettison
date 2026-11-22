# Transit, of no genre (docs/architecture.md, "Transit"): motion profiles, which say in closed form
# how far a vehicle, a lift or a train has come along its way by a time; passages, which carry an
# entity out of physics along a way and bring it out clear of what is there; portals, which take an
# entity out of its frame for a set time and bring it out at an exit in any frame; and slots, which
# one entity holds at a time with others waiting, a gated portal's among them.
jettison_add_module(transit
	DETERMINISTIC
	DEPENDS
		ai
		core
		physics
		reflect
		serialise
		sim
	HEADERS
		include/jettison/transit/Passage.h
		include/jettison/transit/Path.h
		include/jettison/transit/Portal.h
		include/jettison/transit/Profile.h
		include/jettison/transit/Slot.h
		include/jettison/transit/Types.h
	SOURCES
		src/Passage.cpp
		src/Path.cpp
		src/Portal.cpp
		src/Profile.cpp
		src/Types.cpp
	TESTS
		tests/PassageTests.cpp
		tests/PortalTests.cpp
		tests/ProfileTests.cpp
		tests/SlotTests.cpp
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
)
