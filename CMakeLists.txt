cmake_minimum_required(VERSION 3.28)

project(Jettison VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
# that use it and no others.
function(jettison_compile_shaders target)
	set(shader_outputs)
	foreach(shader IN LISTS ARGN)
		set(output "${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv")
		get_filename_component(output_directory "${output}" DIRECTORY)
		add_custom_command(
			OUTPUT "${output}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
			COMMAND Vulkan::glslc --target-env=vulkan1.3
				-I "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include"
				-MD -MF "${output}.d"
				"${CMAKE_CURRENT_SOURCE_DIR}/${shader}" -o "${output}"
			DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" Vulkan::glslc
			DEPFILE "${output}.d"
			VERBATIM
		)
		list(APPEND shader_outputs "${output}")
	endforeach()
	add_custom_target(${target}_shaders DEPENDS ${shader_outputs})
	add_dependencies(${target} ${target}_shaders)
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
