# After phase 13

Work finished after the phases closed, from 2026-11-18 to 2026-11-20, each section as it was written
when the work was done: the comment review, reference frames that hold under load, the gate inside
the sandbox, the pre-review's fixes with ship classes and dry docks, and frames drawn on the GPU
offscreen. The last part of this period, the neutral SDK work for Manifest, is in [neutral
SDK](neutral-sdk.md).

The work finished since the phases closed, as each was done.

## The comment review

Approved 2026-11-18 and run before the SDK work, because every later
change reads these comments. The rule is [coding standards](../coding-standards.md#comments): a
comment stays only if deleting it loses something the code, its names and its types cannot give
back. About 9,100 comment lines in 427 C++ files (10% of 89,000), plus the YAML, CMake and scripts.

| Tier | Scope | Model and effort |
| --- | --- | --- |
| Contracts | Public headers (`include/jettison/...`) and the deterministic modules (`sim`, `celestial`, `spatial`, `physics`, `ai`, `dock`, `script`, `math`) and `serialise`, where a comment holds an ordering, a layout on disk or a determinism hazard | Opus 5.5, high |
| The rest | The other modules' sources, tests, tools, YAML, CMake, scripts | Sonnet 5, high |

- **Comments only.** A commit changes no code: `scripts/comment-only.sh` (every changed line a
  comment or blank) runs before the gate. A comment that would be better as a
  name or a type is listed for later, not changed here.
- **Trim, not delete.** A paragraph that mixes what and why is cut down to the why; a measurement,
  a hazard or a contract is kept, reworded if need be.
- **One module per commit**, so the diff stays reviewable. The first module (`vfs`, small and with
  every kind of comment) is reviewed by the user before the rest; if it lost a hazard, that tier
  moves up a model.
- **Done when** every module is through, the gate is green, and `coding-standards.md`'s test holds
  in a spot check of each tier.
- **Progress:** `vfs` through (2026-11-18) and reviewed by the user; then
  `math`, `celestial`, `spatial`, `script`, `serialise`, `dock`, `sim`, `physics` and `ai`
  (2026-11-18); then every other module's public headers (2026-11-18), with commits for those
  that changed: `reflect`, `audio`, `input`, `core`, `animation`, `scene` and `render`. The
  contracts tier is through. The rest, begun 2026-11-18 on Sonnet 5.5: `app` (twelve
  restating comments out), `render`'s sources (member labels), and the repository's mechanical
  findings (include labels, "Disable copying.", the shader's and the dependency CMake's banners).
  That pass looked at the rest for those patterns alone, so the tier was read again file by file
  the same day on Opus 5.5: every other module's sources and tests, `source/test`, `scripts`, the
  shaders, the assets' YAML, the build's CMake and the formatter's, tidy's and CI's configuration,
  a commit each. Out went restating labels, the step narration left in `DeviceContext.cpp`, history
  clauses, stale claims (the probe's "Planned.", a deleted file in `.clang-format`, bloom as still
  to come in `planet.frag`, the smoke test's "two resizes"), comments broken mid-sentence by
  earlier edits, and a class comment that had drifted onto the struct above it (`GlfwInput.h`).
  The tier is through. A comment-only commit skips the gate; `scripts/comment-only.sh` and the
  format check stand in for it.
- **Done** 2026-11-18. The gate is green over the whole review (`--werror --asan`), but for
  `dock-sequence` drifting under load (12,383 pixels, fixed next, below), which three reruns
  at idle matched with no pixel off. A spot check
  of three files a tier (`BehaviourTree.cpp`, `dock/Definitions.cpp`, `PhysicsWorld.cpp`;
  `AppArguments.cpp`, `EventSounds.cpp`, `AudioSystem.cpp`, a YAML patch, `time`'s CMake), and a
  repository sweep for banners, labels and commented-out code, found two restatements, both cut;
  the test holds. `Crash.cpp`'s two markers stay: they bound the signal handler's code.
  What it left for later is carried forward ([roadmap](../roadmap.md#the-backlog)).

## Reference frames that hold under load

Done 2026-11-18 (the user's, high priority). `dock-sequence` had come out 12,383 pixels off (of
518 allowed) in two gate runs with clang still running, and with none at idle. A line at the
screenshot (frame, fixed time, each sequence's time) showed the shot one frame early under load,
and a trace of each tick's loads found two causes, both a frame's timeline following the machine's
clock: a load requested and drained in one frame was seen at the next tick only when the I/O
worker was slow, and the X server's first resize made `Renderer::Render` skip a frame's draw, so
its materials' textures were asked for a frame late. `ResourceOptions::settleLoads` (set with
`--frame-step`, which also turns hot reload off) drains every load where it is asked for, an
out-of-date acquire recreates and draws, and the `dock-sequence` reference was recorded again a
frame earlier. Exit as met: ten captures, five idle and five under 24 busy loops, each at frame
34, and every view the same count idle and loaded ([build and gates](../build-and-gates.md)). The
residuals the other views carried (`effects` 112 pixels, the rest 5 or fewer) were steady stale
references, so at the user's word every view was recorded again and the allowance went from 518
pixels to none, with llvmpipe's vector width pinned for other machines. Codex's first round: a settled
`Update()` now loops until nothing is in flight, so a load a `finish` asks for settles too, which
found `Finish` holding a slot that such a load could move (fixed, and `resource` joins the
sanitised suites); and a shot is taken only once a frame has drawn it, should a retried acquire
still be out of date. Its second round found that retry a frame late; the user settled it: with
`--frame-step` the renderer recreates until the frame draws (`RendererOptions::drawEveryFrame`).

## The gate inside the sandbox

Done 2026-11-18. Claude Code's sandbox (the managed settings in `~/nixos-config`) lets ccache and
`/tmp` be written and, with `sandbox.network.allowAllUnixSockets` (the user's), Xvfb listen, so the
whole gate runs inside it. With no `/dev/dri` there, Mesa's hardware drivers failed to enumerate
and their loader errors failed the smoke test's missing-shader check, so `ci.sh` pins lavapipe
when there is no `/dev/dri`. Met: `./scripts/ci.sh --werror --asan` passed inside the sandbox, the
reference frames at 0 pixels. Xvfb also served over loopback TCP there (`-listen tcp`), should the
Unix sockets be taken away again.

## The pre-review's fixes, ship classes and dry docks

Done 2026-11-18, after a pre-review of `c04303c..fafa758` ahead of the cross-review, and the
user's ship classes and dry docks with it.

- **A pad's fit is by size as well as rank** (`Docks::Fits`). Rank alone gave a hull any pad of a
  class ranked above or level with its own, however small; a 130 m hull was reserved a 35 x 40 m
  pad. The rank orders the choice and the class's largest hull is the limit. The pinned hashes held.
- **The save game's chain does its own step.** Version 2's move of `galaxy` and `system` into the
  details was in `Read`, behind a step that did nothing, so a version 1 save the chain migrated
  no longer read. The step edits the record the save begins with
  (`BinaryDocument::EditFirstRecord`), and `save-v1.save` reads the same through the chain alone.
- **The dependency check follows every target.** It had kept only targets named `jettison_*`, so
  a sample's library of another name, an optional module behind a helper library, and a link
  inside a generator expression each passed. It now follows every target and the wrapping
  expressions, and stops on a module's link it cannot read.
- **The sample's own registration is tested.** The `sample-space` suite composes the sample through
  `Sample::SpaceGame`'s hooks in the shell's order, and a trader runs a route and sells.
- **Ship classes.** The sample's pad classes are corvette, frigate, destroyer and cruiser in place
  of S, M, L and XL, at the same sizes, the files lower case; the 220 m display hull is `cruiser`.
  The names are the sample's data, so a save's pads are renamed by the sample as it loads
  (`NameOldPadClasses`), not by the dock's chain, and the shell's ship proxies go by a hull's
  length, not its class's name. A hull or a pad that names no class is refused as such.
- **A pad's features.** A layout's pad lists what it offers by name (`PadLayout::features`), a ship
  asks for one (`RequestPad::feature`, kept on its `Docking`), and the docks give it only a pad
  that offers it, queue it for one, or refuse (`Reason::NoPadOffers`). What a feature is for is the
  game's.
- **Damage and repair, in the sample.** A hull is damaged by a contact of 5 m/s or more, and
  repaired for its owner's credits docked on a pad that offers `dry-dock`, two of Highport's.
- **The shell's smoke hooks are the game's screen, not a trade** (`Game::SmokeGameUi`,
  `SmokeGameUiWaiting`, `SmokeGameUiAnswered`, after the review's `SmokeTrade` three); the summary
  counts `screens=` and `answers=`.

Three pinned hashes moved twice, the dock's approach and deck and the sample's traders, each time
by bytes alone: with the classes' old names written back into the save, the old pins passed; then
with a docking's feature left unsaved, the hashes were those after the rename. Manifest's seven
classes (skiff to heavy destroyer) and its dry-dock platform are its own data, not yet authored.
The commit `3f8fb1c` carries H05's code under the message of a gate change; it stays as it is (the
user's decision, 2026-11-18), as it is pushed and Manifest's pin follows it.

## Frames drawn on the GPU, offscreen

Done 2026-11-20 (the user's, placed after N02; entries 21 and 28). The renderer draws with no
surface or swapchain when asked (`--offscreen`): GLFW on its null platform, a device with no present
support or swapchain extension needed, and `Swapchain` owning two `B8G8R8A8_SRGB` images it hands
out in turn, drawn into with no acquire, semaphores or present and exported for a transfer, which
`--screenshot` reads as it read the swapchain's
([architecture](../rendering.md#why-clustered-forward)). `--run` takes a line at a frame
(`@40 undock`) or a simulation time (`@2.5s traffic`). `smoke-test.sh --offscreen` runs with no
display, pinned to the 5060 Ti by name on `lythir` and by `DeviceChoice` elsewhere, and the gate's
plain smoke test runs so unless it has `--llvmpipe` or no `/dev/dri`. Claude Code's sandbox cannot be
given the 5060 Ti's nodes (its bubblewrap call mounts a fresh `/dev` after every bind), so the user
excluded `ci.sh` and `smoke-test.sh` from it ([build and gates](../build-and-gates.md#headless-render-test)).
Exit as met: the plain smoke test and the shell's checks under it passed on the 5060 Ti, validation
clean, in 29 s against 49 on lavapipe under Xvfb, at 1.3 ms a frame against 79; the gate's stage took
35 s with its build, from 66. The rest is the jumps' CPU work, 9 to 11 s each on either device.
The reference frames, the sanitised run and CI stay on lavapipe under Xvfb, unchanged, and a new
view, `orbit-offscreen`, is the `orbit` view drawn offscreen on lavapipe, the same PNG to the byte.
The gate passed with `--werror --asan` in 324 s.
