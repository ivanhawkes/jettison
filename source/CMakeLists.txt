cmake_minimum_required (VERSION 3.18)

# Set the overall project name.
project("Jettison" VERSION 0.1.0.0)

# Turn on using solution folders.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Force the compiler to c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# I like to know which compiler I am using.
message (STATUS "CMAKE_CXX_COMPILER_VERSION = ${CMAKE_CXX_COMPILER_VERSION}")

option(PLUGIN_IMGUI "Enable the imgui plugin" ON)
if (NOT PLUGIN_IMGUI)
    return()
endif()

# Check if the submodules are present, download if needed.
find_package(Git QUIET)
if(GIT_FOUND)
	# Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

# Safety check - just check one of the submodules we know should be there now.
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/external/entt/CMakeLists.txt")
    message(FATAL_ERROR "The submodules were not downloaded! GIT_SUBMODULE was turned off or failed. Please update submodules and try again.")
endif()

message(STATUS "Submodule update completed.")

# External projects.
add_subdirectory("external")

# Sandbox editor.
add_subdirectory("flotsam")

# Runtime.
add_subdirectory("jetsam")

# Mucking about.
add_subdirectory("test")
