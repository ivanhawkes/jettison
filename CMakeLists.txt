cmake_minimum_required(VERSION 3.28)

project(Jettison VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Every compile goes through ccache when it is on the PATH, as it is in the devenv shell: a new
# worktree starts with no build trees (worktrunk copies none) and builds four of them, and a branch
# switched to and back rebuilds what it rebuilt before. The launcher is set before any target, so
# the fetched dependencies, most of the build, go through it too. A launcher the caller sets wins,
# and JETTISON_CCACHE=OFF turns it off. compile_commands.json leaves the launcher out, so clang-tidy
# and clangd see the compiler's own command.
option(JETTISON_CCACHE "Compile through ccache when it is found" ON)
if(JETTISON_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
	find_program(JETTISON_CCACHE_PROGRAM ccache)
	if(JETTISON_CCACHE_PROGRAM)
		set(CMAKE_C_COMPILER_LAUNCHER "${JETTISON_CCACHE_PROGRAM}")
		set(CMAKE_CXX_COMPILER_LAUNCHER "${JETTISON_CCACHE_PROGRAM}")
		message(STATUS "Jettison: compiling through ${JETTISON_CCACHE_PROGRAM}")
	endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(JettisonModule)

# The engine's unit tests: one doctest executable per module, registered with CTest. Off when the
# engine is a dependency of another project (the game repository's FetchContent), which builds
# its own tests and not ours.
option(JETTISON_BUILD_TESTS "Build the engine's unit tests and register them with CTest"
	${PROJECT_IS_TOP_LEVEL})
if(JETTISON_BUILD_TESTS)
	enable_testing()
endif()

find_package(Vulkan REQUIRED COMPONENTS glslc)

# Compiles each GLSL source to SPIR-V beside it in the build tree. Every program sees the shared
# includes in shaders/include (the frame layout, the vertex inputs, the colour helpers), and glslc
# writes a depfile naming the includes each one read, so editing an include rebuilds the programs
# that use it and no others. The depfiles are kept under CMakeFiles, out of the staged assets, which
# are packed as they are (jettison_add_pack).
function(jettison_compile_shaders target)
	set(shader_outputs)
	foreach(shader IN LISTS ARGN)
		set(output "${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv")
		set(depfile "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/shader-depfiles/${shader}.spv.d")
		get_filename_component(output_directory "${output}" DIRECTORY)
		get_filename_component(depfile_directory "${depfile}" DIRECTORY)
		add_custom_command(
			OUTPUT "${output}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}" "${depfile_directory}"
			COMMAND Vulkan::glslc --target-env=vulkan1.3
				-I "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include"
				-MD -MF "${depfile}"
				"${CMAKE_CURRENT_SOURCE_DIR}/${shader}" -o "${output}"
			DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" Vulkan::glslc
			DEPFILE "${depfile}"
			VERBATIM
		)
		list(APPEND shader_outputs "${output}")
	endforeach()
	add_custom_target(${target}_shaders DEPENDS ${shader_outputs})
	add_dependencies(${target} ${target}_shaders)
endfunction()

# One shader source built again under another name with preprocessor definitions: a program that
# is a variant of another (mesh_triplanar is mesh.vert and mesh.frag with TRIPLANAR), so the two
# cannot drift apart. The output is the program's stage path without .spv, as the source's is.
function(jettison_compile_shader_variant target)
	cmake_parse_arguments(PARSE_ARGV 1 variant "" "SOURCE;OUTPUT" "DEFINES")
	if(NOT variant_SOURCE OR NOT variant_OUTPUT)
		message(FATAL_ERROR "jettison_compile_shader_variant needs SOURCE and OUTPUT")
	endif()
	set(output "${CMAKE_CURRENT_BINARY_DIR}/${variant_OUTPUT}.spv")
	set(depfile "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/shader-depfiles/${variant_OUTPUT}.spv.d")
	get_filename_component(output_directory "${output}" DIRECTORY)
	get_filename_component(depfile_directory "${depfile}" DIRECTORY)
	set(defines)
	foreach(define IN LISTS variant_DEFINES)
		list(APPEND defines "-D${define}")
	endforeach()
	# The stage comes from the source's extension, which the output's name does not carry.
	get_filename_component(stage "${variant_SOURCE}" LAST_EXT)
	string(SUBSTRING "${stage}" 1 -1 stage)
	add_custom_command(
		OUTPUT "${output}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}" "${depfile_directory}"
		COMMAND Vulkan::glslc --target-env=vulkan1.3 -fshader-stage=${stage}
			-I "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include"
			${defines}
			-MD -MF "${depfile}"
			"${CMAKE_CURRENT_SOURCE_DIR}/${variant_SOURCE}" -o "${output}"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${variant_SOURCE}" Vulkan::glslc
		DEPFILE "${depfile}"
		VERBATIM
	)
	string(MAKE_C_IDENTIFIER "${variant_OUTPUT}" variant_target)
	add_custom_target(${target}_${variant_target} DEPENDS "${output}")
	add_dependencies(${target} ${target}_${variant_target})
endfunction()

# Turn on using solution folders.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Warnings live on an interface target that the project's own targets link privately, not
# on CMAKE_CXX_FLAGS: source/external pulls in third-party code through FetchContent, and
# flags on the global variable would reach every dependency. The set is measured on this
# tree, not aspirational: it is clean, so JETTISON_WERROR can gate on it.
add_library(jettison_warnings INTERFACE)
option(JETTISON_WERROR "Treat warnings as errors" OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
	target_compile_options(jettison_warnings INTERFACE
		-Wall
		-Wextra
		-Wpedantic
		-Wshadow
		-Wconversion
		-Wsign-conversion
		-Wshorten-64-to-32
		-Wimplicit-int-float-conversion
		-Wnon-virtual-dtor
		-Wextra-semi
		-Winconsistent-missing-destructor-override
		-Wcast-qual
		-Wcast-align
		-Wunreachable-code
		-Wdocumentation
	)
	if (JETTISON_WERROR)
		target_compile_options(jettison_warnings INTERFACE -Werror)
	endif()
endif()

# Installing and exporting the package is off by default: the game consumes the engine through
# FetchContent (source/test/consumer is that build at its smallest), and an install export has to
# carry every third-party target the modules link, which the FetchContent dependencies do not all
# provide. docs/build-and-gates.md, "Consuming the engine", has the state of it.
option(JETTISON_INSTALL "Install the engine and export the Jettison CMake package" OFF)

# Source.
add_subdirectory("source")

# The whole engine as one target, for a consumer that does not want to pick modules.
add_library(jettison_engine INTERFACE)
add_library(Jettison::Engine ALIAS jettison_engine)
set_target_properties(jettison_engine PROPERTIES EXPORT_NAME Engine)
get_property(jettison_modules GLOBAL PROPERTY JETTISON_MODULES)
target_link_libraries(jettison_engine INTERFACE ${jettison_modules})

if(JETTISON_INSTALL)
	include(GNUInstallDirs)
	include(CMakePackageConfigHelpers)
	install(TARGETS jettison_engine EXPORT JettisonTargets)
	install(EXPORT JettisonTargets
		NAMESPACE Jettison::
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Jettison")
	configure_package_config_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/JettisonConfig.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/JettisonConfig.cmake"
		INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Jettison")
	write_basic_package_version_file(
		"${CMAKE_CURRENT_BINARY_DIR}/JettisonConfigVersion.cmake"
		COMPATIBILITY SameMajorVersion)
	install(FILES
		"${CMAKE_CURRENT_BINARY_DIR}/JettisonConfig.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/JettisonConfigVersion.cmake"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Jettison")
endif()
