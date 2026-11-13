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
		Jettison::Physics
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
		zstd::zstd
		wamr
		jettison_warnings
	)
	set_property(TARGET dependency_probe PROPERTY FOLDER "Test")
	if(JETTISON_BUILD_TESTS)
		add_test(NAME dependency_probe COMMAND dependency_probe)
	endif()
endif()

# The smoke test's mod, packed as a player would be given it (scripts/smoke-test.sh --packed): its
# definitions (mods/smoke) and its script, built from Rust (scripts/smoke.rs) against the shell's
# bindings, staged together in the build tree as the mod's directory, which the app suite mounts as a
# player's unpacked mod. Staged with the tree rather than as the suite's dependency, so a change to
# the shell's types still builds the suite that says the bindings are out of date.
set(JETTISON_SMOKE_MOD "${CMAKE_CURRENT_BINARY_DIR}/mods/smoke")
add_custom_target(render_test_smoke_mod_files
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${JETTISON_SMOKE_MOD}/defs"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/mods/smoke" "${JETTISON_SMOKE_MOD}"
	COMMENT "Staging the smoke mod's definitions"
	VERBATIM)
jettison_add_script(render_test_smoke_script ALL
	OUTPUT "${JETTISON_SMOKE_MOD}/scripts/smoke.wasm"
	SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/scripts/smoke.rs")
add_custom_target(render_test_smoke_mod_staged ALL DEPENDS render_test_smoke_mod_files render_test_smoke_script)
jettison_add_pack(render_test_smoke_mod
	SOURCE "${JETTISON_SMOKE_MOD}"
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mods/smoke.pack"
	DEPENDS render_test_smoke_mod_staged
)
