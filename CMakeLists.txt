cmake_minimum_required(VERSION 3.28)

project(Jettison VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED COMPONENTS glslc)

function(jettison_compile_shaders target)
	set(shader_outputs)
	foreach(shader IN LISTS ARGN)
		set(output "${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv")
		get_filename_component(output_directory "${output}" DIRECTORY)
		add_custom_command(
			OUTPUT "${output}"
			COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
			COMMAND Vulkan::glslc "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" -o "${output}"
			DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}" Vulkan::glslc
			VERBATIM
		)
		list(APPEND shader_outputs "${output}")
	endforeach()
	add_custom_target(${target}_shaders DEPENDS ${shader_outputs})
	add_dependencies(${target} ${target}_shaders)
endfunction()

# Set the overall project name.
# project(Jettison
#     VERSION 0.1.0
#     DESCRIPTION "A simple Vulkan based rendering engine"
#     LANGUAGES CXX
# )

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

# Source.
add_subdirectory("source")
