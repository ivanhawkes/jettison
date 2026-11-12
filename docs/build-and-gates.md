# Build and gates

How to build the engine, what the gate checks, and the facts behind each check. This
consolidates the build sections of the old README and `.github/CI-NOTES.md`.

## Dependencies

There are no submodules. Every third-party library is pinned in
`source/external/CMakeLists.txt` and downloaded by `FetchContent` on the first configure. The
comment on each pin says why it is configured the way it is, and why when it is older than
upstream's newest release. Versions were last reviewed on 2026-11-10.

| Library | Pin | Used from | Note |
|---|---|---|---|
| EnTT | v4.0.0 | now | Requires C++20 and CMake 3.28 |
| GLM | 1.0.3 | now | Geometry ABI asserted by the dependency probe |
| GLFW | 3.5.1 | now | Upstream skipped 3.5.0 (bad tag) |
| Dear ImGui | v1.93.0 (commit) | now | GLFW and Vulkan backends from the same release; fonts through `imgui_freetype` (8b) |
| stb | master, 2026-11-10 (commit) | now | No releases |
| yaml-cpp | 0.9.0 | now | |
| fastgltf | v1.0.1 | now | Downloads its own simdjson 5.1.0 and simdutf |
| spdlog | v1.17.0 | now | `SPDLOG_USE_STD_FORMAT`; core links it |
| doctest | v2.5.3 | now | Fetched in every tree, planned set or not |
| Tracy | v0.13.1 | now | **Held back** from 0.15.0 to match devenv's profiler; profile links it |
| Vulkan Memory Allocator | v3.5.0 | now | Header-only; render compiles the implementation, private to it |
| FastNoiseLite | v1.1.1 | now | Header-only; math links it, output pinned by recorded values |
| MikkTSpace | 2020 (only commit) | now | math links it |
| Jolt Physics | v5.6.0 | now | Double precision, deterministic across build types; physics links it privately |
| FreeType | 2.15.0 | now | Answers RmlUi's `find_package(Freetype)`; RmlUi and ImGui both link it; ui links it, since 8b |
| RmlUi | 6.3 | now | Private to ui, since 8b |
| miniaudio | 0.11.26 | now | Private to audio, since 8c; Vorbis and Opus off, backends loaded at run time |
| KTX-Software | v4.4.2 | now | **Held back** from v5.0.0-rc2, still a release candidate on 2026-11-11; render links `ktx_read` |
| WAMR | 2.4.5 | phase 11 | Candidate runtime; the choice is still open |
| zstd | none | phase 11 | **Held back**: KTX-Software already compiles zstd in |

The Vulkan headers, loader, validation layers and `glslc` come from the devenv shell (Vulkan
headers 1.4.363 at the current lock), not from `FetchContent`.

### Planned dependencies and the probe

The libraries marked for a later phase are fetched now (`JETTISON_PLANNED_DEPENDENCIES=ON`, the
default) so the whole set is proven to link before any phase depends on it. They are
`EXCLUDE_FROM_ALL`: nothing in the engine links them yet, so a tree builds them only for
`dependency_probe`. That one executable links the `Renderer` library and every third-party
library, makes at least one real call into each (it runs a WebAssembly function, creates a
Jolt shape and a KTX texture, generates tangents), and asserts the GLM geometry ABI. The gate
runs it after the Debug build.

`clang-headless`, and therefore `clang-asan`, sets `JETTISON_PLANNED_DEPENDENCIES=OFF`: those
trees exist for the render test, which uses none of the planned libraries. KTX-Software left the
planned set when phase 5a began to use it, so those trees fetch it too, and build `ktx_read` (not
`ktx`): measured on 2026-11-11, 63 objects and about 75 CPU-seconds of a 1,700 CPU-second headless
Debug build, and a 217 MiB clone on the first configure. Jolt left it when phase 7a did, and those trees build it
too, for the physics suite they run under the sanitisers. FreeType and RmlUi left it when phase 8b
did: ui links RmlUi, and ImGui rasterises with FreeType, so every tree builds both, and the headless
trees run the UI's suite and draw its screens in the render test. miniaudio left it when phase 8c
did: audio links it, so every tree builds it, the headless trees run the audio suite with no device,
and the render test plays to miniaudio's null backend, touching no sound system.

