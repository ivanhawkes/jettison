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
  SYSTEM
)
FetchContent_MakeAvailable(entt)

# GLM 1.0.1, measured against the 0.9.9.8 this tree carried: the geometry ABI is identical -
# sizeof(Vertex) 108, sizeof(FrameUBO) 192, every offsetof the same, and identical translate,
# rotate, angleAxis, lookAt and perspective results - so no buffer layout and no shader moves.
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(glm)
add_library(glm INTERFACE)
target_include_directories(glm SYSTEM INTERFACE "${glm_SOURCE_DIR}")

# GLM's configuration is a compile definition, on the target that carries the headers: these
# macros have to be set before the first GLM include, and a header can only guarantee that for
# files that include it first. Model.h used to carry them, which leaves every other
# translation unit one include order away from a different GLM. Measured: with
# GLM_FORCE_DEPTH_ZERO_TO_ONE glm::perspective(45, 1.25, 0.1, 10) has [2][2] -1.010101 and
# [3][2] -0.101010, the zero-to-one depth range this renderer's viewport is set up for; without
# it the same call gives -1.020202 and -0.202020, the -1..1 range, and the scene renders with
# depth mapped somewhere else entirely.
target_compile_definitions(glm INTERFACE
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  GLM_ENABLE_EXPERIMENTAL
)

# GLM_FORCE_RADIANS is gone: GLM 1.0 has no degree mode, and radians were already the default
# in 0.9.9.8. GLM_FORCE_DEFAULT_ALIGNED_GENTYPES is gone with it, for a different reason - GLM
# disables aligned gentypes on this compiler (GLM_LANG carries no CXXMS flag, which the feature
# requires), so it changed nothing here: sizeof(glm::vec3) is 12 and alignof 4 on both versions,
# and the vertex stride the shaders read has always been the compact 108 bytes.

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Window backends. The desktop build is Wayland-only. GLFW's X11 backend is off by default
# and enabled by the clang-headless preset (JETTISON_GLFW_X11=ON), which is the tree the
# headless smoke tests run under a virtual display (see scripts/smoke-test.sh).
option(JETTISON_GLFW_X11 "Build GLFW with X11 support for headless smoke tests" OFF)
# One backend per tree. The headless presets ask for X11, and building Wayland alongside it
# leaves libwayland loaded, where it leaks 192 bytes at exit that LeakSanitizer reports
# against code we do not own (measured with clang-asan on wayland 1.26.0).
if(JETTISON_GLFW_X11)
	set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
else()
	set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
endif()
set(GLFW_BUILD_X11 ${JETTISON_GLFW_X11} CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE
  SYSTEM
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG c1aaac65e896a45f81f47a4d68f9959e0f86c859 # v1.93.0
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
target_include_directories(imgui SYSTEM PUBLIC
  "${imgui_SOURCE_DIR}"
  "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 4e9b368bfc071e97206ea5c6adba68dd0cbf8897
  SOURCE_SUBDIR _fetchcontent_no_cmake
)
FetchContent_MakeAvailable(stb)
add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.9.0
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(yaml-cpp)

set(FASTGLTF_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  fastgltf
  GIT_REPOSITORY https://github.com/spnda/fastgltf.git
  GIT_TAG v1.0.1
  GIT_SHALLOW TRUE
  SYSTEM
)
FetchContent_MakeAvailable(fastgltf)

