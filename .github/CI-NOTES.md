# CI notes

Status: the gate passes locally inside the locked devenv shell. The updated GitHub workflow
has not yet run; the latest recorded run failed while compiling ImGui's GLFW backend because
`GL/gl.h` was missing. The ImGui target now defines `GLFW_INCLUDE_NONE`, since it uses Vulkan
and does not need GLFW to include OpenGL headers.

## GitHub environment

The workflow follows the [devenv GitHub Actions guide](https://devenv.sh/integrations/github-actions/):
install Nix on Ubuntu, enable the public `devenv` Cachix cache, install devenv 2.4.0, and run
the gate in the shell described by `devenv.nix` and `devenv.lock`. It does not install NixOS
or duplicate the toolchain with apt packages. LFS checkout supplies the render assets.

An earlier attempt exhausted runner disk while building LLVM and other dependencies from
source. The [devenv cache documentation](https://devenv.sh/binary-caching/) explains that a
multi-user Nix daemon can silently ignore substituters from an untrusted user; the public
devenv cache is designed for the pinned rolling input. The Nix action trusts the runner user,
and the Cachix action configures that cache explicitly. No cache token is required to read it.
Cache coverage and available runner disk still need confirmation in an actual GitHub run; an
uncached derivation can still require a large build. CMake, clang-tidy and Nix parallelism are
bounded to limit memory pressure.

## Defects the gate found on CI, and fixed
* `assets/CMakeLists.txt` copied `assets/textures`, which no fresh clone has: Git cannot
  record an empty directory and `90f4a96` removed the last textures. Configure died with
  "cannot find assets/textures" on CI while local trees built because the empty directory
  survived on disk. Fixed by copying the directories that exist and creating the empty
  output directory, plus `assets/textures/.gitkeep`. Verified on a fresh clone: configure
  exit 0, `Avocado.glb` restored at 8,110,040 bytes.
* `devenv.nix` shell entry was fragile under automation: `tasks."project:init"` ran
  `pnpm install` before `devenv:enterShell` in a repository with no `package.json`;
  `vulkaninfo` exits non-zero when the loader finds no ICD, which is the headless case;
  `wt step copy-ignored` ran outside a worktrunk session. All three fixed.

## What the gate runs, and the flags that matter
```
./scripts/ci.sh --llvmpipe --werror
```
format gate → Debug build → clang-tidy → Release build → headless render test. `--werror`
configures with `JETTISON_WERROR=ON`, which puts `-Werror` on the `jettison_warnings`
interface library the four project targets link; a developer's build leaves it off.
`--skip-tidy` exists for a machine without a configured Debug tree, and `--asan` adds a
render test under `clang-asan` (address + undefined sanitizers, X11-only GLFW, Xvfb,
lavapipe). The worktrunk `pre-merge` hook runs `--skip-release --werror --llvmpipe`.

Two facts behind the sanitizer preset. The headless tree builds GLFW X11-only: with both
backends compiled in, a run that uses X11 still loads libwayland, and LeakSanitizer reports
192 bytes in two allocations, every frame in libwayland-client 1.26.0 at disconnect. And
GLFW dlopens the X11 libraries, so the shell running the test needs libX11, libxcb, libXau,
libXdmcp, libXext, libXrandr, libXcursor, libXinerama, libXi, libxkbcommon and
  libpthreadstubs on `LD_LIBRARY_PATH`. `devenv.nix` builds that list in `enterShell` with
`lib.makeLibraryPath`, because a Nix shell does not put package libraries anywhere on its
own; a shell that predates the list fails with `X11: Failed to load Xlib`, and
`scripts/smoke-test.sh` now says so plainly, naming the library and the variable, because
  GLFW's message does not mention the shell at all. GitHub uses the same shell and library list.


## What the render test asserts, and what it costs

The run is judged on the validation layer's output, not only on exit status: the debug build
loads `VK_KHRONOS_validation`, `DeviceContext` installs a `VK_EXT_debug_utils` messenger, and
`scripts/smoke-test.sh` fails the run when the stream contains a validation error. Two lines
are excluded as the machine talking: the loader's reports about duplicate `VK_LAYER_MESA_*`
manifests. On NixOS that is the shell's Mesa against `/run/opengl-driver`; the exclusion
matches only duplicate Mesa layer messages. The entry points are resolved through
`vkGetInstanceProcAddr`, so a
loader without them costs names, not a link failure.

The sequence is six stages (resize, resize, minimize, restore, scene reload, settle), four
frames each, on a window that opens at 640x480 in smoke mode because lavapipe is fill-rate
bound at the real opening size of 1920x1080. Budget 60-90 seconds for the run on this machine,
more on a runner; the app's own watchdog is 120 seconds. Minimizing runs on X11 only, because
on Wayland a minimized surface parks the frame loop in `DeviceContext::WaitOnWindowResized`
until the window is restored. One handled resize is required, not two: under niri (this is
developed through its X11 backend) a resize request is answered with the tiler's tile, which
may already be the window's size, and the stage gives up after eight frames rather than
running the watchdog. `xvfb-run` has no window manager, so on a runner both requests land.

## Checking CI from the command line
```
gh run list --branch feature-base --limit 1
gh run view <id> --json jobs -q '.jobs[].steps[] | select(.conclusion=="failure") | .name'
gh run view <id> --log-failed | grep -E "<step name>" | grep -vE "\[\[36;1m" | tail -25
gh run watch <id> --interval 30 --exit-status &     # a run takes about five minutes
```
`lfs: true` in the checkout step matters: every asset is an LFS pointer in every rewritten
commit, so a checkout without them gives the render test 132-byte files.
