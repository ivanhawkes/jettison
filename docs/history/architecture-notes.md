# Notes moved out of the reference documents (2026-11-25)

When the reference documents were restructured on 2026-11-25 (one document per feature, a glossary,
the backlog renumbered J01–J29), each writer moved the material that records how something came to
be, or measurements no current choice rests on, here. It is kept as written, by the document it came
from. Where an item repeats an entry in [phases](phases.md), [the catalogue](catalogue.md) or
[after phase 13](after-phase-13.md), those are the fuller record.

## From ai

Material from docs/architecture.md, "Steering" and "Decisions" (lines 461-492, 556-597), and the
AI bullet of "The engine today" (line 1929), left out of docs/ai.md as history.

- **2026-11-12, phase 9b (architecture.md, "Steering")**: steering was built for the space sample's
  shuttles; the shell's console `traffic` command sent up to sixteen of them to a ring about a
  station. The destination's shape-kept-clear flag was first `Destination::byHull` (9c), now
  `byShape`.
- **2026-11-12, phase 9f (architecture.md, "Decisions")**: the retired space sample's traders and
  fleet were the first users of both utility AI and behaviour trees. Its traders' tree repeated a
  three-node jump loop for each leg; a run that could not be made waited half a minute and chose
  again (roadmap.md, "Decisions, not yet placed").
- **13d (architecture.md, "Decisions")**: the helm (`Sim::Helm`, `Ai::TakeHelm`, `LeaveHelm`) was
  added for a player's fleet in the space sample.
- **Stale claim dropped (architecture.md:456, 588-590, 595-596)**: "Manifest's traders' tasks plan
  from wherever the ship is", "their `dock` gives up a pad", "their `cruise` wakes as its cruise
  arrives" and "Manifest's are their successors". As of 2026-11-25 Manifest's source has no
  `Ai::Behaviours`, `DefineTask`, `TaskWake`, `TakeHelm` or `Ai::Choose`; it uses `Ai::FindPath`,
  `Ai::Waypoints`, `Ai::Steering` and `Ai::Destination` (source/game/space, source/game/travel).

## From animation

Material left out of the feature document as history. Each item gives its date and the document it
came from.

- **2026-11-10** (docs/history/catalogue.md entry 27; docs/scope.md, "Animation", rows Clips and
  TRS): phase 1 moved the glTF clip player (`Model::EvaluateSkin`) and the rotation animation out of
  `Model` and `Scene` into `engine/animation`, as `Rig`, `Components.h` and `Playback.h`,
  unchanged in behaviour. Scope still words both as future ("it moves into `engine/animation`",
  "generalised to keyframed TRS tracks (10d)"); both were done by 2026-11-13.
- **2026-11-13** (docs/architecture.md at HEAD, "Cinematics"): the retired space sample bound the
  roles `ship`, `pad` and `station` each frame and played `cinematics/dock.sequence.yaml` as its
  ship landed on a pad, hiding the flight readout under it. Since the space extraction, the
  `dock-sequence` reference frame plays the sequence from the console on the orbit scene, whose
  entity ids `ship` and `pad` fill the roles unbound.
- **2026-11-13** (docs/history/catalogue.md entry 27): the first clip-file rig was a pad's docking
  clamps, built and fitted to the hull by the space sample's station dressing, closing on a docked
  ship and opening as it was let go. The clamps' code went to Manifest with the space extraction;
  `source/test/content/animations/pad-clamp.clip.yaml` stays as the engine's fixture.

## From app-shell

Material from architecture.md "The engine today" (the app shell, piloting, picking, saves, smoke
sequence and "the game a program composes" bullets) that is history rather than contract.

- **The space sample** (8b to G01, retired 2026-11-22; architecture.md, "The engine today").
  `samples/space`, `Sample::SpaceGame`, run by Jetsam: the first slice of Manifest's plan as a game
  composed into the shell, with its galaxy, trading, traders, fleet, piloting and dressing. It flew
  a ship off and onto its pad in the render test's piloting stages; the ground sample's rover drives
  those stages now. Manifest tests all space code; the sample's record is in history for Manifest's
  graphical client to take up.
