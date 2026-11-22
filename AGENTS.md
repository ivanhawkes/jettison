# Jettison

A C++20 game engine with a Vulkan renderer, intended for multiple game types. Manifest is
its first production consumer and owns its gameplay and content. The small space-trading
example remains automated-test/sample coverage, separate from reusable engine code: its code
and content are in `source/samples/space` (H05). Linux, Clang 21; `docs/` records the contracts.

## Agent Directions

Answer concisely.

## Environment & Tooling Instructions (NixOS + Niri + devenv.nix)

You are operating inside a restricted `devenv.nix` shell environment on **NixOS** running the **Niri** Wayland compositor.

### 1. NixOS / devenv.nix Constraints

- **Do not** attempt to install global packages via `apt`, `brew`, `pip`, `npm -g`, `nix-env`, or `sudo`. All compilers, build tools, and language servers are declaratively managed by `devenv.nix`.
- **Do not** hardcode FHS paths like `/usr/bin/...` or `/bin/bash`; rely strictly on `$PATH` provided by the active `devenv` shell.
- If a required CLI tool, LSP binary, or C++ library is missing from `$PATH`, stop and report the exact package name needed so I can add it to `devenv.nix`.

### 2. Mandatory `compile_commands.json` Generation

Before running any C++ code analysis, refactoring, or LSP queries:

1. Check if `compile_commands.json` exists in the repository root or active CMake build directory (`build/compile_commands.json`).
2. If missing, inspect `CMakeLists.txt` (and `CMakePresets.json` if present) and ensure `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` is enabled during CMake configuration (e.g., `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` or `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` in `CMakeLists.txt`).
3. Symlink or copy `build/compile_commands.json` to the project root (`ln -sf build/compile_commands.json compile_commands.json`) if it is not already accessible there so LSPs and static analyzers detect it automatically.

### 3. Language Services (LSP) & Code Intelligence

You are running in a terminal TUI environment (Claude Code, Codex CLI, or Pi). Enable and prioritize native LSP / code-intelligence tools over brute-force text `grep` across our 100K-line codebase whenever those capabilities or extensions are available in your harness:

- **C++:** Use `clangd` (backed by `compile_commands.json`) for symbol definitions, references, type hierarchy, and diagnostics.
- **YAML:** Use `yaml-language-server` for schema validation and key/structure checks on engine configuration and entity definitions.
- **CMake:** Use `cmake-language-server` / `neocmakelsp` for target, variable, and dependency resolution.
- If your current TUI agent lacks direct MCP/LSP tool bindings in this session, run `clangd --check=<file>` or use `compile_commands.json` flags directly via bash before falling back to targeted `rg` searches on `.hpp` headers.

## Coding standards

Read [coding standards](docs/coding-standards.md) before any work that changes code, scripts,
build files or data files, and review against it. It is the single source of truth for naming,
comments, ownership, errors, logging, determinism, dependencies, testing and commits; this file
does not restate it.

## Engine and game responsibilities

- Before engine integration work, read `docs/game-integration.md` and the sibling
  `../manifest` documents it lists, starting with the task queue, `docs/delivery-order.md`.
  Read the local checkout, not GitHub, which lags and may hold work on other branches.
- Map required gameplay mechanics to public SDK capabilities. Provision demonstrated
  reusable gaps in Jettison; leave game rules, ownership policy, prices and content in
  Manifest. Record acceptance tests in both an independent engine consumer and Manifest.
- Keep sample gameplay code/assets in explicit sample/test targets outside engine code.
  Generic host/physics/AI/scene APIs must not require a space game or sample asset pack.
- Reuse engine YAML, VFS, registry and persistence mechanisms. Do not copy Manifest's
  economy into the engine or make Manifest a build dependency.
- Follow the user's model policy: OpenAI agents plan/review read-only and may edit
  explicitly requested documentation; implementation and gate execution belong to the
  selected implementer. Local runs use the machine's own cores, clang-tidy included:
  nothing is bounded to 2, and `MemAvailable` is read before a full-parallel gate
  (docs/build-and-gates.md, "The gate").

## Where to look

- **The work:** [roadmap](docs/roadmap.md). Each sub-phase row names its catalogue entries; read
  the row and those entries, not the whole catalogue. "Carried forward" lists deferred work and
  where it lands. Finished phases move to [history](docs/history.md), and settled decisions are
  in [decided](docs/decisions.md).
- **The design:** [architecture](docs/architecture.md) (layout, module graph, simulation and
  presentation split, frames, the render frame), [scope](docs/scope.md) (what is in and out).
- **The rules:** [coding standards](docs/coding-standards.md), read before changing code.
- **Building and the gate:** [build and gates](docs/build-and-gates.md).

## Building and testing

Everything runs inside the devenv shell (`.envrc` loads it). Leaving `devenv shell` kills what it
started, `setsid` included, so a long-lived process (Xvfb, the app) goes in one script run as a
background task.

```bash
cmake --build --preset clang-debug                                   # configured already
ctest --test-dir build/clang-debug -R '^physics$' --output-on-failure  # one suite, named per module
./scripts/ci.sh --werror --asan --log build/gate.log                 # the gate, before a push
./scripts/tidy.sh --summary                                        # the checks, one line when clean
```

