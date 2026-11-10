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

`scripts/ci.sh` is the gate: the worktrunk `pre-commit` hook checks formatting, `pre-merge`
runs `./scripts/ci.sh --skip-release --werror --llvmpipe`, and `.github/workflows/ci.yml`
runs `./scripts/ci.sh --llvmpipe --werror` inside the locked devenv shell on selected branches
and pull requests.

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
driver so the run does not depend on a GPU. A passing run walks the paths that have hurt this
engine, in six stages: resize, resize, minimize, restore, scene reload, settle. The window
opens at 1920x1080 and lavapipe is fill-rate bound, so smoke mode opens at 640x480 and each
stage runs a handful of frames inside a 120-second watchdog. Minimizing is exercised on X11
only, because on Wayland a minimized surface parks the frame loop in
`DeviceContext::WaitOnWindowResized` until the window is restored, which a headless run cannot
arrange from inside the loop it is parked in. The gate requires one handled resize, not two:
a tiling window manager owns the geometry, and under niri a resize request is answered with
the compositor's tile, which may already be the window's size.

The run is judged on what the validation layer says, not only on exit status: `test` builds
with `VK_KHRONOS_validation` and a `VK_EXT_debug_utils` messenger that names every object,
the script captures the stream, and any validation error fails the run. The loader's own
messages about duplicate `VK_LAYER_MESA_*` manifests are excluded as the machine talking.

```bash
./scripts/smoke-test.sh --llvmpipe                  # build, Xvfb, software Vulkan
./scripts/smoke-test.sh --no-build --xvfb           # existing tree, virtual display
./scripts/smoke-test.sh --no-xvfb                   # your own Wayland/X11 session
./scripts/smoke-test.sh --icd <lvp_icd.x86_64.json> # pin a driver explicitly
```

It needs the X11 development libraries for GLFW (the `devenv.nix` shell installs them; on
Debian: `libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxinerama-dev libxi-dev`), an
X server to run on (`Xvfb` from `xorgserver`), and a
Vulkan driver. With `--llvmpipe` it selects Mesa's software implementation; without it the
machine's own driver is used. The debug build always requests `VK_LAYER_KHRONOS_validation`,
so validation layers must be available (devenv exports `VK_LAYER_PATH` for them).

CI installs Nix and uses the public devenv binary cache on an Ubuntu runner, then runs the same
locked shell and script. The first runner reached the smoke test without running out of disk;
the corrected smoke harness still needs a GitHub run. See `.github/CI-NOTES.md`.

## App shell

`source/renderer` is what runs the engine. `RunRenderApplication` is the entry point
`flotsam`, `jetsam` and `test` call: it reads argv (`AppArguments.cpp`: `--smoke-test`,
`--assets`, `--scene`, `--save-scene`), builds `Application`, and prints the exception a run
threw. Everything that has a lifetime is in the shell.

`Application` is the lifecycle. `Init` builds the subsystems in dependency order: window,
device and swapchain, scene, pipeline, UI, renderer. The frame loop is `RunFrameLoop`, and one frame of it is
`Tick`: events, hot keys, UI, render. `Shutdown` destroys in the reverse order, idempotent and
`noexcept`, so the loop can call it and `~Application` can call it again. Nothing in this tree
has a destructor of its own: every Vulkan object is released by a `Destroy()` call, so that
destructor is the whole safety net, and before this step an exception thrown out of a frame
unwound past every one of them - the logical device, the ImGui context and GLFW were still
live in a process exiting through a diagnostic, which is the run the diagnostics exist to be
read from.

`SmokeSequence` is the render test's six stages as a state machine: `Advance` is called once a
frame with the frame's answer (did it recreate the swapchain), the window's size and the
clock, and its actions run through five callbacks the shell supplies. Nothing in it names
GLFW, Vulkan or the scene graph, which is what lets it be read as the list of things that have
hurt this engine, and lets its counters be the gate (`Passed`, `Summary`).

## Roadmap

The order is set by what endangers the engine, not by what looks good on a README. "Done"
means the gate above covers it, with the commit that did it in the history.

