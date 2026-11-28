# The engine's test content (content/), which render_test mounts over the engine's assets as "base":
# static scenes (a planet and a station at real scale, a ringed giant, the transparent pass's, the
# post chain's and the levels of detail's benches, the texture table's, models and a skinned clip), a
# fixture program of sixteen texture slots, their materials, textures and models, a clip, a sequence,
# a grade and a few sounds.
# The engine's suites read the sources; everything else reads the tree jettison_cook makes of them
# (docs/dense-geometry-and-ray-tracing.md, "The cook"). The sources are staged as the cook's source
# tree, content-source/, where the dense test meshes are generated beside them, and cooked into
# content/, which the cook's record makes cheap when nothing changed, as the packer runs on every
# build. Its pack, content.pack beside the cooked tree, is built for the smoke test's packed run.
set(content_source "${CMAKE_CURRENT_BINARY_DIR}/content-source")
set(content_cooked "${CMAKE_CURRENT_BINARY_DIR}/content")
jettison_add_assets(jettison_test_content_source
	SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/content"
	OUTPUT "${content_source}"
	DIRECTORIES animations cinematics grades materials models scene
	CONFIGURED audio textures
	FILES mod.yaml shaders/slots.program.yaml
)
set_property(TARGET jettison_test_content_source PROPERTY FOLDER "Test")

# The dense test meshes' generator, CPU only. The same bytes in every build type: no contraction into
# FMAs, which a build type could do differently.
add_executable(jettison_test_meshes test_meshes.cpp)
target_link_libraries(jettison_test_meshes PRIVATE Jettison::Core fastgltf glm jettison_warnings)
target_compile_options(jettison_test_meshes PRIVATE -ffp-contract=off)
set_property(TARGET jettison_test_meshes PROPERTY FOLDER "Test")
# What it writes, named so that a mesh the staging's configure removes with the models it does not
# know is generated again.
set(test_meshes
	models/asteroids/asteroid-large.glb
	models/asteroids/asteroid-small.glb
	models/station/station-corridor.glb
	models/station/station-junction.glb
	models/station/station-panel.glb
)
list(TRANSFORM test_meshes PREPEND "${content_source}/")
add_custom_command(
	OUTPUT ${test_meshes}
	COMMAND jettison_test_meshes "${content_source}"
	DEPENDS jettison_test_meshes
	COMMENT "Generating the dense test meshes"
	VERBATIM
)

add_custom_target(jettison_test_content
	COMMAND jettison_cook "${content_source}" "${content_cooked}"
	DEPENDS ${test_meshes}
	COMMENT "Cooking the test content"
	VERBATIM
)
add_dependencies(jettison_test_content jettison_test_content_source jettison_cook)
set_property(TARGET jettison_test_content PROPERTY JETTISON_ASSET_DIRECTORY "${content_cooked}")
# The fixture programs, compiled into the cooked tree beside their descriptors, which the cook leaves
# alone, as it does every file its record does not name.
jettison_compile_shaders(jettison_test_content content/shaders/slots.frag)
jettison_compile_shader_variant(jettison_test_content
	SOURCE ../../assets/shaders/mesh.vert OUTPUT content/shaders/slots.vert DEFINES UNLIT)
set_property(TARGET jettison_test_content PROPERTY FOLDER "Test")
jettison_add_pack(jettison_test_content_pack
	SOURCE "${content_cooked}"
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/content.pack"
	DEPENDS jettison_test_content
)

# The render smoke harness. It was "test" until CTest arrived: CMake reserves that target name once
# testing is enabled, and Ninja generates a phony source/test/test for the directory's tests,
# which an executable of that name in this directory collides with.
add_executable(render_test main.cpp)
jettison_use_shared_assets(render_test)
jettison_use_content(render_test jettison_test_content)
set_property(TARGET render_test PROPERTY FOLDER "Test")
target_link_libraries(render_test PRIVATE Jettison::App jettison_warnings)

# Devtools alone, as Manifest's terminal tool links it: no Vulkan loader or GLFW among its NEEDED,
# and it runs with neither mapped (Manifest's runtime distribution contract, R06).
add_executable(devtools_alone devtools_alone.cpp)
target_link_libraries(devtools_alone PRIVATE Jettison::Devtools jettison_warnings)
set_property(TARGET devtools_alone PROPERTY FOLDER "Test")
if(JETTISON_BUILD_TESTS)
	add_test(NAME devtools_alone COMMAND "${CMAKE_COMMAND}" -DREADELF=${CMAKE_READELF}
		-DEXECUTABLE=$<TARGET_FILE:devtools_alone> -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckDevtoolsAlone.cmake")
endif()

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
		meshoptimizer
		Jolt
		Freetype::Freetype
		RmlUi::RmlUi
		miniaudio
		ktx_read
		zstd::zstd
		wamr
		enet
		sodium
		jettison_warnings
	)
	set_property(TARGET dependency_probe PROPERTY FOLDER "Test")
	if(JETTISON_BUILD_TESTS)
		add_test(NAME dependency_probe COMMAND dependency_probe)
	endif()
endif()