For one unit, `clang-tidy -p build/clang-debug <file> 2>&1 | grep -E ': (warning|error): .*\['`:
raw clang-tidy prints hundreds of thousands of warnings generated, macro-expanded and suppressed
included. Dependency code is already out of the checks — the path filter in `scripts/tidy.py` picks
252 of the 894 units, and `.clang-tidy`'s `HeaderFilterRegex` suppresses findings located in
dependency headers — so what survives that grep is the project's own. A plain `scripts/tidy.sh`
prints at most 20 distinct diagnostics and names `build/clang-debug/tidy-diagnostics.log`, which
holds the whole text; `--full` prints everything, `--summary` the tally.

Run the gate as a background task and wait for it to finish rather than polling with `sleep`.
With `--log` it prints the stages and a summary, and for a failed stage its errors; the full output
is in the log. Warm, it takes 3 to 4 minutes (223 s on 2026-11-14), two thirds of it the
lavapipe smoke tests and reference frames; with both caches cold, several minutes more. The last
commit of each chunk of work leaves the gate green with `--werror --asan` before a push
([coding standards](docs/coding-standards.md#commits)).

Nothing bounds parallelism: the builds, the suites and clang-tidy take the machine's cores. Free
memory here is a question of whether the user's local AI is resident (`strata-host`, about 57 GiB,
not always running): when it is not, the build is free to take the machine, and when it is, read
`MemAvailable` first — 3.4 GiB free with it up, measured 2026-11-17, is a full run that thrashes.
The per-copy peaks and the arithmetic are in
[build and gates](docs/build-and-gates.md#the-gate).

After the gate, Codex reviews the change adversarially with `scripts/cross-review.sh` (a
background task, about ten minutes a round, no edits while it runs); answer each finding with a
fix or evidence, two rounds at most, then the user settles what is open
([build and gates](docs/build-and-gates.md#cross-review)).

Presets: `clang-debug` (Wayland), `clang-release`, `clang-headless` (X11 GLFW, for Xvfb) and
`clang-asan` (headless plus sanitisers). The sanitised and Xvfb runs use lavapipe.

## Finding things

- **clangd** is available through the LSP tool (the compile database is `build/clang-debug`). Use
  it for definitions, references and diagnostics before grepping or building to find out.
- **Dependency sources** are in `build/clang-debug/_deps/<name>-src` (entt, joltphysics, imgui,
  rmlui, fastgltf, ...), at the pinned versions. Their headers are the authority on their APIs.
- **Context7** (`.mcp.json`) gives library documentation for anything not on disk, or for prose
  docs.
- C++ files are formatted automatically after every edit (`.claude/hooks/format-edited.sh`).

## Seeing the app

`scripts/capture.sh <out.png> [render_test arguments...]` writes a frame from inside the frame loop
on lavapipe under Xvfb, the same to the byte every run, the camera placed with console lines:
`--run "orbit <yaw> <pitch> <distance> [target]"` (distance from the target's centre; a target by
the scene entity's id, as `ostra`). render_test is the shell with no game over the test content's
static scenes (`source/test/content/scene`). `--no-build`
skips the build; `--references [--update]` runs the reference views in
`source/test/frames/views.txt` ([build and gates](docs/build-and-gates.md#reference-frames)). Run
captures one at a time: each stops the Xvfb it started, under any other still drawing.

`--run "@<frame> <line>"` or `--run "@<seconds>s <line>"` runs a line later in the run (a
`sequence`, then a `screenshot`). `render_test --offscreen` draws with no display or window at all, so outside the sandbox it
draws on the GPU (`--gpu 5060` on lythir, never the 5070 Ti), and `--screenshot` writes its frame;
`./scripts/smoke-test.sh --offscreen` is the smoke test so.

For a frame that needs keys or the UI, run `build/clang-headless/source/test/render_test [--scene
...]` on `Xvfb :97 -screen 0 1920x1080x24` with lavapipe's ICD in `VK_ICD_FILENAMES`
(`scripts/lib/headless.sh` finds it), and the console's `screenshot <file.png>` writes the next
frame; or grab the screen with `xwd -root -silent | magick xwd:- out.png`, from `nix shell
nixpkgs#xorg.xwd nixpkgs#imagemagick nixpkgs#xdotool`.

- Input samples key state once a frame and a lavapipe frame can take a second, so hold keys:
  `xdotool keydown E`, sleep about 2.5 s, `keyup`. A tap is missed. Focus first with
  `xdotool windowfocus --sync <win>`.
- The console takes `xdotool type --delay 150 '...'`, then `key Return` as a tap.
- Compare frames with `magick compare -metric AE -fuzz 2% a.png b.png diff.png`.
- A killed script may carry on: check `pgrep` before reusing a display.
- `--user-dir <dir>` keeps a run's settings and saves apart, and across two runs.

## Writing

- A phase or sub-phase updates its roadmap row (done, the date, what was built, the exit criterion
  as met) and the documents it changed, in the same commit.
- Commit messages, comments and spelling follow the [coding standards](docs/coding-standards.md).