| # | Work | State |
|---|---|---|
| 1 | Repository hygiene: the abandoned submodule stores, Git LFS for binary assets, `.gitattributes` | done |
| 2 | Truthful gates: the format gate, the worktrunk hooks, `scripts/ci.sh`, the headless render test | local gate passes; GitHub run pending |
| 3 | Delete what the build does not need: the ImGui sample program, `RenderPass`, `ImGuiPipeline`, 667 lines | done |
| 4 | Diagnostics: a measured warning set that survives `-Werror`, a curated clang-tidy set, an ASan/UBSan preset | done |
| 5 | GPU lifetime: `VK_EXT_debug_utils` with named objects, scene-reload invalidation, the extent clamp, pool sizing derived from what is allocated | done |
| 6 | App shell: the lifecycle, the staged init, the frame loop and the smoke sequence, split out of `Loop`'s 200 lines | implemented; partial-startup failure coverage incomplete |
| 7 | Maintenance: GLM 0.9.9.8 → 1.0.1 with its configuration moved to the `glm` target, the swapchain-recreate TODOs on the command pool in `DeviceContext.cpp` and `Pipeline.cpp`, the animation phase out of authored scene state | done |
| 8 | What row 7 found: `Renderer::Present()` is an empty stub nothing calls (the `vkQueuePresentKHR` is inline in `Render`), `Scene::Save` writes animation for an entity with no mesh that `Scene::Load` refuses | done |

Step 5 settled the hazards named in the plan, in the order the instrumentation found them:
a scene reload destroyed model buffers a submitted command buffer still referenced
(`VUID-vkDestroyBuffer-buffer-00922`), and the pipeline keyed model resources on the model
address, which a reload reuses. The joint-palette resize turned out to be fenced by the frame
loop's per-image wait, which is now written down where the destroy happens; the frame pool's
unexplained `+100` was counting sets that live in pools of their own.

Step 6 is the shell. `Loop`'s 200 lines are three files: argv in `AppArguments`, the staged
init, the frame loop and the ordered teardown in `Application`, and the six stages in
`SmokeSequence`, driven through callbacks. The behaviour that changed is the one the step was
for: a frame that throws gets the teardown a frame loop that returns gets, because
`~Application` runs the `Destroy()` calls that unwinding used to skip - and a headless run
that fails the gate is a throw, so the sequence's own failure is reported by a process that
released the device it was testing.

Row 7 is the maintenance, and all three items turned out to be about state that was in the
wrong place. GLM 1.0.1 is byte-identical to 0.9.9.8 on the geometry the shaders and buffers
agree on - `sizeof(Vertex)` 108, `sizeof(FrameUBO)` 192, every `offsetof` the same - measured by
compiling one probe against both sources; what moved was its configuration, which sat in
`Model.h` above the GLM includes, where it reached only the translation units that include that
header first. `glm::perspective(radians(45), 1.25, 0.1, 10)` answers `[2][2]` -1.010101 with
`GLM_FORCE_DEPTH_ZERO_TO_ONE` and -1.020202 without it, so an include reorder is a depth range
change; the macros are compile definitions on the `glm` interface target now, and the two inert
ones are gone (GLM 1.0 has no degree mode, and GLM disables aligned gentypes on this compiler,
which is why `sizeof(glm::vec3)` has always been 12 here). The three command-pool TODOs asked
whether the pool is recreated with the swapchain; it is not, because a recreate changes the
swapchain handle and not the device's queue family, and the descriptor set layout is part of the
pipeline rebuild rather than something left out of it - both written where the code asked, with
the one substantive change being that `Pipeline::Destroy()` now clears `m_commandBuffers` after
freeing it, so no recreate reads an image count off a list of freed handles. The scene round trip
had the frame loop writing authored state: after ten seconds of a 0.7854 rad/s spin on an entity
whose authored `rotation.y` is 0.3, the graph held 8.154025 and the saved file carried
`time: 1.25`; the phase lives in `RotationAnimationComponent::angle` now, composed into the world
transform where the rotation is, the world pose unchanged to within 2.2e-5 rad, and the saved file
says 0.3 and no time.

Row 8 is what row 7 turned up, and both halves were a rule with two owners. `Renderer::Present()`
was an empty body holding a question, with the present, its out-of-date check and the frame-slot
move sitting at the end of `Render`; lifting them out is the move, and the part that is not is
that out of date means two things on the two sides of a submit - Render's acquire leaves the frame
slot alone, because that frame queued nothing, and Present moves it, because that frame queued
everything. The scene round trip had the loader and the writer each enforcing a mesh rule the
other did not know about, and the file in the middle lost: a registry the engine saved came back
as "Entity transform animation needs a mesh and must be a mapping", the loader refusing a document
its own writer produced. A rotation animation runs on the transform, so the loader takes it
without a mesh; a clip player runs on a model's skin, so the writer stopped writing one with no
model to name.
