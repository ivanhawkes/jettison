cmake_minimum_required(VERSION 3.28)

# Set the overall project name.
# Turn on using solution folders.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# I like to know which compiler I am using.
message (STATUS "CMAKE_CXX_COMPILER_VERSION = ${CMAKE_CXX_COMPILER_VERSION}")

option(PLUGIN_IMGUI "Enable the imgui plugin" ON)
if (NOT PLUGIN_IMGUI)
    return()
endif()

add_subdirectory("external")

set(JETTISON_ASSET_BINARY_DIR "${CMAKE_BINARY_DIR}/assets")

function(jettison_use_shared_assets target)
    file(RELATIVE_PATH asset_subdirectory "${CMAKE_CURRENT_BINARY_DIR}" "${JETTISON_ASSET_BINARY_DIR}")
    target_compile_definitions(${target} PRIVATE
        "JETTISON_ASSET_SUBDIRECTORY=\"${asset_subdirectory}\"")
    add_dependencies(${target} jettison_assets)
endfunction()

add_subdirectory("${CMAKE_SOURCE_DIR}/assets" "${JETTISON_ASSET_BINARY_DIR}")

# The engine modules.
add_subdirectory("engine")

# Shared rendering library.
add_subdirectory("renderer")

# Sandbox editor.
add_subdirectory("flotsam")

# Runtime.
add_subdirectory("jetsam")

# Mucking about.
add_subdirectory("test")
