# Jolt in double precision, as a service of the simulation world. Jolt is private: its headers and
# its AVX2 flags stay in this module's translation units, and the engine sees bodies as entities.
jettison_add_module(physics
	DETERMINISTIC
	DEPENDS
		celestial
		core
		math
		profile
		reflect
		serialise
		sim
	HEADERS
		include/jettison/physics/Components.h
		include/jettison/physics/Flight.h
		include/jettison/physics/PhysicsWorld.h
		include/jettison/physics/Shape.h
	SOURCES
		src/Components.cpp
		src/Convert.h
		src/Flight.cpp
		src/JoltRuntime.cpp
		src/JoltRuntime.h
		src/PhysicsWorld.cpp
		src/Shapes.cpp
		src/Shapes.h
		src/StarSystemWorld.cpp
		src/StarSystemWorld.h
	TESTS
		tests/ContactTests.cpp
		tests/FlightTests.cpp
		tests/Halcyon.h
		tests/PhysicsTests.cpp
		tests/ReplayTests.cpp
	PUBLIC_LIBRARIES
		glm
		EnTT::EnTT
	PRIVATE_LIBRARIES
		Jolt
)

# Jolt's profiler hooks (src/JoltProfile.cpp), linked by Jolt itself, so whatever links Jolt has
# them: Jolt references them from most of its objects, and an archive member is only pulled in for
# a symbol something before it needs.
add_library(jettison_jolt_profile STATIC src/JoltProfile.cpp)
target_link_libraries(jettison_jolt_profile PRIVATE Jolt Jettison::Profile $<BUILD_INTERFACE:jettison_warnings>)
set_target_properties(jettison_jolt_profile PROPERTIES FOLDER "Engine")
target_link_libraries(Jolt PUBLIC jettison_jolt_profile)