- **The game a program composes** (H05 of Manifest's delivery order; architecture.md). `App::Game`
  and `App::Shell` were cut so the shell carries no game: with none, Flotsam and render_test run
  the engine alone. The rendered shell was left building its own world rather than starting
  through `Host::Session` (H02), and still does (2026-11-25).
- **The shell's phase labels** (architecture.md): piloting 7b, picking 7c, saves and settings 8a,
  sound 8c, game UI 8b, scripts 11b, cinematics and grading 11c, screenshots 10a. The features are
  described without the labels in app-shell.md.

## From architecture

Material removed from `docs/architecture.md` in the 2026-11-25 restructure, as history. Each item
names the section it came from.

#### The repository's first layout (from "Repository layout")

- Phase 1 of the roadmap moved the existing code into the module layout (`core`, `animation`,
  `render`, `app`, the executables under `tools/` and `samples/`). It stubbed every other
  in-scope module: each had its directory, target, public headers and a passing test, and every
  function that needed an algorithm threw `JETTISON_NOT_IMPLEMENTED()` until its phase. The stub
  phase is over: today the macro is used only by `core`'s own test (`core/tests/AssertTests.cpp`),
  and stays for future stubs.

#### Parked code (from "Repository layout", "Parked code")

- Phase 1 parked the scene graph in `render`. There it loaded models through the device and
  handed the renderer `Model` pointers, while the renderer read its camera and lights back out of
  the scene's registry: a cycle. Phase 6a took it out. The scene became `engine/scene`, naming its
  models by virtual path and loading each model's rig through the resource manager; the renderer
  resolves the same paths to device models through its own cache, behind `IRenderer.h`. The rig
  is read from the glTF file by `animation`, which moved to the World tier, below `scene`, so the
  clip player the scene drives was no longer a sibling edge of `render`'s. Nothing has been parked
  since.

#### Executables (from "Repository layout")

- Jetsam, the runtime that ran the space sample, was retired with the sample (2026-11-22); the
  game has its own repository. `render_test` was called `test` until CTest reserved that name
  (see `source/test/CMakeLists.txt`).

#### When the sibling edges arrived (from "Module dependencies")

- `scene` → `sim`, for the proxies of the simulation's entities (`scene/SimProxies.h`): phase 6b.
- `ui` → `render`, the UI drawing into `Render::Overlay`: phase 8b.
- `spatial` → `sim` and `ai` → `physics`: phase 9b.
- `dock` → `ai` and `physics`: phase 9c. The dock suites fly round a platform on a ferry of
  their own frame model.
- `transit` → `physics` and `ai`: the space extraction (done 2026-11-23), which moved star
  systems as physics frames, cruising, lanes, gates and routes to Manifest, built on `transit`,
  `physics` and `ai`. The `neutral_words` allowance list (`cmake/neutral-words.txt`) was emptied
  by the same extraction.
- The old tier diagram drew `devtools` as part of `ui` and only `app` → `host` for the shell;
  the 2026-11-25 graph is generated from the CMake lists.

#### Simulation and presentation (from "Simulation and presentation")

- Reasons (`sim/Reason.h`) arrived in phase 12c. The old text named a "`Shell.Reason`" that was
  never registered; the registered reason enumerations are `Dock.Reason`, `Ai.Reason`,
  `Physics.Reason` and Manifest's `Space.Reason`.
- Neutral frames (`Sim::InFrame`, `Sim::FrameId`, `Sim::kNoFrame`) arrived under the label A01.
- A star system's scene, and the jump between two, were the retired space sample's (`spacescene`,
  retired 2026-11-22), for Manifest's graphical client to build.

#### Frame and tick structure (from "Frame and tick structure")

- The simulation phase was described as "(commands, AI, economy, physics, rails)". The engine has
  no economy; that is Manifest's. The old text also put "a game's stage first (which may replace the
  scene)" in the sync phase; the shell has no such hook. Its sync phase is `sim.sync` then
  `sim.events`, and a game places its own systems among them with `Game::RegisterFrame`.

#### Threading (from "Threading")

- The section read "To begin with: the main thread runs everything above, and one I/O worker
  thread". Since then the script watchdog thread (`script/src/ScriptHost.cpp`) and miniaudio's
  device thread joined it; Tracy's client adds threads when `JETTISON_PROFILE` is on.

## From audio

- 2026-11-25, docs/audio.md (from architecture.md "Audio" and history/catalogue.md entry 24): the
  test content once mapped the docking sounds to a berth's clamps closing and letting go
  (`defs/sounds`, clip events `clamp.closed` and `clamp.released`), and the shell once played a
  station's hum and the space sample's four event sounds. None of those definitions ships now: the
  test content has no `defs/sounds`, and only the audio and mounts suites write event sounds. The
  comment in `source/test/content/animations/pad-clamp.clip.yaml` still says the sounds are data.

## From build-and-gates

Material removed from build-and-gates.md as history. Each item has its date and source section.

#### Origins

- (undated, before 2026-11-10; "Build and gates" intro) build-and-gates.md consolidated the build
  sections of the old README and `.github/CI-NOTES.md`, which is gone.
- (undated; "Unit tests") The render smoke harness was called `test` until CTest arrived; CMake
  reserves that name once testing is enabled, so it became `render_test`.

#### The planned dependency set (from "Planned dependencies and the probe")

Libraries were fetched ahead of use (`JETTISON_PLANNED_DEPENDENCIES`) and left the set as phases
began to link them:

- 2026-11-10: measured on 24 cores, Release, empty tree: the planned set added about 45 s of
  configure (mostly downloads), about 410 CPU-seconds of compilation (217 without, 627 with) and
  about 0.7 GB of disk per tree; at a runner's four cores, about 70 s of compilation per tree.
- Phase 5a (2026-11-11): KTX-Software left the set; the headless trees fetch it and build
  `ktx_read` (not `ktx`): 63 objects, about 75 CPU-seconds of a 1,700 CPU-second headless Debug
  build, and a 217 MiB clone on first configure (measured 2026-11-11).
- Phase 7a: Jolt left the set, for the physics suite run under the sanitisers.
- Phase 8b: FreeType and RmlUi left the set; the headless trees run the UI suite and draw its
  screens in the render test.
- Phase 8c: miniaudio left the set; the headless trees run the audio suite with no device, and the
  render test plays to miniaudio's null backend.
- Phase 11b: WAMR, the last, left the set; the headless trees run the script suite under the
  sanitisers. The set has been empty since.

#### Scripts in the build

- 2026-11-22: the smoke mod, a script of the shell's bindings that heard the space sample's trades,
  went with the space sample. A mod in the user's mods, which the packed smoke run mounted until
  then, became the host suite's.

#### Parallelism and crashes (from "The gate" and "CI")

- 2026-11-14: a cold gate's clang crashed twice at full load, each time on a file that then built
  alone. Other programs crashed on the machine in the same days, so it was taken as the machine's
  fault. A parallel bound of 2 was set as a workaround, on CI as well
  (`CMAKE_BUILD_PARALLEL_LEVEL`, `CTEST_PARALLEL_LEVEL`, Nix's `max-jobs`).
- 2026-11-17: the crashes were read as probably an unstable motherboard clock speed, and every bound
  came off, here and on CI. Memory pressure, the reason written down at the time, was then measured
  (clang 1,051 MiB peak, clang-tidy 1,151 MiB peak); the runner's six build copies want 6.3 GiB and
  its four tidy copies 4.6 GiB, on a standard hosted runner of four cores and 16 GB.
- 2026-11-17: the `jobs` case "a completion can enqueue more work" failed once in a gate run;
  four parallel runs at full core count passed it, and it passes alone.

#### clang-tidy (from "The gate" and "clang-tidy")

- Since the checks were curated: three checks reported hundreds of findings on the first run, all
  house style, and were excluded with their counts (brace-less `if`, explicit `{}` member init,
  enum-size advice on Vulkan-facing enums).
- 2026-11-12, Ryzen 9 3900X: every unit on all cores in 93 s, against 627 s at the two jobs it ran
  with before (a Release build overlapping some of it); with nothing changed, 0.7 s.
- 2026-11-16, CI job log: the tidy stage was 786 s of a 2,732 s gate (29%), a cold cache every run,
  at the bound of 2 jobs. CI no longer runs tidy.
- A narrowing conversion planted in `Theme.h`, reported by the four units including it, measured
  the output cap: 32 lines full, 1 line capped. A one-unit direct run reported 280,635 warnings
  generated (suppressed and macro-expanded included) and "Suppressed 56 090 warnings (56 090 in
  non-user code)"; the filter picked 252 of 894 units.

#### The gate's output

- 2026-11-12: a passing `--werror --asan` gate with `--log` printed 20 lines, against 658 in its
  log, with warm caches.
- (undated) The suites finished in 42 s with `-j`, the space sample's 39 of them dominating (the
  space sample and its suite were retired on 2026-11-22).
- 2026-11-18: a fresh `clang-cpu` configure fetched doctest, EnTT, fastgltf, FastNoiseLite, GLM, Jolt,
  mikktspace, spdlog, Tracy, WAMR, yaml-cpp and zstd and nothing else; the tree built in 720 steps
  and its 22 suites passed (before ENet and libsodium were added on 2026-11-20).
- Until 2026-11-22: the space sample's traders' hash was pinned in a `sample-space` suite, run in Debug
  and Release but not under the sanitisers (twenty minutes of seven traders, the gate's longest
  simulation).

#### Separation checks (from "Required separation checks (planned)")

