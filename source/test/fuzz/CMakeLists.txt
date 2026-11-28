# The fuzz targets (docs/build-and-gates.md, "Fuzzing"): one libFuzzer program a format the engine
# loads, each its loading path given an input as a file of that format. Built in the clang-fuzz tree,
# where everything is compiled with libFuzzer's coverage and the sanitisers, and linked here with
# libFuzzer's main. Each target's seed corpus is written into the tree as it builds, and its replay,
# every seed run once, is a test.
set(seeds "${CMAKE_CURRENT_BINARY_DIR}/seeds")

add_custom_target(jettison_fuzz)
set_property(TARGET jettison_fuzz PROPERTY FOLDER "Fuzz")

# The seed corpora: the repository's files of each format, and those the generator writes.
add_executable(jettison_fuzz_seeds Seeds.cpp)
target_include_directories(jettison_fuzz_seeds PRIVATE "${PROJECT_SOURCE_DIR}/source/engine/net/src")
target_link_libraries(jettison_fuzz_seeds PRIVATE
	Jettison::Geometry Jettison::Net Jettison::Physics Jettison::Sim Jettison::Vfs
	jettison_collision_bake enet stb jettison_warnings)
set_property(TARGET jettison_fuzz_seeds PROPERTY FOLDER "Fuzz")
file(GLOB_RECURSE seed_sources CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/source/test/content/animations/*"
	"${PROJECT_SOURCE_DIR}/source/test/content/audio/*"
	"${PROJECT_SOURCE_DIR}/source/test/content/grades/*"
	"${PROJECT_SOURCE_DIR}/source/test/content/materials/*"
	"${PROJECT_SOURCE_DIR}/source/test/content/models/*"
	"${PROJECT_SOURCE_DIR}/source/test/content/textures/*.ktx2"
	"${PROJECT_SOURCE_DIR}/source/tools/collision/tests/models/*"
	"${PROJECT_SOURCE_DIR}/assets/fonts/*"
	"${PROJECT_SOURCE_DIR}/assets/strings/*"
	"${PROJECT_SOURCE_DIR}/assets/ui/*")
add_custom_command(
	OUTPUT "${seeds}/seeds.stamp"
	COMMAND jettison_fuzz_seeds "${PROJECT_SOURCE_DIR}" "${seeds}"
	COMMAND "${CMAKE_COMMAND}" -E touch "${seeds}/seeds.stamp"
	DEPENDS jettison_fuzz_seeds ${seed_sources}
	COMMENT "Writing the fuzz targets' seed corpora"
	VERBATIM)
add_custom_target(jettison_fuzz_corpora DEPENDS "${seeds}/seeds.stamp")

# The script target's seeds are the script suite's modules, compiled into its corpus.
set(script_sources "${PROJECT_SOURCE_DIR}/source/engine/script/tests/scripts")
foreach(module IN ITEMS events.c keys.c reads.c limits.rs)
	string(REPLACE "." "_" name "${module}")
	jettison_add_script(jettison_fuzz_seed_${name}
		OUTPUT "${seeds}/script/${name}.wasm"
		SOURCES "${script_sources}/${module}"
		INCLUDES "${script_sources}")
	add_dependencies(jettison_fuzz_corpora jettison_fuzz_seed_${name})
endforeach()

# A target: <name>.cpp, the libraries it reads with, and the private headers of the module whose
# loading path it calls into. Its replay runs every seed once (-runs=0), and every input committed
# under regressions/<name>, each one that crashed it, made of the project's own files; and fails on
# no seeds.
function(jettison_add_fuzz_target name)
	cmake_parse_arguments(PARSE_ARGV 1 fuzz "" "SOURCE" "LIBRARIES;INCLUDES")
	set(target jettison_fuzz_${name})
	add_executable(${target} ${fuzz_SOURCE})
	target_include_directories(${target} PRIVATE ${fuzz_INCLUDES})
	target_link_libraries(${target} PRIVATE ${fuzz_LIBRARIES} jettison_warnings)
	target_link_options(${target} PRIVATE -fsanitize=fuzzer)
	target_compile_definitions(${target} PRIVATE JETTISON_ASSET_SOURCE_DIR="${PROJECT_SOURCE_DIR}/assets")
	set_property(TARGET ${target} PROPERTY FOLDER "Fuzz")
	add_dependencies(${target} jettison_fuzz_corpora)
	add_dependencies(jettison_fuzz ${target})
	set(corpora "${seeds}/${name}")
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/regressions/${name}")
		list(APPEND corpora "${CMAKE_CURRENT_SOURCE_DIR}/regressions/${name}")
	endif()
	add_test(NAME fuzz_${name} COMMAND ${target} -runs=0 ${corpora})
	set_tests_properties(fuzz_${name} PROPERTIES FAIL_REGULAR_EXPRESSION "INFO: +0 files found")
endfunction()

set(render_private "${PROJECT_SOURCE_DIR}/source/engine/render/src")
jettison_add_fuzz_target(gltf SOURCE Gltf.cpp
	LIBRARIES Jettison::Animation Jettison::Geometry Jettison::Render Vulkan::Vulkan INCLUDES "${render_private}")
jettison_add_fuzz_target(pack SOURCE Pack.cpp LIBRARIES Jettison::Vfs)
jettison_add_fuzz_target(ktx2 SOURCE Ktx2.cpp LIBRARIES Jettison::Render)
jettison_add_fuzz_target(image SOURCE Image.cpp LIBRARIES Jettison::Render)
jettison_add_fuzz_target(save SOURCE Save.cpp LIBRARIES Jettison::Physics Jettison::Sim)
jettison_add_fuzz_target(net SOURCE Net.cpp
	LIBRARIES Jettison::Net enet INCLUDES "${PROJECT_SOURCE_DIR}/source/engine/net/src")
jettison_add_fuzz_target(collision SOURCE Collision.cpp LIBRARIES Jettison::Physics Jettison::Sim)
jettison_add_fuzz_target(clip SOURCE Clip.cpp LIBRARIES Jettison::Animation)
jettison_add_fuzz_target(strings SOURCE Strings.cpp LIBRARIES Jettison::Ui)
jettison_add_fuzz_target(script SOURCE Script.cpp LIBRARIES Jettison::Script)
jettison_add_fuzz_target(audio SOURCE Audio.cpp LIBRARIES Jettison::Audio)
jettison_add_fuzz_target(font SOURCE Font.cpp
	LIBRARIES Jettison::Render Jettison::Ui Freetype::Freetype imgui INCLUDES "${render_private}")
jettison_add_fuzz_target(rml SOURCE Rml.cpp LIBRARIES Jettison::Ui)
jettison_add_fuzz_target(rcss SOURCE Rcss.cpp LIBRARIES Jettison::Ui)
