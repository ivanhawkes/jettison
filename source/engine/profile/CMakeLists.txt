# Tracy is public: the macros in Profile.h expand to Tracy's in the code that uses them, and its
# TRACY_ENABLE definition travels with the TracyClient target, so a module that profiles and the
# client it reports to cannot disagree about whether profiling is compiled in.
jettison_add_module(profile
	DEPENDS
		core
	HEADERS
		include/jettison/profile/Profile.h
	SOURCES
		src/Profile.cpp
	TESTS
		tests/ProfileTests.cpp
	PUBLIC_LIBRARIES
		Tracy::TracyClient
)
