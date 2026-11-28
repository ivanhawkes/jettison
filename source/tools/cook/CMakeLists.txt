# The cook: a source tree of models, images and other content into the tree a game reads
# (docs/dense-geometry-and-ray-tracing.md, "The cook"). CPU only, so it builds with presentation off.
# The cooking is a library, which the tool and its suite link. It links ktx, libktx's encoder, never
# ktx_read beside it (source/external/CMakeLists.txt says why), and compiles stb_image's
# implementation in Texture.cpp.
add_library(jettison_cook_tree STATIC
	CookTree.cpp CookTree.h Settings.cpp Settings.h Texture.cpp Texture.h Watch.cpp Watch.h)
target_include_directories(jettison_cook_tree PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(jettison_cook_tree
	PUBLIC Jettison::Geometry
	PRIVATE jettison_collision_bake Jettison::Physics Jettison::Platform Jettison::Serialise
		Jettison::Reflect Jettison::Core fastgltf ktx stb jettison_warnings)
set_property(TARGET jettison_cook_tree PROPERTY FOLDER "Tools")

add_executable(jettison_cook main.cpp)
set_property(TARGET jettison_cook PROPERTY FOLDER "Tools")
target_link_libraries(jettison_cook PRIVATE jettison_cook_tree Jettison::Core jettison_warnings)

# The suite writes its fixture tree, the project's own, into a temporary directory and cooks it.
if(JETTISON_BUILD_TESTS)
	add_executable(jettison_cook_tests tests/CookTests.cpp)
	target_link_libraries(jettison_cook_tests PRIVATE
		jettison_cook_tree Jettison::Physics Jettison::Core fastgltf ktx stb doctest::doctest_with_main
		jettison_warnings)
	set_target_properties(jettison_cook_tests PROPERTIES FOLDER "Test")
	add_test(NAME cook COMMAND jettison_cook_tests)
endif()
