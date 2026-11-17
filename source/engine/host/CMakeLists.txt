# The CPU host a game starts through: one session's file system, definitions and world, built in
# phases from the game's explicit composition, with no window, renderer or sample content.
jettison_add_module(host
	DEPENDS
		core
		reflect
		serialise
		sim
		vfs
	HEADERS
		include/jettison/host/Package.h
		include/jettison/host/Session.h
	SOURCES
		src/Package.cpp
		src/Session.cpp
	TESTS
		tests/HostTests.cpp
)
