cmake_minimum_required(VERSION 3.24)

# Set the overall project name.
project("flotsam" VERSION 0.1.0)

# Force the compiler to c++17
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(flotsam main.cpp)
jettison_use_shared_assets(flotsam)

# Move the targets into a solution folder.
set_property(TARGET flotsam PROPERTY FOLDER "Flotsam")

# Find and link Vulkan.
find_package(Vulkan REQUIRED)
target_link_libraries(flotsam PRIVATE Vulkan::Vulkan)

# GL math library.
target_link_libraries(flotsam PRIVATE glm)

# GLFW.
target_link_libraries(flotsam PRIVATE glfw)

# STB.
target_link_libraries(flotsam PRIVATE stb)

# Configuration and scene graph libraries.
target_link_libraries(flotsam PRIVATE yaml-cpp::yaml-cpp EnTT::EnTT)

# Shared rendering library.
target_link_libraries(flotsam PRIVATE Renderer)

# Project warnings.
target_link_libraries(flotsam PRIVATE jettison_warnings)
