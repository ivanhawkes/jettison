cmake_minimum_required (VERSION 3.18)

# Set the overall project name.
project("stb" VERSION 1.0.0)

# Force the compiler to c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# STB.
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "include")

# Move the targets into a solution folder.
set_property(TARGET stb PROPERTY FOLDER "stb")