`JETTISON_PROFILE=ON` compiles the Tracy client in (`TRACY_ENABLE`). It is on demand and
listens on localhost only, so it costs nothing until a profiler connects. It is off by default,
and off, Tracy's macros and `TracyClient.cpp` compile to nothing. Tracy is fetched in every tree,
because `profile` links it.
The profiler that connects has to be the same version as the client: nixpkgs' `tracy` at the
current devenv lock is 0.13.1, which is why the client is pinned there. It is not in
`devenv.nix`'s package list; add it there, or run `nix run nixpkgs#tracy` from the shell.

Measured on 2026-11-10 (24 cores, Release, from an empty tree), the planned set adds about 45
seconds of configure, mostly downloads, about 410 CPU-seconds of compilation (217 without it,
627 with), and about 0.7 GB of disk per tree. On the CI runner's two build jobs that is
roughly three and a half minutes per tree. CI builds two trees with the set: Debug and Release.

### Integration rules the set follows

Rules that keep every library linkable into one executable, each one learned from a library in
this set:

- **Static only.** `BUILD_SHARED_LIBS` is OFF for the dependency directory. RmlUi declares it as
  an option defaulting to ON, which would make every later dependency shared.
- **One copy of each library.** Before adding a dependency, check whether it compiles its own
  copy of another (`grep` its build for vendored `.c` files). libktx compiles zstd 1.5.5 with
  public symbols, which is why there is no standalone zstd.
- **Options in a `block()`.** Jolt's options are called `DOUBLE_PRECISION`, `ENABLE_INSTALL`
  and `INTERPROCEDURAL_OPTIMIZATION`. A cache entry would configure every other dependency
  reading the same name, so options are plain variables inside a `block()` around the
  dependency they are for. `CMAKE_POLICY_DEFAULT_CMP0077` is NEW so that a dependency declaring
  an old CMake honours them (Tracy 0.13.1 silently discarded them before).
- **The project's language features.** No dependency may build without RTTI or exceptions when
  the engine derives from its classes (Jolt defaults to both off).
- **No system libraries found by accident.** Optional codecs (FreeType's zlib, PNG and
  HarfBuzz; miniaudio's libvorbis and libopus) are off, because each is found on a developer's
  machine and missing on a runner. `OVERRIDE_FIND_PACKAGE` answers a dependency's
  `find_package` for a library this set provides.
- **No executable stack.** An assembly file without `.note.GNU-stack` marks the whole process
  as needing an executable stack. WAMR's trampoline is assembled with `--noexecstack`.
  `readelf -lW dependency_probe | grep GNU_STACK` should say `RW`.
- **A dependency's warnings are not the gate.** Third-party includes are `SYSTEM`, and a
  dependency's own `-Werror` is turned off (astc-encoder's breaks on glibc 2.44's
  `_FORTIFY_SOURCE` warning in Debug).
