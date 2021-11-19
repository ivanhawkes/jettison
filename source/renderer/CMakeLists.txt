cmake_minimum_required (VERSION 3.16)

add_library(Renderer STATIC)
set_property(TARGET Renderer PROPERTY FOLDER "Renderer")

target_link_libraries(Renderer PUBLIC Vulkan::Vulkan glm glfw stb tiny_obj_loader)

target_sources(Renderer PUBLIC
    # Interface.
    IRenderer.h
    )

# The Vulkan rendering code.
add_subdirectory("vulkan")
