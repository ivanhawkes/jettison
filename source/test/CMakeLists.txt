cmake_minimum_required (VERSION 3.18)

# Set the overall project name.
project("Test" VERSION 0.1.0)

# Force the compiler to c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test main.cpp)

# Move the cereal targets into a solution folder.
set_property(TARGET test PROPERTY FOLDER "Test")

include_directories("external/entt/src")
include_directories("external/imgui")
