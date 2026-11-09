cmake_minimum_required(VERSION 3.24)

# Set the overall project name.
project("jetsam" VERSION 0.1.0)

# Force the compiler to c++17
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(jetsam main.cpp)
jettison_use_shared_assets(jetsam)

# Move the targets into a solution folder.
set_property(TARGET jetsam PROPERTY FOLDER "Jetsam")

# Find and link Vulkan.
find_package(Vulkan REQUIRED)
target_link_libraries(jetsam PRIVATE Vulkan::Vulkan)

# GL math library.
target_link_libraries(jetsam PRIVATE glm)

# GLFW.
target_link_libraries(jetsam PRIVATE glfw)

# STB.
target_link_libraries(jetsam PRIVATE stb)

# Configuration and scene graph libraries.
target_link_libraries(jetsam PRIVATE yaml-cpp::yaml-cpp EnTT::EnTT)

# Shared rendering library.
target_link_libraries(jetsam PRIVATE Renderer)
