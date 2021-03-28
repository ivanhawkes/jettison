cmake_minimum_required (VERSION 3.18)

project("renderer" VERSION 0.1.0)

# Force the compiler to c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(Renderer STATIC)
set_property(TARGET Renderer PROPERTY FOLDER "Renderer")

target_link_libraries(Renderer PUBLIC Vulkan::Vulkan glm glfw stb tiny_obj_loader)

target_sources(Renderer PUBLIC
    # Interface.
    IRenderer.h

    # ImGUI's implementation.
    imgui/ImGuiRenderer.cpp
    imgui/ImGuiRenderer.h
    )

target_sources(Renderer PUBLIC
    # ImGUI's implementations.
    imgui/imgui_impl_glfw.cpp
    imgui/imgui_impl_glfw.h
    imgui/imgui_impl_vulkan.cpp
    imgui/imgui_impl_vulkan.h

    # Base files.
    imgui/imconfig.h
    imgui/imgui.cpp
    imgui/imgui.h
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_internal.h
    imgui/imgui_widgets.cpp
    imgui/imstb_rectpack.h
    imgui/imgui_tables.cpp
    imgui/imstb_textedit.h
    imgui/imstb_truetype.h
    )

# Move the targets into a solution folder.
#set_property(TARGET Renderer PROPERTY FOLDER "Renderer/Vulkan")

target_sources(Renderer PUBLIC
    # Vulkan's implementation.
    vulkan/DeviceContext.cpp
    vulkan/DeviceContext.h
    vulkan/Model.cpp
    vulkan/Model.h
    vulkan/Pipeline.cpp
    vulkan/Pipeline.h
    vulkan/Renderer.cpp
    vulkan/Renderer.h
    vulkan/RenderPass.cpp
    vulkan/RenderPass.h
    vulkan/Swapchain.cpp
    vulkan/Swapchain.h
    vulkan/Window.cpp
    vulkan/Window.h
    )