- **Instruction sets travel.** Jolt adds `-mavx2 -mbmi -mpopcnt -mlzcnt -mf16c` as public
  options. `physics` links Jolt privately (7a), so they reach its own translation units and
  nothing above it, and the engine's CPU baseline is x86-64 with AVX2, which is the decided minimum
  ([roadmap](roadmap.md#decided)). A dependency that would raise it further (AVX-512, for
  example) is a new decision, not a default.

Large media (`.glb`, images, fonts, audio) is in Git LFS: run `git lfs install` before the first
checkout. The devenv shell does this for you. A checkout without LFS gives the render test
132-byte pointer files.

## Building

You need Clang, CMake 3.28 or newer, Ninja, Git, the Vulkan development libraries, `glslc`, and
the Wayland development libraries. `devenv.nix` supplies all of them.

```bash
cmake --preset clang-debug
cmake --build --preset clang-debug

# Run from the root folder.
build/clang-debug/source/test/render_test
```

| Preset | Tree |
|---|---|
| `clang-debug` | Debug, Wayland |
| `clang-release` | Release, Wayland |
| `clang-headless` | Debug with GLFW's X11 backend instead of Wayland: the tree the headless render test uses |
| `clang-asan` | `clang-headless` with AddressSanitizer and UndefinedBehaviorSanitizer |

Shaders compile as part of the build (`jettison_compile_shaders`). Assets are staged beside the
executable. `--assets <directory>` selects a different asset directory, and
`--scene <file>` / `--save-scene <file>` load and write scene graphs, and `--system <virtual path>`
(`systems/halcyon.yaml`) runs the scene a star system builds alone, in place of a scene file.
`--gpu <name>` draws with the GPU whose name has that text in it (`5060`, `llvmpipe`), in place of
the one the renderer chooses, the one with the monitor on it; the log lists the devices it saw.
`--user-dir <directory>` puts the user's files under one directory in place of the XDG ones:
settings under `<directory>/config`, saves under `<directory>/data/saves`. The headless run with no
`--user-dir` makes a directory of its own under the system's temporary directory and removes it as
it ends, so the smoke test never reads or writes the settings and saves of whoever runs it.

In VS Code, the default build task uses `clang-debug`. The `Debug test` launch configuration
builds and runs `render_test` under GDB through the Native Debug (`webfreak.debug`) extension.
Run VS Code inside the devenv shell so it finds the tools.

## Consuming the engine

The game repository builds the engine from source with `FetchContent`, pinned to a commit, and
links `Jettison::Engine` (every module) or the `Jettison::<Module>` targets it needs:

```cmake
FetchContent_Declare(jettison GIT_REPOSITORY <url> GIT_TAG <commit>)
set(JETTISON_BUILD_TESTS OFF)
set(JETTISON_PLANNED_DEPENDENCIES OFF)
FetchContent_MakeAvailable(jettison)
target_link_libraries(game PRIVATE Jettison::Engine)
```

`source/test/consumer` is that build at its smallest: a project that fetches the engine from a
directory and calls into two modules through the public include paths. It is phase 1's exit test
for packaging, and it is not in the gate, because it fetches and compiles the dependency set a
second time. Run it when the module layout or the public interface changes:

```bash
cmake -S source/test/consumer -B build/consumer -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DJETTISON_SOURCE_DIR="$PWD"
cmake --build build/consumer && build/consumer/consumer
```

`JETTISON_INSTALL=ON` adds the install rules and the `Jettison` package (`JettisonConfig.cmake`,
`JettisonTargets.cmake`) for a `find_package(Jettison)` consumer. It is off by default and not
yet a supported path: CMake refuses to export a target whose link interface names a target
outside the export set, and the modules link FetchContent dependencies (GLM, EnTT, GLFW, ImGui,
yaml-cpp, fastgltf) that are not all installed alongside. Making that work means installing each
public dependency's export, or vendoring import targets for them in `JettisonConfig.cmake.in`.
It is done when the game needs a prebuilt engine rather than a source build, and not before.

## The gate

`scripts/ci.sh` is the gate. The worktrunk `pre-commit` hook checks formatting, `pre-merge` runs
`./scripts/ci.sh --skip-release --werror --llvmpipe`, and `.github/workflows/ci.yml` runs
`./scripts/ci.sh --llvmpipe --werror` inside the locked devenv shell.

Order: format gate → Debug build → unit tests and the dependency probe (CTest) → clang-tidy →
Release build and its unit tests → headless render test → reference frames, and with `--asan`, the render test and the
`sim`, `spatial`, `physics`, `ai` and `dock` suites under the sanitisers. The simulation's state hashes are pinned in those
suites, so running them in every tree is what holds Release and the sanitised build to the Debug
build's results. The sample's traders' hash is pinned in the app suite, which runs in Debug and
Release but not under the sanitisers: its twenty minutes of seven traders is the gate's longest
simulation, and every module it steps is in the sanitised suites already.

Each stage is timed, and the run ends with a summary, one line per stage: `ok` with its seconds,
`skip`, or `FAIL`, then `gate passed` or `gate FAILED`. `--log <file>` sends the build's own output
to the file alone and prints only the stages and the summary, and, for a failed stage, its error
lines (a compiler's `error:`, ninja's `FAILED:`, a test's assertion, a `[vulkan:error]`) and its last
lines without ninja's progress. That is the form to read the gate in when only the verdict matters,
as an agent does: a passing gate with `--werror --asan` is 20 lines, against 658 in its log with
warm caches (2026-11-12).

```bash
./scripts/ci.sh --werror --asan --log build/gate.log
```

### Unit tests

Each engine module has a doctest executable, `jettison_<module>_tests`, built from the files the
module's `jettison_add_module(... TESTS ...)` names and registered with CTest under the module's
name. The smoke sequence's own test and the dependency probe are registered alongside them. Saves written
by earlier versions of the engine are fixtures beside the suite that loads them
(`source/engine/sim/tests/data`): each was written once, by the build of its version, and is never
regenerated, since a fixture rewritten by the current build proves only that the current build reads
itself. A format change that breaks one needs a migration, not a new fixture. The
gate runs them with `ctest --test-dir build/<preset> --output-on-failure --no-tests=error`, so a
tree that registered nothing fails rather than passes.

`JETTISON_BUILD_TESTS` turns the suites on. It defaults to on when Jettison is the top-level
project and off when it is a dependency, so the game repository's build does not compile the
engine's tests.

The render smoke harness was called `test` until CTest arrived. CMake reserves that target name
once testing is enabled, and Ninja generates a phony `source/test/test` for that directory's tests,
which an executable of the same name collides with, so it is `render_test` now.

```bash
./scripts/format-check.sh      # clang-format >= 20 gate over the tracked sources
./scripts/ci.sh --skip-smoke   # format gate + Debug and Release builds
./scripts/ci.sh --llvmpipe     # everything, on the software Vulkan driver
./scripts/ci.sh --werror       # the same, with warnings as errors
./scripts/ci.sh --asan         # the same, plus a sanitized render test
./scripts/ci.sh --skip-tidy    # for a machine without a configured Debug tree
./scripts/tidy.sh --summary    # clang-tidy; needs a configured Debug tree for its database
```

### Warnings

Each project target links the `jettison_warnings` interface library privately:
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wshorten-64-to-32
-Wimplicit-int-float-conversion -Wnon-virtual-dtor -Wextra-semi
-Winconsistent-missing-destructor-override -Wcast-qual -Wcast-align -Wunreachable-code
-Wdocumentation`.

The flags sit on an interface target rather than in `CMAKE_CXX_FLAGS`, because
`CMAKE_CXX_FLAGS` reaches every FetchContent dependency, and their diagnostics are not ours to
fix. The list was measured on this tree and is clean, which is what lets `-Werror` turn on.
`JETTISON_WERROR=ON` (`--werror`) is that switch. A developer's build leaves it off so an edit
can be interrupted. `-Wold-style-cast` was tried and left out: this is a GLM codebase, and the
flag costs more than it finds.

### clang-tidy

`.clang-tidy` carries a curated set, and `scripts/tidy.sh` runs it over the project's own sources
from a configured tree's compile database.

A check of every unit is about 1,500 s of work: 2 s for a small one, 23 s for `Application.cpp`,
spread over every check rather than in one. So `scripts/tidy.py`, which `tidy.sh` runs, checks on
every core (`JETTISON_TIDY_JOBS`, which CI sets to 2) and skips a unit that came out clean before and
has not changed since. A unit's key is a digest of clang-tidy's version, `.clang-tidy`, the unit's
compile command and the contents of every file it includes, as the built tree's dependency record
(`ninja -t deps`) lists them, with paths under the repository made relative, so a worktree reuses
another's results; the cache is outside the tree (`~/.cache/jettison/tidy`, or
`JETTISON_TIDY_CACHE`). Only clean units are cached, so a diagnostic is reported every run until it
is fixed. A unit with no dependency record, or with a file changed since its object was built, is
always checked and never cached: the record lists what the unit included when it was built, and an
edit since may have included something it does not name. Measured on 2026-11-12 on
a Ryzen 9 3900X: every unit on all cores in 93 s, against 627 s at the two jobs it ran with before (a Release build overlapping some of it);
with nothing changed, 0.7 s; with one source changed, that unit alone. `--all` checks every unit
whatever the cache says, and `--fix` goes through `run-clang-tidy` over every unit, so two units'
fixes to one header are applied once.

### Compiler cache

Every compile goes through ccache when it is on the PATH, which the devenv shell puts it on
(`JETTISON_CCACHE=OFF` turns it off, and a `CMAKE_CXX_COMPILER_LAUNCHER` given wins). Worktrunk
copies no build tree into a new worktree, so each one builds four; through the cache a tree built
before rebuilds in its link time, 17 s for a clean Release tree against 75 s compiling. The shell
sets the cache's base directory to the checkout's parent, where the worktrees are, so paths in a
command are hashed relative to it and a worktree finds what another compiled. A tree built with
debug information hashes its own build directory too (ccache's `hash_dir`), which keeps the source
paths recorded in each object true to the tree it is in: those trees share within a worktree, as a
branch switched to and back does, and Release shares across them. The cache is 10 GB, where four
trees' objects are 1.2 GB before compression. A check earns its place by being clean over these
sources, or by pointing at something that gets fixed. The three checks the first run reported
hundreds of findings against were all house style, and are excluded with their counts in the
file: brace-less `if`, explicit `{}` member init, and enum-size advice on Vulkan-facing enums.

### Formatting

Formatting drift is checked against the project's `.clang-format`. The tree has been clean since
the format commit recorded in `.git-blame-ignore-revs`, so register that file once:

```bash
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

Claude Code formats a C++ file as soon as it writes or edits one, through a hook in
`.claude/settings.json` (`.claude/hooks/format-edited.sh`), so its work reaches the format gate
already formatted. The hook formats only the project's sources, not the build trees, and does
nothing outside the devenv shell, where there may be no clang-format new enough to read
`.clang-format`.

### Sanitizers

The gate's sanitised smoke test runs on lavapipe whatever else it is asked: NVIDIA's driver (615.71)
fails `vkCreateInstance` under ASan, and LeakSanitizer then reports what it allocated, which is a
driver's and not this project's to fix.
`./scripts/smoke-test.sh --preset clang-asan --llvmpipe` builds ASan and UBSan into the headless
tree and runs the render test under them, with `-fno-sanitize-recover=all` so the first finding
stops the run.

That tree builds GLFW X11-only. With both backends compiled in, a run that uses X11 still loads
libwayland, and LeakSanitizer reports 192 bytes in two allocations inside libwayland-client
1.26.0 at disconnect. A suppression for that leak would hide the findings the preset exists to
produce.

GLFW dlopens the X11 libraries, so the shell running the test needs libX11, libxcb, libXau,
libXdmcp, libXext, libXrandr, libXcursor, libXinerama, libXi, libxkbcommon and libpthreadstubs on
`LD_LIBRARY_PATH`. `devenv.nix` builds that list in `enterShell` with `lib.makeLibraryPath`. A
shell that predates the list fails with `X11: Failed to load Xlib`, and `smoke-test.sh` says so,
naming the library and the variable.

## Headless render test

`scripts/smoke-test.sh` runs `render_test --smoke-test` with no desktop session. It builds the
`clang-headless` preset, starts a virtual X server, and can pin Mesa's lavapipe software driver so
the run does not depend on a GPU.

```bash
./scripts/smoke-test.sh --llvmpipe                  # build, Xvfb, software Vulkan
./scripts/smoke-test.sh --no-build --xvfb           # existing tree, virtual display
./scripts/smoke-test.sh --no-xvfb                   # your own Wayland/X11 session
./scripts/smoke-test.sh --icd <lvp_icd.x86_64.json> # pin a driver explicitly
```

**What it walks.** Twelve stages, the paths that have hurt this engine: resize, resize, minimize,
restore, scene reload, pilot from behind, pilot from the seat, pick, save and load, trade, jump,
settle. Each stage runs four frames, the pick, the trade and the jump as many as they take, inside a
120-second watchdog. It walks
them twice, once for the sample scene (glTF models, a skinned clip, a point light, the debug
lines) and once for `assets/scene/orbit.yaml` (a station over a planet at real scale, placed by
the star system, under an orbit camera and the sun, which shadows it through four cascades, Ostra
Highport drawn from its layout: its 104 floodlights about its pads assigned to the clusters, and its
pads' paint, poles and lamps, hundreds of one shape and colour each, drawn as instanced draws). In the orbit scene the piloting stages fly the shell's ship, held off
Highport's dock in the station's frame, ahead under scripted controls: its proxy made by the traffic
layer, the flight commands, and the pilot camera's chase and cockpit rigs on it, each eased into; the
sample scene has no station, so they fly nothing there, and the summary's `pilots=` says how many
did. Behind the ship, the first piloting stage sends four shuttles to Highport (the console's
`traffic`), flown by steering over the spatial index and drawn by the traffic layer from then on.
The save stage writes the game to the run's user mount, flushed to the disk, and loads it
back over itself with the scene drawing it; the run fails unless the world loaded has the state hash
of the world saved (20,980 bytes for the sample scene's world on 2026-11-12, and 27,296 for the
orbit scene's, physics' state, the shell's ship and the four shuttles under way among it). The game UI's two screens, the market
and the saved games, are shown from the first frame, so the overlay is drawn in every stage, over
every resize and the reload; the save stage lists its save on one. The trade stage reads the market
screen of the station the ship is at: its station and its first good's stock have to be the ones the
snapshot has, and its first Buy button, pressed, sends a trade the next step answers, whose answer
the screen then has to show, and to play on the UI bus. In the orbit scene the ship is off the dock,
so the answer is a refusal (the trade itself is the app suite's); the sample scene has no market, and the summary's `markets=`
and `trades=` say what was tried. The sample scene has no sun,
so its shadow pass clears the cascades and draws nothing, under the same plan and barriers. Each scene's sky is baked into its
environment when it loads and again when the reload stage replaces it, so the bake's passes and
the frame sets rewritten after it run under validation too. The reload also reads every resource
again, models and rigs included, so a model's buffers are retired while a frame in flight may read
them and freed after it, under validation. The jump replaces the scene with another system's
behind its fade while frames that drew the old one are in flight: the sample scene jumps to the
galaxy's first system (the orbit scene), and the orbit scene through Halcyon's gate to Vesper, built
from its system file alone; each is about 1.7 s on lavapipe. The whole run took 4 seconds on a
workstation on 2026-11-11, from about 40 before phase 5e gave lavapipe RGBA8 textures and culled
the planet out of the shadow cascades; the script, its start-up checks and the two jumps included,
took 15 seconds after phase 6d. Budget more on a runner.

- **Window size.** The window opens at 1920×1080 normally, but at 640×480 in smoke mode,
  because lavapipe is fill-rate bound.
- **Minimize.** Minimizing is exercised on X11 only. On Wayland a minimized surface parks the
  frame loop in `DeviceContext::WaitOnWindowResized` until the window is restored, which a
  headless run cannot arrange from inside the loop it is parked in.
- **Resize.** The gate requires one handled resize, not two. A tiling window manager owns the
  geometry: under niri, a resize request is answered with the compositor's tile, which may
  already be the window's size, and the stage gives up after eight frames rather than wait for
  the watchdog. Xvfb has no window manager, so on a runner both requests land.

**What it asserts.** The run is judged on what the validation layer says, not only on exit
status. The debug build loads `VK_LAYER_KHRONOS_validation`, and `DeviceContext` installs a
`VK_EXT_debug_utils` messenger that names every object. The script captures the stream, and any
validation error fails the run. Debug builds also turn on the layer's synchronization validation
(through `VK_EXT_layer_settings`, when the layer offers it), which is what checks the barriers the
pass list derives: with them weakened, a run reports `SYNC-HAZARD-WRITE-AFTER-READ` and fails. A
VMA allocation still live at teardown is reported on the same stream as `[vulkan:error]`, naming
it. The loader's messages about duplicate `VK_LAYER_MESA_*` manifests
are excluded as the machine talking: on NixOS that is the shell's Mesa against
`/run/opengl-driver`. So is its note that Mesa's D3D12 driver (`libvulkan_dzn.so`), which exists for
WSL, declined to start, which it does whenever every driver is loaded rather than one pinned. Device
selection asks a device about the window's surface only once the device says it can present to it:
an NVIDIA card under Xvfb cannot, and asking it anyway was reported as
`VUID-vkGetPhysicalDeviceSurfaceCapabilitiesKHR-surface-06211` before lavapipe was picked over it,
which failed every run that did not pin lavapipe (`--llvmpipe`) on a machine with an NVIDIA card. Debug-utils entry points are resolved through `vkGetInstanceProcAddr`, so a
loader without them costs object names, not a link failure.

Three verdicts are the run's own, read after teardown and reported through its exit status: the
sequence has to have reached its last stage, the cluster check has to agree, and the game UI has to
have logged no warning or error (RmlUi's own, a property it does not know, a variable a document
names that no model has, through the log's `ui` category). The check reads
back every frame's light assignment and compares it with the CPU reference
(`render/Clusters.h`) cluster by cluster; a frame that disagrees, an index list that overflowed,
or a run in which no cluster listed a light fails the run, and the run prints how many frames it
compared: 21 to 24 a scene on lavapipe on 2026-11-11, every one with lights and none disagreeing,
the count depending on how many frames the resize stages take. It also prints the most of the
index list any frame used and the most lights any cluster listed, what the list's budget is
measured against. Until phase 5c the first verdict was unreachable: teardown released the
sequence before `Run` read it, so a run that stopped short of its last stage still passed.

The run then prints the GPU's mean time in each declared pass. It is a record, not a verdict: the
frames include the resizes and the reload, under the validation layer, and a runner's times vary
too much to fail on. The frame's budget is measured with the [benchmark](#benchmark).

Lavapipe is a CPU device, so it is given RGBA8 textures rather than BC7, which it would decode in
software at every fetch (phase 5e measured the orbit scene's opaque pass at 469 ms in BC7 and 23 ms
in RGBA8). The run therefore uploads RGBA8; transcoding to BC7 is covered by the render suite,
without a device, and uploading it only on hardware.

**What it needs.** It needs three things:

- The X11 development libraries for GLFW. On Debian:
  `libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxinerama-dev libxi-dev`.
- `Xvfb` (from `xorgserver`).
- A Vulkan driver and the validation layers. devenv exports `VK_LAYER_PATH`.

## Reference frames

`scripts/capture.sh` writes frames of `render_test` to PNG files from inside its frame loop
(`--screenshot`), on lavapipe under Xvfb, with the clocks moved a sixtieth of a second a frame and
held while anything loads (`--frame-step`), so a view captured twice on one machine is the same to
the byte. It replaces grabbing the virtual screen with `xwd` after a fixed wait: the frame is the
swapchain's, exact to the frame, and a console line given with `--run` places the camera where
`xdotool` keys held for seconds used to.

```bash
./scripts/capture.sh out.png --scene $PWD/assets/scene/orbit.yaml --run "orbit 0.8 -0.1 2.0e7 Ostra"
./scripts/capture.sh --references            # compare every view with its reference
./scripts/capture.sh --references --update   # record a change that is meant to change a frame
./scripts/capture.sh --references corva      # one view
```

The gate's reference-frames stage runs the views `source/test/frames/views.txt` lists, a name and
`render_test`'s arguments a line, all at once on one display, and fails if more than one pixel in
a thousand differs from its reference by more than 2% (another machine's lavapipe rounds a little
differently), or if the validation layer reported anything. The frames and their differences are
kept in `build/<preset>/frames`. The five views of phase 10a (the orbit scene as it opens, Ostra by
day and by night, Corva and its rings, and the Vesper scene as it opens) took 23 seconds together
on 2026-11-13, most of it each run transcoding the planet's maps on one thread; captured twice, no
pixel differed. Phase 10b added a sixth, the transparent pass's test bench (`scene/effects.yaml`),
captured three seconds in so its smoke has risen: a view's line may give its own `--after-frames`,
after the script's. The six took 24 seconds, and captured twice, no pixel differed. Phase 10c added
a seventh, the post chain's test bench (`scene/post.yaml`) two seconds in, its beacon bright, with
`--ambient-occlusion`, which a CPU device is otherwise without; the seven took 29 seconds, and
captured twice, no pixel differed. A frame is
960×540, and the frame captured is the one after thirty in a row with nothing loading, unless its
line says otherwise.

Run one capture at a time on a display: the script stops the Xvfb it started when it ends, under
any other capture still drawing on it.

## Benchmark

`render_test --benchmark <frames>` measures the frame: it draws 60 frames to warm up (the
pipelines a material builds on its first draw, the textures' uploads), then the frames asked for,
and prints the GPU's mean time in each declared pass and the frame's, the wall time per frame, and
the last frame's counts (meshes, what culling left out, the instanced draws and their triangles,
each cascade's casters, the lights, the outlines), then closes. `--window <width>x<height>` asks for
a size, which a tiling compositor may overrule (niri gives the window its tile; the report says what
it got); `--depth-prepass` turns the prepass on; `--ambient-occlusion` and `--no-ambient-occlusion`
choose it in place of the device's default (on, except on a CPU device), and `--bloom <strength>`
sets bloom's, 0 for none.

```bash
# Hardware: a Release tree, on the desktop session. NVIDIA's driver cannot present to Xvfb.
build/clang-release/source/test/render_test \
    --scene build/clang-release/assets/scene/orbit.yaml --benchmark 600
# Lavapipe: the headless tree on Xvfb, at the smoke test's size.
VK_DRIVER_FILES=<lvp_icd.x86_64.json> DISPLAY=:<n> build/clang-headless/source/test/render_test \
    --scene build/clang-headless/assets/scene/orbit.yaml --benchmark 60 --window 640x480
# A station with 265 more lights, written beside the orbit scene in a build tree.
python3 scripts/make-light-budget-scene.py build/clang-release/assets/scene/orbit.yaml
```

The budgets and what phase 5 measured are in [architecture](architecture.md#the-budget).

The spatial index has a benchmark of its own, a doctest case skipped in the gate: a system's traffic
of 1,000 to 100,000 ships stepped 60 times, every ship moved in the index each step, and the time
of each kind of query AI makes, for the octree's leaf sizes and looseness and for a walk over every
position. What phase 6c measured with it is under [decided](roadmap.md#decided).

```bash
build/clang-release/source/engine/spatial/jettison_spatial_tests --no-skip -tc='Spatial benchmark*'
```

For the GPU's passes on Tracy's timeline beside the CPU's zones, configure a tree with
`-DJETTISON_PROFILE=ON` (for example `cmake --preset clang-release -B build/profile
-DJETTISON_PROFILE=ON`). Every other tree compiles the zones out. A capture without the profiler's
window: `tracy-capture -o run.tracy -s 3` started before the run, then `tracy-csvexport -g run.tracy`
lists the GPU zones, one per declared pass a frame (`nix shell nixpkgs#tracy`, 0.13.1, the version
the client is pinned to).

## CI

The workflow follows the [devenv GitHub Actions guide](https://devenv.sh/integrations/github-actions/).
It installs Nix on Ubuntu 24.04, enables the public `devenv` Cachix cache, installs devenv 2.4.0,
and runs the gate in the shell described by `devenv.nix` and `devenv.lock`. It does not duplicate
the toolchain with apt packages.

**Status.** The gate passes locally in the locked devenv shell. The first GitHub run of the
current workflow completed the builds, then failed when the smoke harness selected Ubuntu's
`xvfb-run` wrapper. The harness now starts the shell's Xvfb directly, and that fix still needs
a GitHub run.

**Runner facts.**

- An early attempt ran out of runner disk building LLVM from source. A multi-user Nix daemon can
  silently ignore substituters from an untrusted user
  ([devenv caching](https://devenv.sh/binary-caching/)), so the Nix action trusts the runner user
  and the Cachix action names the cache explicitly. No token is needed to read it.
- CMake, clang-tidy, CTest and Nix parallelism are bounded to 2 to limit memory pressure. An uncached
  derivation can still mean a long build.
- An earlier run failed compiling ImGui's GLFW backend because `GL/gl.h` was missing. The ImGui
  target defines `GLFW_INCLUDE_NONE`, because it uses Vulkan and does not need OpenGL headers.

**Defects the gate found on CI.**

- `assets/CMakeLists.txt` copied `assets/textures`, which no fresh clone has: Git cannot record an
  empty directory. It now copies the directories that exist and creates the empty one, and there
  is `assets/textures/.gitkeep`.
- devenv shell entry was fragile under automation, for three reasons: a `pnpm install` task in a
  repository with no `package.json`, `vulkaninfo` exiting non-zero with no ICD, and
  `wt step copy-ignored` running outside a worktrunk session. All three are fixed.

**Checking CI from the command line.**

```bash
gh run list --branch feature-base --limit 1
gh run view <id> --json jobs -q '.jobs[].steps[] | select(.conclusion=="failure") | .name'
gh run view <id> --log-failed | grep -E "<step name>" | grep -vE "\[\[36;1m" | tail -25
gh run watch <id> --interval 30 --exit-status &     # a run takes about five minutes
```

