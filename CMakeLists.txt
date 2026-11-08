cmake_minimum_required(VERSION 3.24)

project(Jettison VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
