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
