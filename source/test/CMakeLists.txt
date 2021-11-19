cmake_minimum_required (VERSION 3.16)

# Set the overall project name.
project("test" VERSION 0.1.0)

# Force the compiler to c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test main.cpp)
#add_executable(test ImGuiMain.cpp)

# Move the targets into a solution folder.
set_property(TARGET test PROPERTY FOLDER "Test")

# Find and link Vulkan.
find_package(Vulkan REQUIRED)
target_link_libraries(test PRIVATE Vulkan::Vulkan)

include_directories("${CMAKE_SOURCE_DIR}/source/external/entt/src")

# External projects.
add_subdirectory("${CMAKE_SOURCE_DIR}/source/external" external)

# Renderer.
add_subdirectory("${CMAKE_SOURCE_DIR}/source/renderer" renderer)

# 
include_directories("${CMAKE_SOURCE_DIR}/source/renderer")

target_link_libraries(test PUBLIC Renderer)

# TODO: HACK: Need the GLFW DLL file. Not sure how to get it to copy over.
install(FILES "${GLFW_BINARY_DIR}/src/glfw3.dll" DESTINATION "bin")

# Assets.
add_subdirectory("assets")