- The game integration contract asked for a broader consumer matrix than the two-module probe: a
  CPU-only non-space simulation, a non-space physics scene and a space example. Delivered: the
  CPU-only configure (`clang-cpu`, a gate stage), the non-space CPU consumer
  (`source/test/consumer`, Manifest's H04), engine assets held free of sample content (H05, the app
  suite's `Game` cases), the physics suite's room as a non-space physics scene (W01), and
  `jettison_check_dependencies` on every configure (W01). The space example lives in Manifest.

#### Smoke test (from "Headless render test")

- 2026-11-20, RTX 5060 Ti offscreen, with the space sample's jumps: 29 s for the three runs and the
  start-up checks, against 49 s on lavapipe under Xvfb; 1.3 ms a frame on the GPU against 79 ms.
- Until 2026-11-22 the space sample drove the smoke run's flight, a trade on its market screen and
  a jump to another star system; the trade and the jump went to Manifest, and the jump stage was
  removed.
- 2026-11-22: the whole run (start-up checks, packs and the three runs) took 33 s on lavapipe under
  Xvfb.

#### Reference frames (from "Reference frames")

- Phase 10a (2026-11-13): five views (the orbit scene as it opens, Ostra by day and by night, Corva
  and its rings, the Vesper scene as it opens) in 23 s together, most of it transcoding the planet's
  maps on one thread; captured twice, no pixel differed. Captures replaced grabbing the Xvfb screen
  with `xwd` after a fixed wait, and console lines (`--run`) replaced `xdotool` keys held for seconds.
- Phase 10b: a sixth view, `effects` (the transparent pass's bench), three seconds in; a view's line
  may give its own `--after-frames`. Six views in 24 s.
- Phase 10c: a seventh, `post` (the post chain's bench), two seconds in, with `--ambient-occlusion`;
  seven views in 29 s.
- Phase 11c: `graded` (the orbit scene through `grade cold-light`) and `dock-sequence` (the docking
  cinematic 1.5 s in); every earlier reference stayed the same to the byte with the grade's code in
  the output pass.
- Phase 11d: `market-de`, the market screen in German.
- 2026-11-18: two timing leaks found on `dock-sequence`: a load requested and drained in one frame was
  seen a tick late when the I/O worker was slow, and the X server's first resize skipped a frame's
  draw. Either moved the shot one frame, 12,383 pixels, 24 times the allowance of the time. Fixed by
  `ResourceOptions::settleLoads` and `RendererOptions::drawEveryFrame`. Ten runs, five idle and five
  under 24 busy loops on 24 cores, each shot at frame 34 with the sequence at 1.516666 s; every view
  the same count idle and loaded. The allowance of one pixel in a thousand (518) was dropped to
  zero: it had hidden stale references, `effects` 112 pixels behind the thrusters' plumes, so every
  view was recorded again.
- 2026-11-20: `orbit-offscreen` added; it came out the same to the byte as `orbit`.
- 2026-11-22 (the space extraction): the views moved to the test content's static scenes;
  `render_test` became the shell with no game. `effects` and `post` came out the same to the byte;
  the planets' views differed by 1 to 18 pixels, and `orbit` and `graded` by about 300, the bodies'
  spin now a float Euler angle. `docked` went with the sample; `dock-sequence` starts from an orbit
  camera 30 m off the shuttle, its roles the scene's `ship` and `pad`; `saves-de` took `market-de`'s place.
  Every view was recorded again.

#### Benchmark

- Phase 5 measured the frame budgets with `--benchmark` (see the phase 5 row); phase 6c measured the
  spatial index's octree against a sorted grid with its benchmark (see decided).

#### Cross-review (from "Cross-review")

- 2026-11-14, the first trial: Codex reviewed the commit that closed phase 13, which had passed the
  gate and a run by hand under Xvfb. In ten minutes it built Debug, ran the app suite and wrote six
  scratch reproductions, each a bug the suites had missed: a deliberately vacated helm retaken on
  load; a ship left at its last throttle when the shell attaches elsewhere; a market still bound to
  the previous station; shipyard data with zero mass or a negative price accepted; a scene lost by
  attaching during a jump; and the shuttle's eye point used in the transport. It marked all six
  blockers, which overstated the last.

#### CI (from "CI")

- An early attempt ran out of runner disk building LLVM from source; the fix was the devenv Cachix
  cache, named explicitly in the Cachix action (a multi-user Nix daemon can ignore substituters from
  an untrusted user).
- An early run failed compiling ImGui's GLFW backend because `GL/gl.h` was missing; the ImGui target
  defines `GLFW_INCLUDE_NONE`.
- Defects the gate found on CI: `assets/CMakeLists.txt` copied an `assets/textures` directory a fresh
  clone lacks (Git records no empty directory); devenv shell entry was fragile under automation (a
  `pnpm install` task with no `package.json`, `vulkaninfo` exiting non-zero with no ICD, and
  `wt step copy-ignored` outside a worktrunk session). All fixed.
- 2026-11-16, the full gate on the runner: 2,732 s; clang-tidy 786 s, Debug build 665 s, Release
  build 580 s, lavapipe smoke test 489 s, reference frames 160 s, the two suites 27 s and 24 s,
  format 1 s, with 94 s of runner setup. The job took 47m34s of a 60-minute timeout; later runs ran
  past it.
- 2026-11-25 (the user's decision): CI was cut to the `clang-headless` build and its packed smoke test on
  lavapipe, on a push to `main` and by hand; the full gate took 45 to 60 runner minutes and failed on
  the job's timeout, and every commit has already passed it locally.

## From docks

Moved out of docs/architecture.md, "Docks" (lines 493-555), when docs/docks.md was written
(2026-11-25).

- **Deck traffic, retired** (built in phase 9e; retired 2026-11-22; from architecture.md "Docks").
  Deck traffic carried a docked vehicle's crates across the deck in the simulation, by buggies on
  roads made from a layout. The space extraction retired it with the space sample, its one user, on
  2026-11-22. Its two components, `Dock.Transfer` and `Dock.Buggy`, were dropped in slice 7 of the
  space extraction. (architecture.md said an older save's storages of them "are read and left out";
  no such code remains, and formats change in place until player testing.)
- **Saved names since slice 7** (2026-11-23; from architecture.md "Docks"). The saved and reflected
  names became the docks' own (`Dock.Berth`, `Dock.PlatformLayout` and the rest) in slice 7 of the
  space extraction. Files written before were rewritten, not migrated: the chains read the current
  version alone.
- **Approach measurements** (from architecture.md "Docks", measured with the dock suite's largest
  platform, Ostra Highport, berth 1). Flown out at the lift's 20 m/s, the climb to the point above
  the berth is 755 m and takes 39 s. A vehicle its pilot flies is lifted `kLiftHeight` (40 m) and
  held for the pilot 2.7 s after the clamps open. A shuttle from 10 km on the platform's far side is
  over its berth in 117 s and clamped 24 s later, touching nothing on the way (2026-11-21, after the
  stopgap below; 111 s before it).
- **The way in's stopgap** (2026-11-21, for Manifest's zero-client run; from architecture.md
  "Docks" and the roadmap). A vehicle flown straight to the point above its berth, coming round the
  platform fast and sideways, overshot into the sphere and circled until its reservation lapsed. The
  stopgap (`Docks::FlyInbound`, `FlyAbove`) stops it first at a point out along the berth's normal,
  as far beyond the point above as it stops in from its top speed, then flies it straight down the
  normal from rest. The proper way in is roadmap item J09.

## From files-and-mods

Material moved out of the feature document (sources: architecture.md "Files, packs and mods",
history.md catalogue entries 10, 11 and 30, vfs/Pack.h). Each item keeps its date.

- **Pack compression measured** (2026-11-13, phase 11a; architecture.md and `vfs/Pack.h`). On the
  retired space sample's assets, 87 MB by Pack.h's count and 90.3 MB by architecture.md's, 75 MB of
  them KTX 2 textures already zstd-supercompressed, zstd at level 1 saved under 0.1 % on the textures
  and models and about half on the text. 127 of 136 files were compressed and the pack was 88.6 MB,
  written in about a second at level 19, against 22 CPU-seconds for compressing the whole tree at 19.
  This is why a file is tried at level 1 and stored unless it saves a thirty-second.
- **Mods ordered by name, then by the player's list** (2026-11-13; decisions.md, phase 11a then 12a).
  Mods were first mounted in the byte order of their names; since 12a the order is `mods.yaml`'s,
  name order standing only for the mods the list does not yet name.
- **`Find` replaced `Resolve`** (phase 11a; history.md entry 10). A file in a pack has no host
  path, so the file system answers with a mount index, time and size instead.
- **Durable writes** (phase 8a; history.md entry 10). Phase 3's `Write` renamed a partial file over
  the target with no fsync; 8a added the flush of the file before the rename and of the directory
  after it, through `Core::WriteFileDurably`.
- **The model cache moved onto the resource manager** (phase 6a; history.md entry 11). Models became a
  resource keyed on their virtual paths, their buffers retired through a queue that waits out the
  frames in flight, so a scene reload no longer idled the device. The resource manager's suite was
  run once under ThreadSanitizer, built by hand, since no preset has it.

## From game-integration

#### The extraction's slices 1 to 3, as planned (docs/game-integration.md, approved 2026-11-15)

Done: slices 1 to 3 (the public lifecycle and explicit composition, `Host::Session` and
`App::Game`; engine resources apart from sample packs, and presentation optional, `clang-cpu`;
generic AI, physics and scene apart from space adapters, neutral frame identity and save metadata).
Each is recorded in history.md, "Neutral SDK and Manifest integration" (H02 to H05, A01, W01, G01).
Slice 4, provisioning what Manifest's current stage needs, recurs and is now the "Provisioning a
capability" workflow in game-integration.md. The text as it stood:

#### Remaining gaps and extraction order

Slices 1–3 below are done ([history](neutral-sdk.md#neutral-sdk-and-manifest-integration)). What
remains of the boundary: the shell names two pieces of content by convention, each optional (the
default scene and the music), and several engine suites read the test content as test data. Slice 4
recurs with every Manifest stage.

Implement the boundary in reviewable slices:

1. Public genre-neutral lifecycle and explicit game composition; move sample code and
   registration out of the engine app library. A sample's executable selects its game explicitly;
   Flotsam may open that project but must not activate its economy by default.
2. Separate engine resources from sample packs, and make samples/editor/presentation
   optional. A CPU consumer must configure without ImGui, Vulkan/glslc or sample content.
3. Split generic AI/physics/scene facilities from optional space adapters. Resolve neutral
   spatial identity and game save metadata before Manifest binds durable state to them.
   Keep save/schema compatibility through explicit migrations rather than silent renames.
4. Provision the public capabilities required by Manifest's current stage, validate
   against its local SDK override, commit the engine revision, then update Manifest's
   immutable pin and matching development environment. Preserve dependency/ABI ownership.

Do not rewrite every subsystem at once or add speculative support for every genre.
Preserve current sample/render/movement tests during extraction. A small sample may
exercise gameplay, but must use the same public interfaces as an external game.

#### The opening note (docs/game-integration.md, 2026-11-15)

Approved direction, 2026-11-15. This document sets the boundary and the workflow. The
extraction it describes (H02–H05, A01, W01) is done and is in Manifest's SDK pin; the
[history](neutral-sdk.md#neutral-sdk-and-manifest-integration) records what each step built.

#### The space-trading example (docs/game-integration.md, 2026-11-18 to 2026-11-22)

"The space-trading example, with its damage and its repair in a dry dock (2026-11-18) to exercise a
berth's features, was retired on 2026-11-22, Manifest testing all space code."

#### The enforcement list as "completion" (docs/game-integration.md, to 2026-11-25)

The heading was "Enforcement and completion"; the `neutral_words` sentence was ungrammatical and
the engine "has no space module since the space extraction's slice 6". Rewritten as "Enforcement".

## From persistence

Material from architecture.md "Persistence" (lines 598-653) and history catalogue entry 25 that is
history rather than current fact, for docs/history/.

- **2026-11-18, the save game's version 2** (from architecture.md, Persistence). Version 1 of
  `Jettison.Save` held the space sample's galaxy and the system it showed as fields of the info
  (`galaxy`, `system`). Version 2 moved them into `SaveInfo::details`, key and value strings of the
  game's; its step was an edit of the record the save begins with (`BinaryDocument::EditFirstRecord`).
  The step was deleted on 2026-11-23 when formats began to change in place (`SaveGame::Migrations()`
  now holds no steps), so no step reads a version 1 save today.
- **2026-11-12, phase 8a: the first fixture** (from decisions.md, "Migrations are a chain of steps").
  `world-v1.save`, written by the phase 6d build, was kept as a fixture; the world's save migrated
  from version 1, which had no services' state. Fixtures and steps were deleted on 2026-11-23.
- **2026-11-12, phase 8a: durable writes cost nothing anyone waits on** (from decisions.md). Measured
  then: the orbit scene's world saved at 4 KB, written only when the player asks, so the two flushes
  were judged free.
- **2026-11-23: the space extraction's slice 7 steps deleted** (from the arch2 audit and
  decisions.md). Its migration steps for the docks' renamed types, and the reading-and-dropping of old
  `Dock.Transfer` and `Dock.Buggy` storages, went with every other step when formats began to change
  in place.
- **2026-11-24: durable directories and a world lock** (from architecture.md, Persistence; the
  history row "Durable directories and a world lock", R02 (5)). `Core::CreateDirectoriesDurably` and
  `Core::FileLock` were added for Manifest's recovery, and `WriteFileDurably` made its directory
  with them. Before, it made a missing directory without flushing its parent.

## From physics

Source: docs/architecture.md "Physics" (lines 267-416), rewritten as docs/physics.md. These items
were dropped from the feature doc as history or measurement narrative. Most are already recorded in
docs/history.md rows 7a, 7b, 7c and 12b; the history writer should check before adding.

- 2026-11-13 (row 12b), architecture.md: the cost of a rebuild across a Jolt version, measured with
  `SaveAsJoltVersion`. Four crates thrusting in a row into a block: 1.2 mm off in the first step,
  7.1 mm by the tenth, resting 4.6 mm from where they would have. The thrown-crate bay rebuilt at
  step 180 with three pairs touching: 2 nm out after a step, 57 µm two seconds on, the same pairs
  beginning and ending each step. A docked vehicle loses nothing (kinematic on its platform's rails;
  the clamps are presentation's).
- 2026-11-13 (row 12b), architecture.md: "Layout 3, before the version was saved, is read as
  5.6.0's." No longer true: the layout is 5 today and any other layout is refused (formats change
  in place until player testing). Record as history only.
- 2026-11-11 (row 7b), architecture.md: a ship let drift beside Highport stays within 5 mm of where
  it was for a minute in the station's frame (1.8 m in the star's frame); within a station's frame
  what is left of the planet's 8.6 m/s² is the tide's few ten-thousandths. Manifest's measurement.
- 2026-11-11 (rows 7a-7c), architecture.md: the physics suite's recorded runs were "every kind of
  body and shape" and "a vehicle flown through a platform's docking zone into its hub, picked",
  pinned in Debug, Release and under the sanitisers. Since the space extraction (2026-11-22/23) the
  engine's physics suite pins one run (`ReplayTests.cpp`, the bay); the docking-zone and pick runs
  live in Manifest (`source/game/space/tests/ContactTests.cpp`, `FlightTests.cpp`).
- architecture.md "Quiet steps": the kinematic sweep's grid rounding, "2 to 14 times a day by up to
  30 nm at the retired space sample's stations", is already in docs/history.md (line ~347).
- architecture.md: "When time compression is above 1x, piloting is suspended ... Compression is
  refused near platforms and other vehicles, by the game." That is Manifest's rule, not the
  engine's; not carried into physics.md.

## From rendering-effects

Material from docs/architecture.md ("Ambient occlusion", "Particles", "Colour grading", lines
1346-1531) that is measurement narrative or retired space-sample work, moved out of the feature doc.

- **2026-11-13, phase 10c, ambient occlusion on the post bench** (architecture.md, "Ambient
  occlusion"). On the test bench (`scene/post.yaml`) the debug view had the deck's corners at about
  half the open deck's access and the deepest, between the stacked crates, at a fifth; the shaded
  frame changed by little. In space the sky is dark and the sun is most of the light, so it shows
  most on a planet's light on a hull and in a station's bays out of the sun.
- **2026-11-13, phase 11c, the colour grade's cost** (architecture.md, "Colour grading"). A grade
  cost the output pass 0.7 ms on lavapipe at 960 × 540. Without a table the output pass grades
  nothing, so every earlier reference frame stayed the same to the byte.
- **2026-11-13, phase 10b, the retired space sample's thrusters** (architecture.md, "Particles").
  The space sample's shell lit a plume at each nozzle and a jet at each port from each vehicle's
  `Physics::Thrust` (`ShipThrusters`), its nozzles warming and cooling, for Manifest's graphical
  client to take up. Its glass was the ships' canopies and a buggy's windscreen.
- **2026-11-13, phase 10b, a transition's streaks** (architecture.md, "Particles"). A game's
  transition had streaks: an emitter kept ahead of the camera, sending them past it as the effect
  rose, then a blue-white flash on its way to black and back. The streaks' emitter had to be placed
  after the camera's rig moved the camera: the camera moved tens of kilometres a second with the
  system's bodies, and an emitter placed by the last frame's pose was left hundreds of metres
  behind, its streaks out of sight. That code was the space sample's and is not in the engine.

## From rendering

Material moved out of the rendering sections of docs/architecture.md (lines 1179-1345, 1532-1676,
1842-1895) and history catalogue entries 17-20 while writing docs/rendering.md (2026-11-25). Each
item keeps its original date where it had one.

#### The frame's budget, measured (phase 5e, 2026-11-11; architecture.md "The budget")

| Device | Frame | Budget | `orbit.yaml` | With 265 lights |
|---|---|---|---|---|
| RTX 5060 Ti | 1248 x 1358 (the compositor's tile) | 4 ms | 0.30 ms | 0.47 ms |
| lavapipe, 24 threads | 640 x 480 | 50 ms | 33 ms | 46 ms |

Note: the same section gave the RTX 5060 Ti frame as 1243 x 1338 in its post-chain paragraph; the
two sizes contradict each other and neither was re-measured.

- Lavapipe decodes BC7 in software at every texel fetch: the orbit scene's opaque pass took 469 ms
  in BC7 and 23 ms in RGBA8, so a CPU device is given RGBA8 (5e).
- The planet, drawn into two cascades whole, was most of the shadow pass until casters were culled
  exactly against each map's prism: 15.5 ms to 4.2 ms on lavapipe, most of the rest the four 2048²
  clears (5e).

#### The dressed station measured (phase 9d; architecture.md "The budget")

Ostra Highport dressed, 925 meshes and 104 floodlights (built by the space sample, since retired;
`orbit.yaml` is now its baked copy). On the RTX 5060 Ti: 0.32 ms from the orbit camera's 1.5 km,
0.53 ms from 1.1 km over the station and 0.66 ms from 320 m over its hub. On lavapipe it first
took 84 ms, the opaque pass 73 ms against 9c's 22 ms, its lights only 1.5 ms of it. Two software
costs were the rest. A material's 16x anisotropic filter, nine fetches a pixel in the triplanar
program at grazing angles: a CPU device's sampler now takes no anisotropy, which took the opaque
pass to 31 ms. And hundreds of sub-pixel triangles each shading a block of the lit program:
leaving out what is under a pixel across (`TooSmallToSee`, `kDetailPixels`) took it to 22 ms, and
the frame to 33 ms. Hardware still filters at 16x, and from 1.5 km at 1338 lines leaves out 26
meshes.

#### The transparent pass measured (phase 10b; architecture.md "The budget")

On its bench (`scene/effects.yaml`, two glass shapes, three emitters of 512 slots), against variants
without the glass and without the particles. RTX 5060 Ti at 1243 x 1338: 0.72 ms, glass 0.09 ms,
particles 0.03 ms. Lavapipe at 640 x 480: 25 to 27 ms, glass about 2 ms, particles about 4 ms,
mostly the smoke's large lit overlapping sprites. The particle pass is 0.002 ms on hardware and 0.2
to 0.4 ms on lavapipe with or without an emitter, its barriers' and timestamps' cost. The orbit
scene, whose parked ships' plumes have nothing alive, was 0.29 ms on hardware and 36 ms on
lavapipe.

#### The post chain measured (phase 10c; architecture.md "The budget")

Measured on the orbit scene, the transparent bench and `scene/post.yaml`, with ambient occlusion
and bloom both on and both off. RTX 5060 Ti at 1243 x 1338: orbit 0.64 ms against 0.42 ms (prepass
0.08, AO 0.08, bloom 0.06); transparent bench 1.32 against 0.77 ms; post bench 1.58 against
0.89 ms, AO 0.41 and 0.55 ms of those, dearest close up. Outlines 0.27 ms on the post bench, the
flood's five steps 0.22 ms of it. Lavapipe at 640 x 480, against 10b's commit (orbit 35 to 36.5 ms,
opaque 25.4 ms): both off 41.3 ms (opaque 26.5 ms for the stencil written and resolved, most of the
rest the seventeen new passes' barriers and timestamps); with bloom 44.2 ms; with AO too 53 ms,
over budget, the prepass 11.2 ms of it for the dressed station's 925 meshes. So a CPU device
starts with AO off. The transparent bench was 34.7 ms by default there, the post bench 31.6 ms,
34.8 ms with AO.

#### The cluster grid and range tuned (phase 5e, 2026-11-11; architecture.md "The grid")

The grid went from 16 x 9 x 24 to 32 x 18 x 48 against a station of 265 lights
(`scripts/make-light-budget-scene.py`), shading the opaque pass a fifth faster on hardware and on
lavapipe. The cluster range, 2 km, was kept because every light measured was within 200 m. The
shadow distance started at 400 m and was not varied (decisions.md has the grid's numbers).

#### The environment capture's effect (phase 10a; architecture.md "The environment")

With the bodies in the capture, Highport's dark hull plating went from about (12, 9, 11) of 255 to
(17, 21, 21) in the orbit scene's opening frame, the blue-green of Ostra's ground. A station in low
orbit turns a degree against its planet in fifteen seconds, which set the capture's re-bake
thresholds. The capture has no clouds, which would about double the planet's albedo.

#### How the frame was built (catalogue 17-20, architecture.md "The frame")

The phase labels the frame's steps carried: the pass list and HDR targets (phase 4), PBR, IBL, sky
and tonemap (5b), clustered lights (5c), sun shadows (5d), culling, instancing, LOD and the budget
(5e), atmospheres and rings (10a), transparency and particles (10b), the post chain, prepass
normals and object IDs (10c), `TooSmallToSee` (9d). The renderer interface `IRenderer.h` was
filled in 6a. Catalogue entry 20 said every scene program has four texture slots in set 1; the code
now has eight (`kMaterialTextureSlots`).

## From roadmap

Material removed from the roadmap when it was rewritten as open work only (current deliveries and
the backlog J01 to J29). The history writer places it; each item gives its date and source.

#### The current priority, as it closed (docs/roadmap.md, "Current priority", to 2026-11-25)

The status narrative of "Neutral SDK and Manifest integration", approved 2026-11-15. Every row it
names is already in history.md, "Neutral SDK and Manifest integration"; this is the roadmap's own
log of it from Manifest's Stages 1 to 5, kept verbatim. It ends at R09 (2026-11-25), Manifest's
Stage 5 closed on `13c1bec`. Two later engine commits are not in it: `6c4e9ca` (three gaps
Manifest's Stage 5 cross-review found: `SimWorld::ComponentNames`/`ServiceNames` returning
copies, `Net::Server::Closing`, `Platform::ResolvePath`) and `459af99` (a directory made with
the permissions asked for, from the start, `CreateDirectoriesDurably`), which Manifest pinned
(Manifest's history.md, Stage 5 cross-review round 5). The sentence "Retain the small space example
as a separate automated-test consumer" was stale (the space sample was retired 2026-11-22) and is
dropped, as is the closing paragraph on "discretionary expansion of the sample".

#### Current priority: neutral SDK and Manifest integration

Approved 2026-11-15. What is done, and what each Manifest task named of the engine, is in
[history](neutral-sdk.md#neutral-sdk-and-manifest-integration). Manifest's Stages 1 to 3 are closed
on the pin `30ea3f3`, Stage 3 (A02 to A07) with no engine gap. That stage's cross-review named
one, a service loaded with the commands a save queues, provisioned in
[history](neutral-sdk.md#neutral-sdk-and-manifest-integration). Its N01 (2026-11-20) settled the network
seam ([decided](../decisions.md)), and N02 is done in both (2026-11-20,
[history](neutral-sdk.md#neutral-sdk-and-manifest-integration)): the engine's `net`, and Manifest's
dedicated server and TUI client on it, on the pin `65846bb`. N03 is done in Manifest with no engine
edit (2026-11-20, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration)). Its W02 is done in
Manifest (2026-11-20), a planning task: its hosted step runs Jettison's movement systems at 1/60 s and
names one engine gap, an order between `dock` and `lanes`, provisioned ahead of W05 (2026-11-20,
[history](neutral-sdk.md#neutral-sdk-and-manifest-integration)). W03 is done in Manifest with no engine
edit (2026-11-20, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration)). W05 is done in
Manifest (2026-11-20) on the pin `377bd8d`, its ships travelling through the docks, the cruise and
the flight out of a station's reach, with two provisions here: `Space::ClearOf`, and a steered ship's
controls let go with its destination ([history](neutral-sdk.md#neutral-sdk-and-manifest-integration)).
W17 is done in Manifest (2026-11-20) on the pin `54fb871`, its mode cruising by acceleration,
manned or unmanned by a hull's occupants, Astralis on the power law, with one provision here, the
cruise by acceleration ([history](neutral-sdk.md#neutral-sdk-and-manifest-integration)). W16, new
onboarding, is done in Manifest with no engine edit (2026-11-20,
[history](neutral-sdk.md#neutral-sdk-and-manifest-integration)), and so is W15, the vertical slice
(2026-11-20, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration)), and so is W04, local
production, finite brokers and demand (2026-11-20, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration)),
and so is W06, station life support and resupply (2026-11-20,
[history](neutral-sdk.md#neutral-sdk-and-manifest-integration)), and so is W07, live and stale market
observations (2026-11-20, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration)), and so is W08,
company-funded cargo upgrades (2026-11-20, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration)). Its L26, the console's long
step, is done in Manifest (2026-11-20), the step taken a slice at a time and stoppable, with one
provision here, the rails placed once a body, which halved a cruising step
([history](neutral-sdk.md#neutral-sdk-and-manifest-integration)). Its W09, the complete trading fixture, is
done in Manifest with no engine edit (2026-11-20, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration)).
Its L27, the fast-forward over quiet steps it asked for after L26, is done (2026-11-20), provisioned
here ([history](neutral-sdk.md#neutral-sdk-and-manifest-integration)): Manifest's systems say when they
next run and its console and server pass over the steps between, Ore to Brine 2.4 s from 4 min 50 s.
Its W10, standing trade policies and NPC jobs, is done in Manifest (2026-11-21) with one provision
here, a stopgap for the way in to a pad, and its L28, a docked fleet's wait passed over, with no
engine edit ([history](neutral-sdk.md#neutral-sdk-and-manifest-integration)). Its W18, W11 and W19 are
done in Manifest with no engine edit. Its W21, fuel burn, asked for a cruise's coast phase, a share
of the time thrusting, provisioned (2026-11-22, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration));
and its slice 4 a station's reach measured where physics placed it (2026-11-22,
[history](neutral-sdk.md#neutral-sdk-and-manifest-integration)); W21 is done in Manifest (2026-11-22).
The station traffic lanes booked after it were measured and are parked ([carried
forward](../roadmap.md#the-backlog), 2026-11-22). The space extraction they waited
for is done (2026-11-23, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration),
[its plan](space-extraction.md)): the space setting is Manifest's, the space sample retired, and
Manifest pinned to `14fc23a` with every pin held. Formats change in place until player testing
([decided](../decisions.md)). Manifest's W12, W13 and W14 are done with no engine edit (2026-11-23,
[history](neutral-sdk.md#neutral-sdk-and-manifest-integration)): its Stage 4 is closed and cross-reviewed.
Its R01, the save and compatibility contract, is done (2026-11-24), a planning task with no engine
edit ([history](neutral-sdk.md#neutral-sdk-and-manifest-integration)); its R02, durable I/O and the world
lock, is done in Manifest (2026-11-24) on the pin `ea66afe`, with two provisions here, directories
made durably and a file lock ([history](neutral-sdk.md#neutral-sdk-and-manifest-integration)), and with
them the message several datagrams long its Stage 4 review booked, which needed a limit set and no
splitting of the engine's own. Its R03 and R04, the journal flushed every 30 s and its hourly snapshots, are done in Manifest with
no engine edit (2026-11-24, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration)), and so is
its R05, a clean stop, backups and restore (2026-11-24). Its R06, the runtime distribution contract, is done (2026-11-24), a planning task
([history](neutral-sdk.md#neutral-sdk-and-manifest-integration)): a standalone bundle for glibc 2.39 or
newer, with its TUI, and one engine gap, ImGui's backends out of `Devtools`, provisioned here before its R07
(2026-11-24, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration)). Its R07, the distribution
built, staged and run in Ubuntu 24.04 and Debian 13, is done in Manifest with no further engine edit
(2026-11-24, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration)), and so is its R08,
persistent reconnect and recovery (2026-11-24, [history](neutral-sdk.md#neutral-sdk-and-manifest-integration));
and so is its R09, the close of its Stage 5 (2026-11-25), with one provision here, a world listing the
names its save holds ([history](neutral-sdk.md#neutral-sdk-and-manifest-integration)): its Stage 5 is closed. Its later workstreams wait until the user has played the first loop for a day or two.
Follow [game integration](../game-integration.md) and inspect Manifest's current development-stage/gameplay requirements before each task.
Provision their reusable engine capabilities here; production rules and content stay
in Manifest. Retain the small space example as a separate automated-test consumer.

Each row carries the Manifest task it is ([delivery order](../../../manifest/docs/delivery-order.md))
and the Manifest stage it unblocks; work them in Manifest's order.

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Encryption | before Manifest hosts outside a trusted network | A key exchange in the hello with the server's public key pinned by the client, then every packet sealed, on the libsodium `net` already links ([D-081](../../../manifest/docs/decision-ledger.md#d-081)); a client with the wrong key is refused, and the suite and consumer run sealed. |
| Manifest capability integration | each of the above | Each required mechanic maps to a public API or a provisioned reusable gap, with independent engine and Manifest acceptance evidence; immutable SDK pin updated after engine validation. |

These deliveries precede discretionary expansion of the sample. The historical space
features below and in [decided](../decisions.md) describe existing coverage and possible extension work,
not permission to make space gameplay a neutral engine requirement. Completed phases
remain historical evidence, not evidence that this new boundary is already implemented.

#### The carried-forward framing (docs/roadmap.md, "Carried forward", to 2026-11-25)

#### Carried forward

Work that phases 1–13 moved on, in the order it is worth doing: by what is expensive to change later,
then by what the game's next work stands on, as the phases were ordered. Within a tier the first is
the most pressing. The decisions they waited on are settled, under [decided](../decisions.md). Phases
1–13's full record, and catalogue entries 1–31, are in [history](../history.md).

The old tiers ("First", "Next", "Then", "Last", "The samples' backlog") became tiers 1 to 5 of the
new backlog. The rows "Manifest integration, not sample expansion" and "Manifest capability
integration" (a process row, "each of the above") became the workflow in game-integration.md.

#### The way in to a pad: the stopgap and its measurement (docs/roadmap.md, 2026-11-21)

Done: the stopgap (`Docks::FlyInbound`, `FlyAbove`). The proper solution stays open as J09. The
full item as it stood, including the measurement and the survey of other games:

- **The way in to a pad, done properly** (the user's, 2026-11-21; entry 31). First in the tier:
  every NPC and autopilot trade in Manifest docks through it. Manifest's W10 zero-client run found a
  ship that came round its station fast and sideways overshooting the point above its pad, which is
  only `kAboveMargin` (10 m) outside what steering keeps it from: each lap avoidance commanded it
  out at up to 190 m/s and Arrive pulled it back, and it circled 30 to 55 m off the point at about
  40 m/s until its 300 s reservation lapsed. In a grid of 48 starts round Highport, 2 km and 4 km
  out, still or going across at 150 m/s, two never reached the point. Ship and station shared one
  frame throughout. A 100 m margin alone cured that case; a waypoint out along the normal, which
  steering passes at full speed, put a sample trader into Ostra Anchorage at 126 m/s. The stopgap
  (`Docks::FlyInbound`, `FlyAbove`): the ship stops at a point out along the normal as far beyond
  the point above as it stops in from its top speed, then flies straight down the normal from rest,
  so the 10 m margin stands; from 10 km Highport's shuttle is over its pad in 117 s, 111 s before.
  What other games do, from recollection and not checked against sources: EVE has no approach,
  docking at once within range; Freelancer and many older games fly to a ring or entry point and
  script the rest; X4 and Star Citizen's traffic control fly authored approach paths, holding point,
  approach point, pad, with avoidance relaxed against the station; Elite Dangerous gives a pad and a
  permission that lapses, limits speed near a station and flies its docking computer down a fixed
  approach; Space Engineers' general autopilot is known for circling and overshooting its
  waypoints, the failure here, which players work round with a slow precision mode or scripts. So
  the solution: an approach corridor flown without stopping, at a speed that falls with the
  distance left; the station not an obstacle on the corridor's last leg, as it is not on the way
  down (`byShape`); Arrive damping the velocity across its line rather than pursuing; and a
  reservation that lapses when a ship stops making progress rather than at a fixed time. Measured by
  ships flown in from every side and speed, one queued behind another on the next pad, and no
  arrival slower than the stopgap's.

#### Logging: the run's reports through Log.h (docs/roadmap.md, 2026-11-17)

Done: the run's reports (pass timings, the cluster check, the benchmark) went through `Log.h` on
2026-11-17. The `InitLogging` gap stays open as J10. The item as it stood:

- **Logging in the shell** (entry 3). The app shell does not call `InitLogging`, so its reports
  reach only the default stderr sink. The run's reports (pass timings, the cluster check, the
  benchmark) went through `Log.h` on 2026-11-17; the failure report, usage line and teardown stay on
  `std::cerr` ([coding standards](../coding-standards.md#errors)).

#### The dev tools' theme (docs/roadmap.md, 2026-11-17)

Done: the dev tools' style themed on 2026-11-17, on Catppuccin's four varieties with the spacing of a
Tailwind sheet, switched live by `dev.palette` and `dev.ui_scale`. The leftovers stay open as J26.
The measurement behind the dark-tone leftover: over the 264 channels of the four varieties, 209 land
exact, 33 are one step off and the most is four, all four-step cases at the dark end. The item as
it stood:

- **The dev tools' theme, and what it leaves** (ui/imgui): the style is themed 2026-11-17, on
  Catppuccin's four varieties with the spacing of a Tailwind sheet, switched live by `dev.palette`
  and `dev.ui_scale` ([architecture](../ui.md)). Not yet placed: the game
  UI's own stylesheet in the same palette, so the market screen and the tools read as one design,
  which is a RmlUi stylesheet over the VFS and no engine work; the palette as a player setting on the
  game's own screen, which wants the settings screen the sample does not have; a light-attachment
  swap noticed mid-run, which `ApplyThemeIfChanged` answers only when a setting changes; and the
  darkest tones exact on the palette, which wants the sRGB encoding done in the UI's own shader over
  a UNORM attachment — ImGui packs a vertex colour into 8-bit linear bytes, so `#1e1e2e` lands as
  `#1c1c2e` and `#11111b` as `#0d0d1c`; measured over the 264 channels of the four varieties, 209
  land exact, 33 are one step off and the most is four, all four-step cases at the dark end
  ([architecture](../ui.md)).

#### Station traffic lanes, parked (docs/roadmap.md, 2026-11-22)

Measured and parked, then handed to Manifest on 2026-11-25 (its lanes are Manifest's `space`, on
the engine's `transit` slots and portals). The full text went to Manifest's backlog with the other
space-game items (shipyard screen, cruising, gates, a body's look as data, the station dressing, a
nozzle's glow).

#### The stubs contract (docs/roadmap.md, "Stubs", phase 1)

Every stub is filled: `JETTISON_NOT_IMPLEMENTED` is used only in `core` and its test (checked
2026-11-25).

#### Stubs

Phase 1 wrote a stub for every module in the catalogue below; the modules under
`source/engine/` are the list. A stub is:

- the module's directory, `CMakeLists.txt` and its place in the dependency graph
- its public header(s), with the main types and functions declared and documented: what they
  are for, what owns what, and which phase fills them in
- an implementation that compiles, does nothing observable, and fails loudly where doing
  nothing would be wrong (`JETTISON_NOT_IMPLEMENTED()` from `core`)
- one doctest test that the module links and its stub behaves as documented

A stub does not invent behaviour the phase that fills it would have to undo. Where a stub holds
configuration it was constructed with (a fixed step, a seed, a mount list), that much is real and
tested; everything that would need an algorithm throws.

#### Empty pointers (docs/roadmap.md, to 2026-11-25)

#### System catalogue

In priority order. Each entry is the brief for its stub. *Library* is the chosen dependency.
*Stub* names what phase 1 declares.

Entries 1–31, for phases 1–13, are in [history](catalogue.md#system-catalogue-entries-131).

#### Open decisions

Each is settled before the work it blocks; settled ones are under [decided](../decisions.md).

None open.

## From runtime

- 2026-11-10 to 2026-11-14, from history/catalogue.md entries 3, 5 and 6 and architecture.md
  "Crashes": already recorded in the catalogue (core, profiling, platform filled in during phase 2;
  the CPU guard landed in phase 2, ahead of phase 7 when Jolt was first linked; a build with AVX2
  forced missing printed the message and exited before a static initialiser ran; Tracy's GPU zones
  moved to phase 4, one per declared pass; the crash handler built in 11d). Nothing new to add.
- 2026-11-17, from roadmap.md "Logging in the shell": the run's reports (pass timings, the cluster
  check, the benchmark) moved from std::cerr to Log.h. The failure report, the usage line and
  teardown stay on std::cerr. Belongs with J10's history once the shell calls InitLogging.

## From sample-game

#### The space-trading sample (docs/sample-game.md, retired 2026-11-22)

The space-trading sample (`Sample::SpaceGame`, `source/samples/space`, run by Jetsam) played the
first slices of Manifest's plan from phase 8b. It was retired on 2026-11-22 in the space extraction
(commit `d924475`; its code and content are in the tree at `3a533dc`, the commit before):
Manifest tests all space code, the engine cases only it held are in the engine's suites, and its
scenes, baked static, are the render test's content (`source/test/content`). An untracked
`source/samples/space` directory remains on disk holding only tool leftovers (`.claude/.cc-writes`).

The old text:

### Samples

Jettison keeps small games of its own to exercise engine capabilities in automated tests; none is
Manifest's production game or part of the reusable engine contract. The ground sample is the one
left. The space-trading sample (`Sample::SpaceGame`, `source/samples/space`, run by Jetsam),
which played the first slices of Manifest's plan from phase 8b, was retired on 2026-11-22 in the
[space extraction](space-extraction.md): Manifest tests all space code, the engine cases only
it held are in the engine's suites, and its scenes, baked static, are the render test's content
(`source/test/content`). Its code, content and what it played are in the history at `3a533dc`.

#### The ground sample's origin (docs/sample-game.md)

The ground sample was built as G01f (2026-11-18), the last sub-phase of the generic shell; history.md
records it under "Neutral SDK and Manifest integration".

## From scene

Material taken out of the scene and spatial-index text of docs/architecture.md and the history
catalogue entry 21 when docs/scene.md was written. Each item is history, not current behaviour.

- **Phase 6a (date in history.md's phase table), from architecture.md "Parked code".** Phase 1 parked the
  scene graph in `render`, where it loaded models through the renderer. Phase 6a took it out into
  `engine/scene`, naming no device and its models by virtual path, at scene file version 2.
- **Phase 6b, from history.md catalogue 21.** `SystemScene` built a star system's scene around a
  file or from the system alone, placed the bodies, and kept the traffic layer's proxies from the
  simulation's snapshots. Layers became an entity's own or its parent's, a hidden one neither drawn
  nor lighting the scene.
- **Phase 6c (2026-11-11), from SpatialIndex.h, Octree.h and decisions.md.** The octree was chosen
  over a sorted grid and a loose octree by measurement over a star system's traffic
  (`spatial/tests/SpatialBenchmark.cpp`; the grid comparison is at commit 64d5488). Leaves of 64
  measured best; 32 was within a tenth, 16 a third slower to update. Range results past 512 are
  radix sorted, since sorting a sensor sweep's 7,500 entities was four fifths of the query.
- **Phase 6d (2026-11-11), from history.md catalogue 21.** A jump between star systems
  (`JumpTransition`) replaced the shown scene behind a fade, reading the destination's files on
  the I/O worker and checking them before touching anything. `Sim::InSystem` named an entity's
  system; it became the neutral `Sim::InFrame`.
- **Phase 9b (2026-11-12), from architecture.md and decisions.md.** The spatial index became kept
  from the simulation, by the system `spatial`, as `Spatial::SystemIndices` (since renamed
  `FrameIndices`); `spatial` gained its dependency on `sim`.
- **2026-11-22, from architecture.md 219-225.** A star system's scene and the jump between two
  were the retired space sample's (`spacescene`), removed with it; they are Manifest's graphical
  client's to build.
- **2026-11-23, from architecture.md 1817 (audit arch4).** The scene's migration from version 2
  (a `celestial` record read as `body`) was deleted when formats began changing in place (commit
  14fc23a). Version 3 is the only version read.

## From scope

Material removed from the two documents as history. Each item gives its date and the document it
came from.

#### From docs/scope.md

- Undated (before 2026-11-10): scope.md replaced the old `TODO.md` list of typical engine building
  blocks; every item on that list was given a verdict.
- 2026-11-10: Windows dropped from the plan, the toolchain costing more than it returned.
- Phase 1 (2026-11-10): EnTT upgraded from v3.6.0 (2020) to a current release.
- 2026-11-11: screen-space reflections reconsidered and kept out: they need most of a G-buffer, and
  where a ray leaves the screen they fall back to the environment at a seam black space makes plain.
- 2026-11-11: screen-space ambient occlusion, and the stencil aspect with an object ID buffer, added
  to scope for phase 10c.
- 2026-11-13: glass (cockpit canopy, station windows) brought forward into phase 10b, with
  particles, in the transparent pass.
- 2026-11-15: the neutral-SDK direction approved. It superseded the earlier test of whether a
  feature would suit another space game: the test became whether a contract is reusable without
  Manifest, with game policy left to the consumer.
- 2026-11-18: low-level networking added to scope, from Manifest's N01 (which named the gaps); N02
  built it in `net`. 2026-11-20: ENet over UDP decided. The server/protocol/account split stayed
  Manifest's (its D-057).
- 2026-11-23: the space extraction. The catalogue's rows had been written for the space sample
  ("no humanoids", "space has no walkable surface"); the system map screen and Keplerian orbital
  mechanics (the extraction's slice 6) moved to Manifest.
- The catalogue's "Added: needs the list did not name" table, with the phase each was planned for:
  unit test framework (1; there had been one hand-rolled test executable), engine packaging (1),
  configuration and settings (2), events and messaging (3), the definition registry (3), debug draw
  (4), instancing and LOD (5), the editor Flotsam (ongoing), crash handling (11), localisation (11).
- Phase tags on rows, all now done: scripting in Wasm (11b), packs (11a), cinematics and colour
  grading (11c), clip files and TRS tracks (10d), reflections with planets in the capture (10a),
  particles and glass (10b), post-processing, SSAO and stencil (10c).

#### From docs/coding-standards.md

- Until the space sample's retirement (2026-11-23), the determinism rules named its economy's
  integer credits and crates, beside Manifest's Decimal prices, as an example of a numeric model
  the engine does not impose.

## From scripts

Material from architecture.md "Scripts" (lines 733-834) that is history rather than current fact,
moved out while writing docs/scripts.md (2026-11-25).

- **2026-11-13, the first use of script inputs** (architecture.md, "Scripts", "The calls"). The
  retired space sample's traders were the first to use a script's input: its smoke mod
  (`scripts/smoke.wasm`, definition handling `Shell.Traded` and answering `crowding`) weighed a
  trader's run by how crowded its shelf was, and counted the trades it heard under a key, as a
  version byte and the count. A decision asking for an input neither a trader nor a script answered
  gave the shell no traders, logged. The sample, the event and the mod are gone; the script suite's
  modules (`source/engine/script/tests/scripts`) are the examples now.
- **2026-11-13, the cost of a call** (architecture.md, "Scripts", "The cost"; Release build, the
  development machine). A call on a kept instance took 0.27 µs; an input call, with its fresh
  instance, 9 to 10 µs, nearly all of it WAMR's instance and environment. An input instance with a
  host heap took 41 µs, which is why an input's instance is made without one. A trader weighing its
  runs asked the smoke mod's script for 25 inputs in the first step, a quarter of a millisecond
  once. (The figures that justify WAMR's interpreter and the fresh instance stay in decisions.md.)

## From simulation

- 2026-11-25, from docs/architecture.md "Quiet steps" (lines 458-459): "The frame loop's `Stepper`
  steps as before" referred to the code before quiet steps (2026-11-20) and was dropped. "An
  unattended server calls `FastForward`" was not verifiable in the engine; the callers are
  Manifest's console and fixture tests, which simulation.md names instead.
- 2026-11-25, from docs/architecture.md "Simulation and presentation" (line 214): the reasons'
  example `Shell.Reason` was wrong (no such enumeration is registered); simulation.md names
  `Ai.Reason`, `Dock.Reason` and `Physics.Reason`.
- 2026-11-25, from docs/architecture.md "Frame and tick structure" (line 1685): "economy" in the
  simulation phase was wrong; the economy is Manifest's.

## From transit

- 2026-11-22, from docs/architecture.md "Transit": the `transit` module was built in slice 4 of
  the space extraction, in parts: 4a motion profiles (the cruise's profile made neutral), 4b
  passages (the cruise's passage as any game's), 4c portals (the lanes' trip as any game's), 4d
  slots and gated portals (the gates' clearance and queue as any game's). Manifest's cruise, lanes
  and gates were rebuilt on those parts with no pinned hash moved. Already summarised in
  history.md's "Space extraction" row; nothing new to add beyond these part names.

## From ui

Material taken out of docs/architecture.md ("UI", the dev tools' theme, lines 909-976) as
measurement narrative rather than current contract. Source: architecture.md as of 2026-11-25. It
overlaps docs/history/devtools-theme.md (the 2026-11-17 handoff), which holds the review findings
F5 and F6 these measurements answered.

#### The dev tools' theme, measured (2026-11-17)

- **The style's "applied" state cannot be keyed by the context's address.** Measured: a context
  made after the themed one reuses its address and starts stock, because `ImGuiStyle`'s constructor
  writes the stock padding, rounding and colours over whatever the last context left in the block.
  A context recreated at the reused address begins at padding 8 and rounding 0. A key of addresses
  would have answered "already themed" to a stock style. The suite checks the stock start.
- **The UI scale's band.** Measured: a `dev.ui_scale` of 0.01 gives `WindowMinSize` {0.8, 0.6},
  and `NewFrame` asserts "Invalid style setting!". The console's variable could reach it; the dev
  tools' slider could not. Hence the clamp to 0.25..4.
- **The default face.** It was assumed that the last face added is ImGui's default, and the whole
  UI drew in Hack. ImGui 1.93 draws with `Fonts[0]` unless `io.FontDefault` names another, so the
  UI face is now named.
- **A face read from memory has no name.** Only `AddFontFromFileTTF` derives one from the file.
  The console found no mono face at run time, while a test that loaded from a file found one. The
  loader now writes the face's file name itself.
- **The palette through ImGui's 8-bit packing.** Mocha's base `#1e1e2e` fed as its own bytes to the
  sRGB swapchain would store as about `#606076`, and its text `#cdd6f4` as `#e8ecfa` (the
  washed-out look of stock Catppuccin ImGui ports); hence the palette fed in linear light. Measured
  through `ColorConvertFloat4ToU32` and the encode, over the 264 channels of the 88 colours the four
  varieties give the 22 roles: 209 channels land on the palette byte, 33 one step away, 15 two, 2
  three and 5 four. Four is the bound, at the darkest: Mocha's crust `#11111b` lands as `#0d0d1c`,
  Latte's `#04a5e5` as `#00a5e5`. Two steps cost a panel ground: Mocha's base lands as `#1c1c2e`,
  Macchiato's `#24273a` as `#22263b`. One step reaches the mid tones of the lighter varieties
  (Macchiato's `#494d64` as `#494d63`, Frappé's `#51576d` as `#51566d`, Latte's text `#4c4f69` as
  `#4b4f69`). Mocha keeps its mid and light tones exact: from the orbit scene with the tools open,
  its text draws as `#cdd6f4` over 2 318 pixels and its secondary button as `#45475a` over 16 279;
  `dev.palette macchiato` typed into the console drew 2 318 pixels of `#cad3f5` and 16 279 of
  `#494d63` on the frame after, the packing's predicted step. `ThemeTests.cpp` asserts the
  landings by name.
- **Reference frames.** The theme moved the `docked` reference view of the time (since retired
  with the space sample): its flight HUD re-typeset in Roboto, 10 050 pixels differing within a
  960 x 528 box at the top-left, re-captured with `capture.sh --references --update docked`. The
  other views drew no dev tools and landed at nil pixels with a 2 % fuzz; `dock-sequence` read
  12 383 in one gate run, which was that view's own sensitivity to load (14 516 pixels in one of
  three runs under twelve busy loops, nil at idle, against a 518-pixel allowance), not the theme's.
  See build-and-gates.md, "Reference frames".

#### The game UI (2026-11-12)

- The first documents were the shell's market and saved-games screens over the sample game's first
  slice; the market's overlay was a few thousand vertices a frame. The market went with the space
  extraction; the saves screen (`assets/ui/saves.rml`) is the engine's own today.
