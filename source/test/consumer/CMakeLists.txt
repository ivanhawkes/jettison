cmake_minimum_required(VERSION 3.28)

# The scratch consumer: what the game repository's build looks like at its smallest. It fetches
# the engine, links Jettison::Engine, and calls into two modules through the public include
# paths. Phase 1's exit criterion is that this configures, builds and runs; it is not part of the
# gate, because a second fetch of the dependency set costs minutes the gate does not need to
# spend twice. docs/build-and-gates.md says how to run it.
project(JettisonConsumer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# The game pins a commit here. The consumer check points at the working tree instead, so it tests
# the engine as it is about to be committed.
# EXCLUDE_FROM_ALL: the engine's executables are not the game's to build; only what the game links
# gets built.
if(JETTISON_SOURCE_DIR)
	FetchContent_Declare(jettison SOURCE_DIR "${JETTISON_SOURCE_DIR}" EXCLUDE_FROM_ALL)
else()
	FetchContent_Declare(
		jettison
		GIT_REPOSITORY https://github.com/ivanhawkes/jettison.git
		GIT_TAG main
		EXCLUDE_FROM_ALL
	)
endif()
# The engine's own tests and probe are the engine's business; the planned set is not needed to
# link what exists.
set(JETTISON_BUILD_TESTS OFF)
set(JETTISON_PLANNED_DEPENDENCIES OFF)
FetchContent_MakeAvailable(jettison)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Jettison::Engine)
