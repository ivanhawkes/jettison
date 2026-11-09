# Jettison Engine

A toy 2D / 3D game engine project (very experimental).

I'm looking into the possibility of making a basic engine using primarily open source software. The goal is to find the minimum viable product which is able to be used to make simple games. The libraries will be selected based on fitness for purpose.

Criteria will include:

- elegance
- efficiency
- modern c++ usage
- interoperability
- maintenance

All attempts will be made to observe best practice.

## Cloning

```
git clone git@github.com:ivanhawkes/Jettison.git
```

There are no submodules. The external dependencies (EnTT, GLM, GLFW, Dear ImGui, stb,
yaml-cpp, fastgltf) are pinned in `source/external/CMakeLists.txt` and downloaded by CMake's
`FetchContent` during the first configure.

Large media (`.glb`, images, fonts, audio) is tracked with Git LFS, so install the filter
before the first checkout: `git lfs install`. The `devenv` shell does this for you.

## Building

Install Clang, CMake 3.24 or newer, Ninja, Git, Vulkan development libraries, `glslc`,
and the Wayland development libraries. The provided `devenv.nix` supplies these tools
and dependencies. CMake downloads the other dependencies on the first configure.
Dear ImGui and its GLFW and Vulkan backends are built from the same pinned upstream release.

```bash
cmake --preset clang-debug
cmake --build --preset clang-debug
cd build/clang-debug/source/test
./test
```

With a graphical session available, run `./test --smoke-test` to render the sample scene
(`assets/scene/main.yaml`: a static Avocado, a rotating Avocado and a skinned, animated Fox)
through the Vulkan renderer, exercise two window resizes, and shut down with Vulkan validation
enabled in the debug build. Assets are staged beside the executable; use
`--assets <directory>` to select a different asset directory, and `--scene <file>` /
`--save-scene <file>` to load and write scene graphs.

Shaders are compiled automatically. In VS Code, the default build task uses the
same preset, and the `Debug test` launch configuration builds and runs the test
program with GDB using the Native Debug (`webfreak.debug`) extension. Run VS Code
in the development environment so it can find the tools and libraries.
