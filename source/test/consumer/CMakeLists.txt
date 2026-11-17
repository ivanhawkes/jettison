cmake_minimum_required(VERSION 3.28)

# The scratch consumer: what the game repository's build looks like at its smallest, and the CPU
# engine's independent consumer (Manifest's H04). It fetches the engine with the presentation
# modules off, links Jettison::Engine and runs a game with nothing of space in it through the public
# host: definitions, commands, steps, a save, a load and teardown. It is not part of the gate's
# unit tests, because a second fetch of the dependency set costs minutes the gate does not need to
# spend twice. docs/build-and-gates.md says how to run it.
project(JettisonConsumer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# The game pins a commit here. The consumer check points at the working tree instead, so it tests
# the engine as it is about to be committed.
# EXCLUDE_FROM_ALL: the engine's executables are not the game's to build; only what the game links
# gets built.
# One of the two is given, so a check is of a revision it names: the working tree, or the commit
# the game pins. There is no default to a branch that moves.
if(JETTISON_SOURCE_DIR)
	FetchContent_Declare(jettison SOURCE_DIR "${JETTISON_SOURCE_DIR}" EXCLUDE_FROM_ALL)
elseif(JETTISON_GIT_TAG)
	FetchContent_Declare(
		jettison
		GIT_REPOSITORY https://github.com/ivanhawkes/jettison.git
		GIT_TAG "${JETTISON_GIT_TAG}"
		EXCLUDE_FROM_ALL
	)
else()
	message(FATAL_ERROR "Give JETTISON_SOURCE_DIR (a checkout) or JETTISON_GIT_TAG (a commit)")
endif()
# The engine's own tests and probe are the engine's business; the planned set is not needed to
# link what exists.
set(JETTISON_BUILD_TESTS OFF)
# No window, renderer or sample content: the CPU engine a server builds.
set(JETTISON_PRESENTATION OFF)
set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan ON)
set(JETTISON_PLANNED_DEPENDENCIES OFF)
FetchContent_MakeAvailable(jettison)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Jettison::Engine)
# The fixture's content is the consumer's own, found from its source: no sample pack is read.
target_compile_definitions(consumer PRIVATE CONSUMER_CONTENT="${CMAKE_CURRENT_SOURCE_DIR}/content")

enable_testing()
add_test(NAME consumer COMMAND consumer)
