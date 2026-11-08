cmake_minimum_required(VERSION 3.24)

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

# Sandbox editor.
add_subdirectory("flotsam")

# Runtime.
add_subdirectory("jetsam")

# Mucking about.
add_subdirectory("test")
