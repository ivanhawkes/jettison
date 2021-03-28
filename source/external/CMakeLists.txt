set(JETTISON_EXTERNAL_SOURCE ${CMAKE_CURRENT_LIST_DIR})

# Can bypass Boost dependancy using SKIP_PERFORMANCE_COMPARISON
#add_subdirectory("cereal")

# Configure Cereal to skip the performance tests so we don't need Boost.
#option(SKIP_PERFORMANCE_COMPARISON "Skip building performance comparison (requires boost)" ON)

# Move the targets into a solution folder.
#set_property(TARGET cereal PROPERTY FOLDER "Cereal")
#set_property(TARGET sandbox PROPERTY FOLDER "Cereal/Sandbox")
#set_property(TARGET sandbox_json PROPERTY FOLDER "Cereal/Sandbox")
#set_property(TARGET sandbox_rtti PROPERTY FOLDER "Cereal/Sandbox")
#set_property(TARGET sandbox_vs PROPERTY FOLDER "Cereal/Sandbox")
#set_property(TARGET sandbox_vs_dll PROPERTY FOLDER "Cereal/Sandbox")

# Fast ECS library.
add_subdirectory("entt")

# Move the targets into a solution folder.
set_property(TARGET aob PROPERTY FOLDER "EnTT")

# Missing harfbuzz, ZLIB and PNG and BZIP2
#add_subdirectory("freetype")

# Move the targets into a solution folder.
#set_property(TARGET freetype PROPERTY FOLDER "Freetype")

# Not a cmake thing.
#add_subdirectory("imgui")
#list(APPEND IMGUI_SOURCES
#	${CMAKE_CURRENT_LIST_DIR}/imgui/imconfig.h
#	${CMAKE_CURRENT_LIST_DIR}/imgui/imconfig.cpp
#	)

# Warning which needs suppressing.
# NOTE: I got sick of suppressing it, so zlib is out till I need it.
#add_subdirectory("zlib")

# Move the targets into a solution folder.
#set_property(TARGET example PROPERTY FOLDER "ZLib")
#set_property(TARGET zlib PROPERTY FOLDER "ZLib")
#set_property(TARGET zlibstatic PROPERTY FOLDER "ZLib")
#set_property(TARGET minigzip PROPERTY FOLDER "ZLib")

# GL math library.
add_subdirectory("glm")

# GLFW.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("glfw")

# STB.
add_subdirectory("stb")

# Tiny Object Loader.
add_subdirectory("tiny_obj_loader")


# Testing getting something with fetch...
# Interesting, it pulled it in, but not into the external folder - rather a dep folder.
include(FetchContent)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog
  GIT_TAG        de0dbfa3596a18cd70a4619b6a9766847a941276
)

# After the following call, the CMake targets will be defined and available to the rest of the build.
FetchContent_MakeAvailable(spdlog)
