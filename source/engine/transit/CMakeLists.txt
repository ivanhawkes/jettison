# Transit, of no genre (docs/space-extraction-plan.md, slice 4): motion profiles, which say in closed
# form how far a vehicle, a lift or a train has come along its way by a time.
jettison_add_module(transit
	DETERMINISTIC
	DEPENDS
		core
		reflect
		serialise
	HEADERS
		include/jettison/transit/Profile.h
		include/jettison/transit/Types.h
	SOURCES
		src/Profile.cpp
		src/Types.cpp
	TESTS
		tests/ProfileTests.cpp
)
