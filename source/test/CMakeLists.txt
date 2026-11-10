cmake_minimum_required(VERSION 3.24)

# The render smoke harness. It was "test" until CTest arrived: CMake reserves that target name once
# testing is enabled, and Ninja generates a phony source/test/test for the directory's tests,
# which an executable of that name in this directory collides with.
add_executable(render_test main.cpp)
jettison_use_shared_assets(render_test)

if(JETTISON_BUILD_TESTS)
	add_executable(smoke_sequence_test smoke_sequence_test.cpp)
	target_link_libraries(smoke_sequence_test PRIVATE Renderer doctest::doctest_with_main jettison_warnings)
	set_property(TARGET smoke_sequence_test PROPERTY FOLDER "Test")
	add_test(NAME smoke_sequence COMMAND smoke_sequence_test)
endif()

# One executable linking the engine and every third-party library it has or plans to have (see
# source/external/CMakeLists.txt). The gate runs it after the Debug build.
if(JETTISON_PLANNED_DEPENDENCIES)
	add_executable(dependency_probe dependency_probe.cpp)
	target_link_libraries(dependency_probe PRIVATE
		Renderer
		doctest::doctest
		spdlog::spdlog
		Tracy::TracyClient
		GPUOpen::VulkanMemoryAllocator
		FastNoiseLite
		mikktspace
		Jolt
		Freetype::Freetype
		RmlUi::RmlUi
		miniaudio
		ktx_read
		wamr
		jettison_warnings
	)
	set_property(TARGET dependency_probe PROPERTY FOLDER "Test")
	if(JETTISON_BUILD_TESTS)
		add_test(NAME dependency_probe COMMAND dependency_probe)
	endif()
endif()

# Move the targets into a solution folder.
set_property(TARGET render_test PROPERTY FOLDER "Test")

# Find and link Vulkan.
find_package(Vulkan REQUIRED)
target_link_libraries(render_test PRIVATE Vulkan::Vulkan)

# Configuration and scene graph libraries.
target_link_libraries(render_test PRIVATE yaml-cpp::yaml-cpp EnTT::EnTT)

target_link_libraries(render_test PRIVATE Renderer)

# Project warnings.
target_link_libraries(render_test PRIVATE jettison_warnings)
