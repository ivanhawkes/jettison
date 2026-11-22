# Transit, of no genre (docs/architecture.md, "Transit"): motion profiles, which say in closed form
# how far a vehicle, a lift or a train has come along its way by a time, and passages, which carry an
# entity out of physics along a way and bring it out clear of what is there.
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
		include/jettison/transit/Profile.h
		include/jettison/transit/Types.h
	SOURCES
		src/Passage.cpp
		src/Path.cpp
		src/Profile.cpp
		src/Types.cpp
	TESTS
		tests/PassageTests.cpp
		tests/ProfileTests.cpp
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
)
