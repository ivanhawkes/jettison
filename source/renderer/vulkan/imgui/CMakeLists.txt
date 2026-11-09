cmake_minimum_required (VERSION 3.17)

# ImGui's own Vulkan backend: ImGui_ImplVulkan_Init compiles its shaders at runtime, so
# none of the hand-written pipeline this target once carried is needed. ImGuiPipeline.cpp
# had already been deleted, leaving a missing source entry and an orphaned header.
target_sources(Renderer PUBLIC
    ImGuiRenderer.cpp
    ImGuiRenderer.h
)

