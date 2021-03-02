cmake_minimum_required (VERSION 3.18)

# Set the overall project name.
project("tiny_obj_loader" VERSION 1.0.0)

# Force the compiler to c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tiny Object Loader.
add_library(tiny_obj_loader INTERFACE)
target_include_directories(tiny_obj_loader INTERFACE "include")

# Move the targets into a solution folder.
set_property(TARGET tiny_obj_loader PROPERTY FOLDER "tiny_obj_loader")
