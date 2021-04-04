cmake_minimum_required (VERSION 3.17)

target_sources(Renderer PUBLIC
    # Vulkan implementation.
    DeviceContext.cpp
    DeviceContext.h
    Model.cpp
    Model.h
    Pipeline.cpp
    Pipeline.h
    Renderer.cpp
    Renderer.h
    RenderPass.cpp
    RenderPass.h
    Swapchain.cpp
    Swapchain.h
    Window.cpp
    Window.h
    )

# The ImGui rendering code.
add_subdirectory("imgui")
