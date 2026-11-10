cmake_minimum_required(VERSION 3.24)

# Set the overall project name.
project("test" VERSION 0.1.0)

# Force the compiler to c++17
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test main.cpp)
jettison_use_shared_assets(test)

add_executable(smoke_sequence_test smoke_sequence_test.cpp)
target_link_libraries(smoke_sequence_test PRIVATE Renderer jettison_warnings)

# Move the targets into a solution folder.
set_property(TARGET test PROPERTY FOLDER "Test")

# Find and link Vulkan.
find_package(Vulkan REQUIRED)
target_link_libraries(test PRIVATE Vulkan::Vulkan)

# Configuration and scene graph libraries.
target_link_libraries(test PRIVATE yaml-cpp::yaml-cpp EnTT::EnTT)

target_link_libraries(test PRIVATE Renderer)

# Project warnings.
target_link_libraries(test PRIVATE jettison_warnings)

if(WIN32)
	install(FILES "${glfw_BINARY_DIR}/src/glfw3.dll" DESTINATION "bin")
endif()
