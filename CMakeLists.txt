cmake_minimum_required(VERSION 3.24)

project(Jettison VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
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

# Source.
add_subdirectory("source")
