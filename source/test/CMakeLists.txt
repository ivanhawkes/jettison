cmake_minimum_required (VERSION 3.18)

# Set the overall project name.
project("test" VERSION 0.1.0)

# Force the compiler to c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test main.cpp)

# Move the targets into a solution folder.
set_property(TARGET test PROPERTY FOLDER "Test")

# Find and link Vulkan.
find_package(Vulkan REQUIRED)
target_link_libraries(test PRIVATE Vulkan::Vulkan)

include_directories("external/entt/src")
include_directories("external/imgui")

# Copy shaders, models and the textures.
configure_file("shaders/shader.vert.spv" "shaders/shader.vert.spv" COPYONLY)
configure_file("shaders/shader.frag.spv" "shaders/shader.frag.spv" COPYONLY)
configure_file("textures/texture.jpg" "textures/texture.jpg" COPYONLY)
configure_file("textures/viking_room.png" "textures/viking_room.png" COPYONLY)
configure_file("models/viking_room.wobj" "models/viking_room.wobj" COPYONLY)

# GL math library.
target_link_libraries(test PRIVATE glm)

# GLFW.
target_link_libraries(test PRIVATE glfw)

# STB.
target_link_libraries(test PRIVATE stb)

# Tiny Object Loader.
target_link_libraries(test PRIVATE tiny_obj_loader)
