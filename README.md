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
git clone git@github.com:ivanhawkes/jettison.git
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
formatting, `pre-merge` runs `./scripts/ci.sh --skip-release --werror --llvmpipe`, and
`.github/workflows/ci.yml` runs `./scripts/ci.sh --llvmpipe --werror` on every push.

```bash
./scripts/format-check.sh      # clang-format >= 20 gate over the tracked sources
./scripts/ci.sh --skip-smoke   # format gate + Debug and Release builds
./scripts/ci.sh --llvmpipe     # everything, on the software Vulkan driver
./scripts/ci.sh --werror       # the same, with warnings as errors
./scripts/ci.sh --asan         # the same, plus a sanitized render test
./scripts/tidy.sh --summary    # clang-tidy; needs a configured Debug tree for its database
```

### Warnings

The project's four targets link a `jettison_warnings` interface library privately:
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wshorten-64-to-32
-Wimplicit-int-float-conversion -Wnon-virtual-dtor -Wextra-semi
-Winconsistent-missing-destructor-override -Wcast-qual -Wcast-align -Wunreachable-code
-Wdocumentation`. On an interface target rather than in `CMAKE_CXX_FLAGS` because
`CMAKE_CXX_FLAGS` reaches every FetchContent dependency, and their diagnostics are not ours
to acquire. The list is measured on this tree: it is clean, which is what lets `-Werror`
turn on. `JETTISON_WERROR=ON`, or `--werror`, is that switch; a developer's build leaves it
off so an edit can be interrupted. `-Wold-style-cast` was probed and left out: this is a GLM
codebase, and the flag costs more than it tells.

### clang-tidy

`.clang-tidy` carries a curated set, and `scripts/tidy.sh` runs it over the project's own
sources from a configured tree's compile database. A check earns its place by being clean
over these sources or by pointing at something that gets fixed; the three the first run
produced hundreds of findings against, all of them house style (brace-less `if`, explicit
`{}` member init, enum ABI advice on Vulkan-facing enums), are excluded with their counts
in the file so the decision is legible later.

### Sanitizers

`./scripts/smoke-test.sh --preset clang-asan --llvmpipe` builds AddressSanitizer and
UndefinedBehaviorSanitizer into the headless tree and runs the render test under them,
with `-fno-sanitize-recover=all` so the first finding stops the run. That tree builds GLFW
X11-only, which is why it is the sanitized one: a tree that links libwayland reports a
192-byte leak inside libwayland at disconnect, and a suppression for it would hide the
findings the preset exists to produce. GLFW loads the X11 libraries at runtime, so the
shell running the test needs libX11 and friends on `LD_LIBRARY_PATH`; the devenv shell
carries them.

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
