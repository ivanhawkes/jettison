# The ground sample (docs/sample-game.md): a rover driven on the ground under gravity, a game of no
# space composed into the shell (App::Game). It links the shell alone, which links no space module
# (jettison_check_optional), and the smoke test runs it beside the space sample.
add_library(jettison_sample_ground STATIC
	include/jettison/sample/GroundGame.h
	include/jettison/sample/RoverInput.h
	src/Ground.cpp
	src/Ground.h
	src/GroundGame.cpp
	src/RoverInput.cpp
)
target_include_directories(jettison_sample_ground
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(jettison_sample_ground PUBLIC Jettison::App PRIVATE jettison_warnings)
set_property(TARGET jettison_sample_ground PROPERTY FOLDER "Samples")

# Its content, mounted as "base" over the engine's assets: its scene and its package description.
jettison_add_assets(jettison_sample_ground_assets
	SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/assets"
	DIRECTORIES scene
	FILES mod.yaml
)
set_property(TARGET jettison_sample_ground_assets PROPERTY FOLDER "Samples")

add_executable(ground main.cpp)
jettison_use_shared_assets(ground)
jettison_use_content(ground jettison_sample_ground_assets)
set_property(TARGET ground PROPERTY FOLDER "Samples")
target_link_libraries(ground PRIVATE jettison_sample_ground jettison_warnings)

if(JETTISON_BUILD_TESTS)
	add_executable(jettison_sample_ground_tests tests/GroundTests.cpp)
	target_include_directories(jettison_sample_ground_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
	target_link_libraries(jettison_sample_ground_tests PRIVATE
		jettison_sample_ground doctest::doctest_with_main jettison_warnings)
	target_compile_definitions(jettison_sample_ground_tests PRIVATE
		JETTISON_GROUND_SCENE="${CMAKE_CURRENT_SOURCE_DIR}/assets/scene/main.yaml")
	set_property(TARGET jettison_sample_ground_tests PROPERTY FOLDER "Test")
	add_test(NAME sample-ground COMMAND jettison_sample_ground_tests)
endif()
