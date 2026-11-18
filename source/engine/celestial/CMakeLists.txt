jettison_add_module(celestial
	DETERMINISTIC
	DEPENDS
		core
		math
		time
		reflect
		serialise
		vfs
	HEADERS
		include/jettison/celestial/Frames.h
		include/jettison/celestial/Galaxy.h
		include/jettison/celestial/Orbit.h
		include/jettison/celestial/StarSystem.h
	SOURCES
		src/Frames.cpp
		src/Galaxy.cpp
		src/Orbit.cpp
		src/StarSystem.cpp
	TESTS
		tests/CelestialTests.cpp
		tests/GalaxyTests.cpp
		tests/StarSystemTests.cpp
	PUBLIC_LIBRARIES
		glm
)

# The sample system's and galaxy's own tests read the sample's files, not copies of them.
if(TARGET jettison_celestial_tests)
	target_compile_definitions(jettison_celestial_tests PRIVATE
		JETTISON_SAMPLE_ASSET_DIR="${PROJECT_SOURCE_DIR}/source/samples/space/assets")
endif()
