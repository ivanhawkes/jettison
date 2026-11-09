cmake_minimum_required(VERSION 3.24)

add_library(Renderer STATIC)
set_property(TARGET Renderer PROPERTY FOLDER "Renderer")

target_link_libraries(Renderer PUBLIC
    Vulkan::Vulkan
    glm
    glfw
    stb
    yaml-cpp::yaml-cpp
    EnTT::EnTT
    fastgltf
    "$<LINK_GROUP:RESCAN,imgui>"
)

# Our warnings, privately: an executable that links Renderer compiles its own sources with
# its own flags, so each project target asks for the set itself.
target_link_libraries(Renderer PRIVATE jettison_warnings)

target_sources(Renderer
    PUBLIC
    # Interface.
    RenderApplication.h
    Scene.h
    SceneSystems.h
    PRIVATE
    RenderApplication.cpp
    Scene.cpp
    SceneSystems.cpp
)

target_include_directories(Renderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# The Vulkan rendering code.
add_subdirectory("vulkan")
