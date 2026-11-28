# The collision baker: a model's collision shape into the file physics reads
# (physics/CollisionFile.h), world content a server collides with and reads no model for. The baking is
# a library, which the cook (source/tools/cook) calls for every model it cooks, and the tool for one.
add_library(jettison_collision_bake STATIC Bake.cpp Bake.h)
target_include_directories(jettison_collision_bake PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(jettison_collision_bake
	PUBLIC Jettison::Physics
	PRIVATE Jettison::Geometry fastgltf jettison_warnings)
# The same model bakes to the same bytes in every build type, as a simulation module computes: a
# node's transform fused into FMAs or not would move a point by a rounding.
target_compile_options(jettison_collision_bake PRIVATE -ffp-contract=off)
set_property(TARGET jettison_collision_bake PROPERTY FOLDER "Tools")

add_executable(jettison_bake_collision main.cpp)
set_property(TARGET jettison_bake_collision PROPERTY FOLDER "Tools")
target_link_libraries(jettison_bake_collision
	PRIVATE jettison_collision_bake Jettison::Core jettison_warnings)

# The suite bakes the models in tests/models, written for it, the project's own; the CLI's test runs
# the tool on one twice and compares the files.
if(JETTISON_BUILD_TESTS)
	add_executable(jettison_collision_bake_tests tests/BakeTests.cpp)
	target_link_libraries(jettison_collision_bake_tests PRIVATE
		jettison_collision_bake Jettison::Sim Jettison::Vfs doctest::doctest_with_main
		jettison_warnings)
	target_compile_definitions(jettison_collision_bake_tests PRIVATE
		JETTISON_BAKE_MODELS="${CMAKE_CURRENT_SOURCE_DIR}/tests/models")
	set_target_properties(jettison_collision_bake_tests PROPERTIES FOLDER "Test")
	add_test(NAME bake_collision COMMAND jettison_collision_bake_tests)
	add_test(NAME bake_collision_cli COMMAND ${CMAKE_COMMAND}
		"-DTOOL=$<TARGET_FILE:jettison_bake_collision>"
		"-DMODELS=${CMAKE_CURRENT_SOURCE_DIR}/tests/models"
		"-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cli"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunTool.cmake")
endif()
