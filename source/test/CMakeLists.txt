# The render smoke harness. It was "test" until CTest arrived: CMake reserves that target name once
# testing is enabled, and Ninja generates a phony source/test/test for the directory's tests,
# which an executable of that name in this directory collides with.
add_executable(render_test main.cpp)
jettison_use_shared_assets(render_test)
set_property(TARGET render_test PROPERTY FOLDER "Test")
target_link_libraries(render_test PRIVATE Jettison::App jettison_warnings)

# One executable linking the engine and every third-party library it has or plans to have (see
# source/external/CMakeLists.txt). The gate runs it after the Debug build. The libraries the
# engine links privately are named here as well, because the probe calls each of them directly.
if(JETTISON_PLANNED_DEPENDENCIES)
	add_executable(dependency_probe dependency_probe.cpp)
	target_link_libraries(dependency_probe PRIVATE
		Jettison::App
		glfw
		imgui
		stb
		yaml-cpp::yaml-cpp
		fastgltf
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
