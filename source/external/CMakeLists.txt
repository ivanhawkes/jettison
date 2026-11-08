include(FetchContent)
find_package(Vulkan REQUIRED)

set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.6.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(entt)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 0.9.9.8
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(glm)
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE "${glm_SOURCE_DIR}")

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG 01a2bac7d51d99a81277439b64d9faa69cdd12ed
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_demo.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)
target_include_directories(imgui PUBLIC
  "${imgui_SOURCE_DIR}"
  "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 5736b15f7ea0ffb08dd38af21067c314d6a3aae9
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(stb)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${stb_SOURCE_DIR}")

FetchContent_Declare(
  tinyobjloader
  GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
  GIT_TAG v2.0.0rc13
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(tinyobjloader)
add_library(tiny_obj_loader INTERFACE)
target_include_directories(tiny_obj_loader INTERFACE "${tinyobjloader_SOURCE_DIR}")
