#add_compile_options(${PLASMA_C_CXX_EXTERNAL_FLAGS})

# Can bypass Boost dependancy using SKIP_PERFORMANCE_COMPARISON
add_subdirectory("cereal")

# Configure Cereal to skip the performance tests so we don't need Boost.
option(SKIP_PERFORMANCE_COMPARISON "Skip building performance comparison (requires boost)" ON)

# Move the targets into a solution folder.
set_property(TARGET cereal PROPERTY FOLDER "Cereal")
set_property(TARGET sandbox PROPERTY FOLDER "Cereal/Sandbox")
set_property(TARGET sandbox_json PROPERTY FOLDER "Cereal/Sandbox")
set_property(TARGET sandbox_rtti PROPERTY FOLDER "Cereal/Sandbox")
set_property(TARGET sandbox_vs PROPERTY FOLDER "Cereal/Sandbox")
set_property(TARGET sandbox_vs_dll PROPERTY FOLDER "Cereal/Sandbox")

# Fast ECS library.
add_subdirectory("entt")

# Move the targets into a solution folder.
set_property(TARGET aob PROPERTY FOLDER "EnTT")

# Missing harfbuzz, ZLIB and PNG and BZIP2
add_subdirectory("freetype")

# Move the targets into a solution folder.
set_property(TARGET freetype PROPERTY FOLDER "Freetype")

# Not a cmake thing.
#add_subdirectory("imgui")

# Warning which needs suppressing.
add_subdirectory("zlib")

# Move the targets into a solution folder.
set_property(TARGET example PROPERTY FOLDER "ZLib")
set_property(TARGET zlib PROPERTY FOLDER "ZLib")
set_property(TARGET zlibstatic PROPERTY FOLDER "ZLib")
set_property(TARGET minigzip PROPERTY FOLDER "ZLib")

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

