# CI notes

Status: **not green.** `./scripts/ci.sh --llvmpipe` passes locally in about a minute. On
GitHub the workflow got as far as a real CMake configure and stopped at the asset defect
described below; the fix is pushed, nothing has run since. Picking this up means pushing
a commit and reading the first build failure.

## Verified facts about the Ubuntu 24.04 runner
These cost several runs to establish. They are all checked, not guessed.

* `apt.llvm.org` serves `llvm-toolchain-noble-21` (its `Release` answers 200). `llvm.sh 21`
  installs llvm, clang, lld, lldb, clang-tools, dev packages.
* `clang-format-21` and `clang-tidy-21` are **separate packages** that `llvm.sh` does not
  install. `clang-tools-21` contains neither binary. `update-alternatives` then fails on a
  path that does not exist.
* The runner image registers Ubuntu's **clang-18** alternatives at a higher priority, so
  `--install … 21 100` loses auto mode: `clang-format` keeps reporting 18 and the build
  silently gets clang 18. Use a priority above the distro's and `--set` each group
  explicitly, and assert `version 21` on clang, clang++, clang-format and clang-tidy.
  The compiler binaries are `clang-21` and `clang++-21`; registering the `clang++` group
  against `clang-21` hands out a C compiler.
* Ubuntu 24.04 has **no package providing `glslc`**. `glslang-tools` ships `glslang`,
  `glslangValidator` and `spirv-remap` only, and the project's
  `find_package(Vulkan REQUIRED COMPONENTS glslc)` needs the name. `nix profile install
  nixpkgs#shaderc` substitutes shaderc 2026.1 in seconds, which is the same release the
  devenv shell ships.
* Spelling: the validation layers package is `vulkan-validationlayers`, not
  `vulkan-validation-layers`. `wayland-scanner` ships in `libwayland-bin`. `vulkan-headers`
  has no installation candidate; `libvulkan-dev` carries `vulkan/vulkan.h`. GLFW's X11
  backend needs `libxrender-dev` and `libxfixes-dev` as well as the usual X11 packages.
* `mesa-vulkan-drivers` is preinstalled on the image, so apt says nothing about it. Its
  manifest is `/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`. Assert with `find`, and print
  the directory listing on failure: a bare `test -f` exit 1 taught nothing.
* The official devenv GitHub action no longer exists: `cachix/devenv-action` has no refs,
  only zero-star forks. `github:cachix/devenv/v2.4.0` installs and evaluates in about
  four minutes.

## Why CI is not the devenv shell
Running the gate inside devenv was tried and failed: `devenv.yaml` pins
`github:cachix/devenv-nixpkgs/rolling`, whose builds are not in cache.nix.org, so the
runner compiled LLVM, boost, node and the rest from source and exhausted the disk
(`error: writing to file: No space left on device`). So CI mirrors the toolchain from
Ubuntu packages plus nixpkgs shaderc, which is stated in the workflow header.

Proper parity, when it is worth doing:

1. Trim `packages` in `devenv.nix` to the C++ essentials (drop `nodejs_24`, `pnpm`, `go`,
   the Python interpreter and `pyProjectDir`, `pi-coding-agent`, and decide on `gdb` and
   `lldb`). The gate needs clang, clang-tools, cmake, ninja, git, git-lfs, vulkan-loader,
   vulkan-headers, vulkan-validation-layers, vulkan-extension-layer, shaderc, mesa.drivers,
   wayland, libxkbcommon, the X11 development libraries, `xorg.xorgserver` for Xvfb.
2. Add a binary cache (`cachix: ["<name>"]` in `devenv.yaml`, `CACHIX_AUTH_TOKEN` as a
   repository secret) and push one copy of the dev shell from a development machine.
3. Then the workflow becomes: checkout with `lfs: true`, `cachix/install-nix-action`,
   install devenv from `github:cachix/devenv/v2.4.0`, `devenv shell './scripts/ci.sh
   --llvmpipe --werror' 2>&1 | tee ci.log`, upload the log. Note the CLI: devenv 2.4 takes
   the command as an argument to `devenv shell`, not as a `-c` option. That run needs no
   apt at all.

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
GLFW's message does not mention the shell at all. On a GitHub runner the libraries come
from apt, so nothing extra is needed there.


## What the render test asserts, and what it costs

The run is judged on the validation layer's output, not only on exit status: the debug build
loads `VK_KHRONOS_validation`, `DeviceContext` installs a `VK_EXT_debug_utils` messenger, and
`scripts/smoke-test.sh` fails the run when the stream contains a validation error. Two lines
are excluded as the machine talking: the loader's reports about duplicate `VK_LAYER_MESA_*`
manifests. On NixOS that is the shell's Mesa against `/run/opengl-driver`; on a runner the
same class of message comes from apt's layer manifests, so the exclusion is written for the
class, not for one host. The entry points are resolved through `vkGetInstanceProcAddr`, so a
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
