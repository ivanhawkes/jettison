# Jettison

A C++20 Vulkan game engine for a space trading game that lives in its own repository and consumes
this one as a CMake package (`Jettison::Engine`). Linux, Clang 21, built in phases. The documents
in `docs/` are the record; this file only says where to look and how to work here.

## Where to look

- **The work:** [roadmap](docs/roadmap.md). Each sub-phase row names its catalogue entries; read
  the row and those entries, not the whole catalogue. "Carried forward" lists deferred work and
  where it lands. Finished phases move to [history](docs/history.md).
- **The design:** [architecture](docs/architecture.md) (layout, module graph, simulation and
  presentation split, frames, the render frame), [scope](docs/scope.md) (what is in and out).
- **The rules:** [coding standards](docs/coding-standards.md). The most important: the
  simulation modules (`sim`, `celestial`, `spatial`, `physics`, `ai`, `dock`, `script`, and `math`) are
  deterministic (no wall clock, the engine's `Random`, no unordered iteration deciding an outcome,
  integer currency, `-ffp-contract=off`); report through `Log.h`, never `std::cerr` or spdlog;
  `m_` and `m_p` members, PascalCase functions.
- **Building and the gate:** [build and gates](docs/build-and-gates.md).

## Building and testing

Everything runs inside the devenv shell (`.envrc` loads it). Leaving `devenv shell` kills what it
started, `setsid` included, so a long-lived process (Xvfb, the app) goes in one script run as a
background task.

```bash
cmake --build --preset clang-debug                                   # configured already
ctest --test-dir build/clang-debug -R '^physics$' --output-on-failure  # one suite, named per module
./scripts/ci.sh --werror --asan --log build/gate.log                 # the gate, before a commit
```

Run the gate as a background task and wait for it to finish rather than polling with `sleep`.
With `--log` it prints the stages and a summary, and for a failed stage its errors; the full output
is in the log. Warm, it takes 40 to 70 s; with both caches cold about 6 minutes. A commit leaves
the gate green with `--werror --asan`.

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
`--run "orbit <yaw> <pitch> <distance> [body]"` (distance from the body's centre), or behind or in
the ship with `--run "pilot chase|cockpit"`. `--no-build`
skips the build; `--references [--update]` runs the reference views in
`source/test/frames/views.txt` ([build and gates](docs/build-and-gates.md#reference-frames)). Run
captures one at a time: each stops the Xvfb it started, under any other still drawing.

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

- Commits: the subject is `Area: what changed, and what it was for`; the body records what was
  measured and why the change takes its shape, in plain prose, as `git log` shows. British
  spelling (serialise, colour), as the code and documents use.
- A phase or sub-phase updates its roadmap row (done, the date, what was built, the exit criterion
  as met) and the documents it changed, in the same commit.
- Comments say why, and what was measured; the code already says what.
