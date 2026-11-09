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

The presets are `clang-debug`, `clang-release` and `clang-headless` (Debug with GLFW's X11
backend enabled as well, which is the tree the headless render test uses).

## Gates

`scripts/ci.sh` is the gate, and everything runs it: the worktrunk `pre-commit` hook checks
formatting, `pre-merge` runs `./scripts/ci.sh --skip-release --llvmpipe`, and
`.github/workflows/ci.yml` runs `./scripts/ci.sh --llvmpipe` on every push.

```bash
./scripts/format-check.sh      # clang-format >= 20 gate over the tracked sources
./scripts/ci.sh --skip-smoke   # format gate + Debug and Release builds
./scripts/ci.sh --llvmpipe     # everything, on the software Vulkan driver
```

Formatting drift is checked with the project's own `.clang-format`; the tree is clean from
the format commit recorded in `.git-blame-ignore-revs`, so register it once:

```bash
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

### Headless render test

`scripts/smoke-test.sh` runs `test --smoke-test` with no desktop session: it builds the
`clang-headless` preset, starts a virtual X server, and can pin Mesa's lavapipe software
driver so the run does not depend on a GPU. A passing run renders the scene, takes two
window resizes, and shuts down with Vulkan validation enabled.

```bash
./scripts/smoke-test.sh --llvmpipe                  # build, Xvfb, software Vulkan
./scripts/smoke-test.sh --no-build --xvfb           # existing tree, virtual display
./scripts/smoke-test.sh --no-xvfb                   # your own Wayland/X11 session
./scripts/smoke-test.sh --icd <lvp_icd.x86_64.json> # pin a driver explicitly
```

It needs the X11 development libraries for GLFW (the `devenv.nix` shell installs them; on
Debian: `libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxinerama-dev libxi-dev`), an
X server to run on (`Xvfb` from `xorg.xorgserver`, or `xvfb-run` when installed), and a
Vulkan driver. With `--llvmpipe` it selects Mesa's software implementation; without it the
machine's own driver is used. The debug build always requests `VK_LAYER_KHRONOS_validation`,
so validation layers must be available (devenv exports `VK_LAYER_PATH` for them).

CI installs LLVM 21, the Vulkan SDK, Mesa's lavapipe and Xvfb on an Ubuntu runner and calls
the same script, so a local `./scripts/ci.sh --llvmpipe` is a faithful rehearsal of the build.
