# Roadmap

The order is set by what is expensive to change later, then by what later work stands on. It
is not set by what looks good in a screenshot. Architectural decisions (layout, the
simulation split, coordinate precision, colour space, serialisation) come before the systems
that would otherwise bake in the wrong answer. "Done" means the gate covers it, with the
commit that did it in the history. Phases 0–10, and the catalogue entries for the systems they
built, are recorded in [history](history.md).

## Phases

| Phase | Work | Exit criterion |
|---|---|---|
| 0–10 | **Done** 2026-11-10 to 2026-11-13: plan, layout and scaffolding, core, runtime architecture, coordinates and render foundations, space rendering, star systems and galaxy, physics and flight, UI, audio and persistence, AI and docks, presentation polish. Recorded in [history](history.md). | Met. |
| 11 | **Scripting and shipping.** Sub-phases 11a–11d; catalogue entries 29 and 30. | A data mod and a script mod load from the user mount, and a packed build runs. |
| 11a | **Packs.** Done 2026-11-13. Catalogue entry 30. Pack files behind the VFS (`vfs/Pack.h`): a directory's files in one, each stored or one zstd frame with its checksum, the index checked whole as it is mounted, read by `pread` from a file held open; a mount of a regular file is a pack. `FileSystem::Find` (a file's mount, time and size, which the resource manager's hot reload stamps by) in place of `Resolve`, and `ReadLayers`, every mount's file at a path. zstd 1.5.7, one copy, which `ktx_read` is built against in place of its own ([decided](#decided)). `jettison_pack` and `jettison_add_pack`; the sample assets as `jettison_assets_pack`. Definitions in layers: a whole file replaces, a `.patch.yaml` beside it merges over the layers below (`YamlArchive::ReadLayered`: mappings key by key, a null removing, `!append`, `!merge=<field>`), each layer migrated on its own, and every change a mod makes logged ([files, packs and mods](architecture.md#files-packs-and-mods)). The shell's mounts (`App::MountAssets`): the base, the user's mods under `mods/` in the order of their names, the user's data directory; the assets' pack found beside the executable where there is no directory; the default scene and ImGui's fonts read through the mounts. The headless smoke test runs packed, with a data mod in its user's mods (`--packed`). | Met: the headless smoke test, both scenes, from `assets.pack` with validation on and a pack of the test's mod in its user directory, the shell saying it mounted the base and the mod as packs and that beer is patched by the mod, once a scene; in the app suite, a mod in the user's mods over the sample game's definitions packed patching beer's price and Highport's market by `!merge=good` and adding wine. In the vfs suite, a packed directory read as the directory does, every prefix of a pack and four kinds of damage refused as it is mounted, a damaged file caught as read, and a pack replaced on the disk read as mounted, under the sanitisers too. The sample assets, 136 files and 90.3 MB, pack in 1.2 s to 88.6 MB. |
| 11b | **Scripting.** Done 2026-11-13. Catalogue entry 29. WAMR 2.4.5 decided over wasmtime ([decided](#decided)): its fast interpreter, with instruction metering, the thread manager, bulk memory and reference types, software bounds checks, and no libc or WASI. Scripts as definitions (`defs/scripts`, `script/ScriptDefinition.h`), their modules read through the VFS from whichever mount has them; fuel a call by instruction count, and a watchdog's time limit that stops a script past it; a trap logged and survived. The host API generated from reflection (`script/Bindings.h`): the types `Reflect::Attribute::Script` marks laid out as wasm32's C ABI lays them out, an event copied into a handler's memory and a command read out of it with every pointer, bool and enumerator checked, and from the same layouts a C header and a Rust module, each asserting its sizes and offsets, written by the console's `script_bindings`; imports (`log`, `input`, `send:<Command>`) checked against a script's grants as it loads. Calls from events, as the shell takes them, whose handlers send commands into the queue, and from AI considerations, an input a script answers on a fresh instance each call (`ScriptHost::WithScripts`), which the traders weigh their runs with. Scripts in C or Rust (`jettison_add_script`; lld's wasm-ld and rustc in the devenv), the shell's in Rust. The smoke mod gains a Rust script and a patch of the trade decision ([scripts](architecture.md#scripts)). | Met: the headless smoke test, packed, loads the smoke mod's script from the pack in its user's mods once a scene ("Script 'smoke': scripts/smoke.wasm loaded, 1 handler(s), 1 input(s), granted log"), and the script, called from the traders' trade events, says what it heard, twice a scene ("smoke: heard trade 1: bought 9 crate(s) of copper at 61660 credits"), with no trap. In the app suite, the same mod from a user's directory answers 25 inputs as the traders choose in the first step and hears every trade they fill, and the shell's Rust bindings are checked as this build's. In the script suite, under the sanitisers too, the limits (fuel the same on every run, a call past 50 ms stopped, memory, stack, bounds), every refusal of a module or a definition, and events in and commands out and inputs, against the same module in C and in Rust. A handler call takes 0.27 µs and an input call 9 to 10 µs in Release. |
| 11c | **Cinematics and grading.** The cinematic camera and sequencer; the colour-grading LUT. | |
| 11d | **Crash handling and localisation.** A backtrace and the log tail written on a fatal signal; localisation string tables. | |

Phase 1 was the last step before large-scale work: every system now has a home, a namespace, a
target and a test, and phases 2–11 fill stubs in rather than invent structure.

Phases 5–11 are split into lettered sub-phases (phases 5's to 10's are in [history](history.md)), each a block of related work that touches the
same modules and documents, so it can be done in one session and the context cleared after it.
A sub-phase names the catalogue entries that are its brief; read those, not the whole catalogue.
A phase is done when its last sub-phase meets the phase's exit criterion, so a reference to
"phase 7" in the code or the documents still means the whole phase. A sub-phase with no exit
criterion yet gets one when it starts, as phase 11's rows will.

## Carried forward

Work that phases 1–10 moved on, and where it lands. Their full record, and catalogue entries 1–28 and
31, are in [history](history.md).

- **Logging in the shell** (entry 3). The app shell does not call `InitLogging`, and the renderer
  writes to `std::cerr`, because the smoke test reads that output; they move to `Log.h` with the
  smoke test's patterns.
- **A body's frame chosen for it** (entry 22, 7b): `Physics::LocalFrame` is set by the game, as the
  shell sets its ship's and its traffic's, and by lane travel as a ship comes out of a gate, into the
  frame of the station the gate keeps by (9a). Taking a ship into a station's frame as it closes and
  out as it leaves, by a radius about each body, is not yet placed: steering (9b) flies within a
  frame, and nothing in phase 9 leaves one, since every gate keeps station a few kilometres from its
  station, in its frame. The first traffic between a station and the open system lands it.
- **A change inside a component's fields** (entry 25, 8a): fields are found by name, so a field
  added to a component reads an old save with its default, and one renamed is a rename in the
  world's table; but a field removed is refused (the archive refuses a field its type does not
  save), and a field whose type changed misreads its length. Migrating either needs the old type in
  the step, and the world's steps have the document alone. A component's own chain, run by the
  world as it reads that component's storage, is not yet placed: the game's first change to a
  component after its saves matter is its first reader.
- **Jolt's state across a Jolt upgrade** (entry 25, 8a): physics' share of a save is Jolt's own
  binary state, which a step cannot edit as data. A Jolt upgrade that changes it needs a load that
  rebuilds the bodies from the world's components instead, losing the contact cache's warm start for
  a step. Not yet placed; it is the first thing to check when Jolt is next moved.
- **The UI's drawing, not yet placed** (entry 23, 8b): RmlUi's clip masks (an element with
  `overflow` clipped by a rounded border, or clipped while transformed), which want a stencil in the
  output pass's rendering, where the UI is drawn into the swapchain and the depth target's stencil
  (10c) is not attached; and its layers, filters and shaders (`filter`, `backdrop-filter`,
  `mask-image`, gradients drawn as shaders), which want render targets of their own, as the post
  chain's are (10c). Until then RmlUi's defaults draw nothing for them, and a screen uses none.
- **The UI, not yet placed** (entry 23, 8b): a stylesheet or document reloaded when its file
  changes (RmlUi keeps a stylesheet for the run); a document's images decoded on the I/O worker
  rather than as the document lays out, which is on the main thread and fine for a screen's icons;
  RmlUi's debugger in the dev tools.
- **The audio, not yet placed** (entry 24, 8c): music that crossfades between pieces and a playlist
  to choose them; Doppler, once something reports velocities to presentation; the ship's own sounds
  (its engines, the cockpit's) and occlusion by hulls; a sound file streamed from its mount rather
  than read whole, which waits on a streaming read in the VFS (entry 10; a pack's stored files could
  be read in ranges, its compressed ones would want zstd's streaming decoder). The voice bus has
  nothing on it yet.
- **The sample game's later slices** ([sample game](sample-game.md)): ManifestCore's order book,
  which the traders (9f) post to once they trade by orders, the economy's turns and the skill tree's
  effects, then scripting (11).
- **A gate flown through** (entry 26, 9a): the shell sends its ship along a lane wherever it is in
  the system, as the scene jumps, standing in for the game's rule, and a trader's tree sends its
  own once it is within 150 m of the gate's centre (9f). A gate that sends what flies through its
  ring, a sensor whose contact pushes `EnterLane`, for the pilot and the traders alike, is not yet
  placed; the game's rule for who may use a gate lands with it. The smoke sequence's jump sends the
  ship and does not wait the 40 s for it to come out; the AI and app suites cover the arrival.
- **Courses past moving bodies** (entry 26, 9a): a course is straight legs between bodies where they
  are when it is plotted, plotted again as a ship goes. Fine across a station's few kilometres and
  for estimating a crossing; a course across a system flown at real speeds (days, past a planet that
  moves) needs its bodies where they will be, and a way of crossing a system at that scale at all,
  which nothing in the engine has yet. Steering (9b) flies a course's waypoints in the ship's frame,
  so a course by a station moves with it; the system's scale is not yet placed.
- **Packs and mods, not yet placed** (entry 10, 11a): a list of the mods enabled and their order,
  which the shell takes from their names; a mod's own description (a name, a version, the engine
  version it was written for); a patch that removes an element from a list, rather than replacing
  the list; a pack hot-reloaded, which is read as it was mounted; and layering for anything but
  definitions, so a mod still replaces a scene, a system or a stylesheet whole.
- **Scripts, not yet placed** (entry 29, 11b): a script's own state in a save, which a handler that
  counts loses on a load (an input keeps none); a script reading the world, which today it sees only
  through the events it is given and an option's inputs, and which would come through snapshots; a
  script called from a behaviour tree's task, as an input is from a consideration; a script reloaded
  when its module changes; scripts compiled ahead of time; and the fuel a call used, which WAMR does
  not report, for the console.
- **The cinematic camera** (entry 18): 11c.
- **The crash handler hook** in `platform` (entry 6): 11d.
- **The planets' look in the environment's capture** (entry 28, 10a): the capture draws a body as
  a sphere of its mesh's colour, without its maps or clouds, which would about double Ostra's
  brightness in it. Not yet placed.
- **Air over a mesh in it** (entry 28, 10a): the haze is the planet's own surface's and the sky's,
  not a ship's or a station's within the air, which no sample scene has yet, nor a transparent
  draw's (10b); nor are the bodies' air and rings sorted where two overlap on the screen. Not yet
  placed: the first scene with a ship in the air brings it, for the opaque and the transparent draws
  alike.
- **A gas giant's bands** (entry 28, 10a): Corva is one colour under its air. Not yet placed.
- **The transparent pass, not yet placed** (entry 28, 10b): particles sorted within an emitter, on
  the GPU, which a smoky emitter seen through itself would want; soft particles where they meet a
  surface, which want the depth the opaque pass is still writing, so a node of their own after it;
  refraction through glass, from a copy of the frame behind it, which the opaque pass's resolved
  target would give were the transparent draws a node of their own after it; a glTF primitive's `BLEND` alpha mode, which draws opaque; a transparent
  surface's shadow, tinted, which the sun's depth-only cascades cannot hold; and an emitter
  started warm, as if it had been running, which a capture of a smoke column waits three seconds
  for instead. A ship's plumes show its throttle ahead alone, not its turns and strafes.
- **The post chain, not yet placed** (entry 28, 10c): a transparent part of an outlined shape (a
  canopy) is not in the depth prepass, so the shape's outline runs round the opaque parts alone; a
  pick read from the object IDs under the mouse, which the physics ray answers today; the game UI and
  the dev tools under a mask, since they are drawn in the output pass, where the stencil is not
  attached; ambient occlusion at half the frame's size, or kept from frame to frame, should a
  measured frame want it cheaper; and the colour-grading LUT (11c).
- **Animation, not yet placed** (entry 27, 10d): a glTF clip's events, which glTF has no place for
  (its extras, or a clip file beside the model); one clip blended into another, so a clip changed
  part-way, as a pad's clamps let go before they have closed, starts the new one from its first pose
  rather than from where the rig is; the clamps fitted to the hull's box rather than its shape, so
  on a hull drawn by its model they grip where its box's sides are; and rigs of the ships' own
  (landing gear, a cargo hatch), which the hull proxies do not have.
- **A destination prefetched** before it is shown (entry 21): the renderer asks for a model when it
  first draws it, so a jump holds its effect over the new system's first frames while they stream
  in, 0.2 to 0.4 s on lavapipe for the sample systems. Not yet placed.
- **A body's look as data** (entry 21): the system scene draws a body it builds as its kind is, from
  a table in code. A station's look is its layout's, dressed (9d); a planet's material, air and
  rings are a scene file's (10a), so Corva's rings are drawn in `scene/vesper.yaml` and not in the
  Vesper a jump builds from its system file. A look per planet, moon or kind, as a scene fragment a
  mod can shadow, is not yet placed.
- **A glTF model's collision mesh** (entry 22, 7b): a station built to a layout collides as its
  decks and piers (9c), as every station of the sample systems is, and one of no layout as the
  procedural shapes the scene draws it with (`app/src/Piloting.h`); a hull collides as the box of its
  definition's size, whether or not it names a model to be drawn with (9d). A model's triangles, read from its file on
  the I/O worker as its rig is, or a collision mesh beside it, are not yet placed, and a model on a
  station of no layout is reported and left out.
- **Physics, not yet placed** (entry 22): a shape offset in its body, and compound shapes; collision
  filtering beyond static against moving (the game's own layers and groups); Jolt's debug renderer
  drawn through debug draw; Jolt's job system on more than one thread, once a profile asks for it
  (deterministic either way); the capacities tuned against real traffic; overlap queries (every body
  in a shape) and casts that return every hit, which steering (9b) did without (below).
- **Steering, not yet placed** (entry 26, 9b): an obstacle is the sphere about its body that holds
  its shape, and only a destination kept clear by its hull (9c) flies inside one, by a shape cast
  along its way, which slows it and does not turn it: a course round the inside of a station, from
  one pad to another under a pier, is not plotted, and a docked ship's way to its pad is straight
  down from outside the sphere, which a layout is checked to leave clear. Steering is measured for a
  station's traffic, sixteen ships; a busier lane's, where every ship queries the index each step,
  is measured when a game has one.
- **The station dressing, not yet placed** (9d): the dressing is drawn and not collided with, so a
  ship flown off its pad's column passes through a floodlight pole or a display ship, which the
  column a layout is checked to keep clear never meets; a pad's look as data (its paint, its poles'
  spacing and its floodlights' aim are the shell's, by class, in code; a reserved pad's lamps blink,
  10c, by a material of the shell's choosing); the floodlights' shadows,
  which the clusters' lights do not cast. The light-budget scene (`scripts/make-light-budget-scene.py`)
  still adds its 265 lights where 5e's hand-built hub was, now inside the layout's hub, so it measures
  the grid's cost and no longer shows anything; the dressing's 104 are the station's lights now.
- **Deck traffic, not yet placed** (9e): a buggy drives to its ship's hatch on the deck under the
  hull, through whatever of the hull's proxy is lower than the buggy, as a ramp or a lift a hull
  lowers to its hatch would not; roads run only on the tops of decks and piers, so a pad on a side
  or an underside, which no road reaches, is refused at a station with bays; the roads' legs are
  not checked against each other, so two that cross between nodes let buggies pass through each
  other there, which neither sample station has; a buggy carries crates, not goods, the trade
  having said which; and a pad's buggies are its own, not a station's pool shared out by demand.
  A buggy is placed on its station's rails each step, which for a station's few dozen is nothing,
  and is measured when a game has hundreds.
- **Commands that carry their sender** (9c): the docks' commands carry a `Sim::Controller` and
  are refused from one that does not control the ship, and a trader sends its own with its AI
  controller (9f); the flight controls, a destination, a lane entered, a pick and the sample's trade
  do not yet, and are taken from anyone. Each gains one as the first command a second player could
  send for it, with networking.
- **Decisions, not yet placed** (entry 26, 9f): reactive nodes, which tick a condition above a
  running branch and halt what they leave, as a trader breaking off its run to flee would need, and
  with them a task's halt; parallel nodes; a tree that names another as a subtree, where the
  trader's tree repeats its three-node jump loop for each leg; a blackboard of the engine's, where a
  trader's run is its own component; a command that gives an entity a tree, with its sender, where
  the shell gives a trader its tree as it makes it; and a tree ticked less often than every step,
  which seven traders do not need. A trader's run that cannot be made waits half a minute and
  chooses again; what a trader does about a hull it cannot dock, a market that has gone, or the
  player in its way beyond steering clear is the game's.
- **Picking a paused world, and under the cursor every frame** (entry 22, 7c): a pick is answered by
  the next step, so a paused world answers when it resumes, and a hover highlight would send a
  command a frame. Both want a query presentation makes itself, against the bodies' shapes in a
  snapshot. Not yet placed.
- **Not yet placed** (entries 19 and 20): the sun's shadow distance and split blend tuned by
  measurement; hysteresis between levels of detail, and levels for model files; the planets' own
  shadows past the shadow distance (a station in eclipse), which would need an analytic test against
  the bodies; programs that hot-reload; cube maps and texture arrays in files; normal mapping in `mesh`, which
  waits for tangent-space vertex data (`math/Tangents.h` makes it; `planet` builds its frame from
  the sphere and does not need it); a glTF model's own
  metallic-roughness factors and textures; and a model file read once rather than twice, for its
  rig and for its geometry (6a).

## Stubs

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

## System catalogue

In priority order. Each entry is the brief for its stub. *Library* is the chosen dependency.
*Stub* names what phase 1 declares.

Entries 1–28 and 31, for phases 1–10, are in
[history](history.md#system-catalogue-entries-128-and-31). The numbers carry on here.

### 29. Scripting · phase 11 · `engine/script`

A Wasm host: module loading from mod mounts, fuel/time limits, a capability-scoped host API
generated from reflection, and calls into scripts from AI considerations and events.
*Library*: WAMR (WebAssembly Micro Runtime) 2.4.5, its fast interpreter ([decided](#decided)).
*Filled in* (11b): scripts as definitions (`defs/scripts`), their modules read through the VFS; fuel
by instruction metering and a watchdog's time limit; the host API's layouts, its C header and its
Rust module generated from the types `Reflect::Attribute::Script` marks, imports checked against
each script's grants as it loads; handlers of events that send commands, and inputs for a decision's
considerations. Scripts in C or Rust. See [scripts](architecture.md#scripts).

### 30. Shipping · phase 11

Pack files, crash handling (a backtrace and the log tail, written on a fatal signal), the colour-grading LUT, the cinematic camera and sequencer, localisation string tables.
*Library*: zstd 1.5.7, for pack files, shared with libktx.
*Filled in* (11a): pack files (`vfs/Pack.h`, `jettison_pack`, `jettison_add_pack`), mounted as a
directory is; mods in the user's data directory; definitions in layers, by patches. See
[files, packs and mods](architecture.md#files-packs-and-mods).

## Decided

Settled on 2026-11-10:

- **C++20.** The language standard stays where it is. `std::expected` and `std::print` are not
  available; see [coding standards](coding-standards.md#errors) for what is used instead.
- **Linux only**, on x86-64: Wayland for the desktop, X11 for the headless tests, one
  toolchain (Clang 21 from the devenv shell).
- **spdlog for logging** (catalogue entry 3).
- **Clustered forward rendering.** Lights are assigned to view-space clusters in compute, and
  every surface is shaded once in its own material program. There is no G-buffer; SSAO, when it
  came (10c), reads the depth prepass's depth and normals. Phases 4, 5
  and 10 are planned around it (catalogue entries 17, 19 and 28), and
  [architecture](architecture.md#rendering-clustered-forward) has the frame and the rules it sets.
- **KTX-Software stays on v4.4.2** (2026-11-11, phase 5a). Phase 5 was to move to v5 if it was out
  of release candidate; its newest tag is still v5.0.0-rc2. v4.4.2 reads every file the engine
  needs (KTX 2, Basis Universal ETC1S and UASTC, zstd supercompression). Revisit when v5 is
  released.
- **Ambient occlusion is off on a CPU device by default** (2026-11-13, phase 10c). It runs the depth
  prepass, which now shades two multisampled targets as well as laying down the depth: on lavapipe
  at 640 × 480 the dressed orbit scene's prepass took 11.2 ms and its frame 53 ms, against a budget
  of 50 ms and 44.2 ms without it. Hardware has it on, at 0.16 ms with the prepass from the orbit
  scene's 1.5 km on the RTX 5060 Ti. The smoke test and the reference views therefore draw without
  it, but for the post chain's bench, whose view asks for it.
- **A pad's clamps are presentation's, and sound as a clip's events** (2026-11-13, phase 10d). The
  docks clamp a ship as a kinematic body the moment it is within reach (9c), and nothing in the
  simulation waits on a clamp; the dressing draws its pad's clamps closing over the next two seconds
  from the snapshots, which show each ship's `Docking` and `Hull` for them, and opening as it is let
  go, its jaws clear of the hull 0.3 s after it starts to lift. The docking sounds moved from the
  docks' `Landed` and `Left`, which the shell heard for the player's ship alone, from the station, to
  the clip's `clamp.closed` and `clamp.released`, heard from the pad for every ship: the jaws are
  heard biting as they are seen to, and a trader docking beside the player is heard too.
- **The cluster grid is 32 × 18 × 48** (2026-11-11, phase 5e), up from 16 × 9 × 24, with the
  index list at 32 entries a cluster on average and the cluster range at 2 km. Against 265 lights on
  the orbit scene's station, the opaque pass fell from 0.43 ms to 0.34 ms on an RTX 5060 Ti and from
  47 ms to 36 ms on lavapipe, with the assignment no dearer; 16 × 9 × 48 and 32 × 18 × 24 each
  gained about half that. The list's busiest frame, from 20 m, used 16 entries a cluster. The cluster
  range was not varied: every light measured was within 200 m.
- **The depth prepass is kept, and off** (2026-11-11, phase 5e). Measured on `orbit.yaml`, it
  cost more than it saved on both devices: 0.023 ms for 0.016 ms of the opaque pass on hardware,
  3.4 ms for 0.5 ms on lavapipe, where front-to-back batches already let the depth test reject what
  is behind. It is the place ambient occlusion's normals and the outlines' object IDs are written
  (10c), which turn it on.
- **A CPU device gets RGBA8 textures, not BC7** (2026-11-11, phase 5e). Lavapipe offers BC7 and
  decodes it in software at every fetch: the orbit scene's opaque pass took 469 ms at 640 × 480 with
  the hull plating in BC7 and 23 ms in RGBA8. The gate's smoke test therefore uploads RGBA8;
  transcoding to BC7 is covered by the render suite without a device, and uploading it on hardware.
- **Stencil and an object ID buffer, both** (2026-11-11, for phase 10c). The stencil, on the depth
  target as `D32_SFLOAT_S8_UINT` (a float depth, which reversed-Z needs; `D24_UNORM_S8_UINT` would
  throw its precision away), is the cheap mask: the hardware tests it before shading, for crisp
  outlines and for keeping post-processing off the cockpit and the HUD. It costs about 8 MB at
  1080p with 4× MSAA, barriers over both aspects, and a sample-zero resolve for the post passes.
  The ID buffer, written by the depth prepass rather than by every material program, is the
  flexible one: any pass can read it, and wide, soft target highlights are drawn from it. Its cost
  is the prepass running whenever outlines are on, 0.02 ms on hardware as measured in 5e.
- **Screen-space reflections stay out** (2026-11-11, reconsidered with SSAO and bloom brought in).
  SSAO needs the prepass's depth and normals; SSR would also need each pixel's roughness and
  metalness, and a colour of the scene to march through, which is most of the G-buffer clustered
  forward was chosen to do without. What it adds is reflection of what is on screen, and its
  failure, where the reflected ray leaves the screen or goes behind something, falls back to the
  environment cube: against black space that seam is plain to see. The reflection actually missing
  from a hull is the planet, which is not in the environment's capture; adding the planets to it
  (10a) is stable and right off screen too.
- **The spatial index is an octree** (2026-11-11, phase 6c), not a sorted grid, and not loose. Both
  were measured over a system's traffic (`spatial/tests/SpatialBenchmark.cpp`: 60% of ships within
  60 km of eight stations, 30% on the lanes between them at up to 300 km/s, the rest scattered),
  stepped at 60 Hz, on a Ryzen 9 3900X in Release. At 100,000 ships the octree (leaves of 64) took
  0.74 ms a step to update every ship, 1.4 µs for a 5 km avoidance query, 4.6 µs for eight nearest
  neighbours and 2.4 µs for the nearest ship to a point in open space; the best grid for each, its
  cells from 1 km to 1,000 km, took 1.1 ms, 2.9 µs, 36 µs and 64 µs. A grid finds a neighbour by
  growing its search through cells, which in open space are almost all empty, and pays for cells
  small enough for avoidance in updates (4.4 ms at 10 km). It was ahead on the 500 km sensor sweep,
  43 µs to 52 µs, and, by a tenth of a microsecond, on avoidance at 10,000 ships and fewer. Loose
  cubes (1.5 and 2 times the cube) made every small query 2.5 to 4 times slower, since they overlap,
  and the update no cheaper at 60 Hz; at steps of a second they saved a quarter of it. Range results
  past 512 are radix sorted: sorting a sweep's 7,500 entities was four fifths of the query.
- **A jump reads its files on the I/O worker and parses them on the main thread** (2026-11-11,
  phase 6d). The archives read EnTT's meta context, and a type registered on the main thread while
  the worker resolves another inserts into the table it is reading; nothing in it guards against
  that, and registration is lazy throughout the engine (every reader registers its types on first
  use). A system file and a scene file are a few kilobytes of YAML; the megabytes a destination
  needs, its models and textures, decode on the worker as they always have, and touch no reflection.
- **A jump point is the end of one lane** (2026-11-11, phase 6d), so a gate says where it leads, and
  flying through it needs no choice of destination. A system with two ways out has two gates.
- **Physics is simulation state, a Jolt world per star system, on one thread** (2026-11-11, phase
  7a; a world per frame of each system since 7b, below). Jolt's state is saved inside the world's save, as a service of the world, rather than
  rebuilt from components on a load: the contact cache each step warm-starts from is Jolt's alone,
  and a world rebuilt without it steps on differently from the world saved. A world per system
  rather than one with a filter per system, since every system's coordinates start at its own
  primary and their bodies would share one broad phase; 0.7 MB each at the default capacities.
  Body identifiers are chosen by the engine, the lowest free index with a saved sequence number,
  because Jolt's free list is history a save does not hold and its results depend on them. Jolt's
  job system is single-threaded, as the threading rules have the simulation.
- **Physics runs in frames, a Jolt world per frame of each star system** (2026-11-11, phase 7b),
  revising 7a's world per system. A frame is the system's own or the local frame of a body on rails,
  a station or a planet, translating with it; Jolt's positions are metres from the body and its
  velocities relative to its motion. In the star's frame, a ship flown at a station moving at
  30 km/s about the star (500 m a step) could not be swept against it: Jolt sweeps a fast body by
  their relative motion, but finds what to sweep against along the body's own absolute motion
  (`PhysicsSystem::JobFindCCDContacts` casts the broadphase with it), so a ball fired at 240 m/s at a
  panel beside it passed through, swept or not. Its broadphase bounds are floats, 8 km apart at
  150 million kilometres, and a float velocity at 30 km/s is 4 mm/s between values: a ship let drift
  beside the station for a minute moved 1.8 m in the star's frame, and 5 mm in the station's. The
  frame's change of speed between steps is taken off its dynamic bodies, which telescopes, so the
  rounding of the difference of two positions a hundred and fifty million kilometres out does not
  accumulate. One frame per system for its busiest place would not do: two stations about two
  planets are 30 km/s apart. Bodies in different frames do not meet, and a body is put in a frame by
  the game (carried forward).
- **Gravity is physics', from the star system's bodies** (2026-11-11, phase 7b). Orbits stay on
  rails; dynamic bodies are pulled by every body with a gravitational parameter, as point masses,
  evaluated as each step begins. Without it a ship beside a station on its orbit falls away from it at
  the planet's 8.6 m/s², 15 km in a minute, and an assist holds it there only by thrusting against
  the planet all the while.
- **Contacts are the simulation's, from Jolt's contact cache** (2026-11-11, phase 7c). The event
  queue is presentation's, and nothing in the simulation reads it back, while what a contact means
  (a ship docked, a hull damaged) is decided in the step. So the physics world keeps them: what the
  last step began and ended and what touches now, saved with the world, so a system ordered before
  `physics` reads after a load what it read before the save. Whether two bodies touch is asked of
  Jolt's cache once the step is over (`PhysicsSystem::WereBodiesInContact`) rather than counted from
  its callbacks: Jolt adds and removes a contact per pair of sub-shapes (a hull across two of a
  mesh's triangles is two), reports a continuous body's swept contact without caching it until the
  next step, and drops a sleeping body's contacts though it lies where it was. A contact added in the
  step counts, and a pair that both sleep still touches. A sensor is kinematic to Jolt and never
  sleeps, since a static sensor sees only bodies that are awake, and a ship come to rest in a docking
  zone would leave it as it slept.
- **Picking is a command, answered by an event** (2026-11-11, phase 7c). Presentation reads
  snapshots and events and never the simulation's state, which keeps holding once the simulation
  leaves the main thread; so a pick is `Physics::Pick`, applied as the next step begins and answered
  among its events, a frame later, which a click does not notice. The ray is drawn from what
  presentation shows, half a step behind the simulation on average, and a station's frame moves
  500 m a step about the star, so the pick carries the time it was drawn at and each frame is placed
  there for the cast: without it a click on the station misses by the distance between the two. A
  pick is in a recording like any command, and a replay raises its answer again. The cost, a paused
  world answering when it resumes, is carried forward.
- **Migrations are a chain of steps over a document** (2026-11-12, phase 8a), one step a version,
  written once and never changed, rather than readers that know every old layout: a step reads the
  data its version wrote and nothing else, so a format's history is a list of small edits, each
  tested by the fixture of its version. A binary step edits the archive's table of names and its
  payload without types, which serves a rename and an addition at the end; one that needs the types
  keeps the old ones registered under names of their own and reads with them. Every part of a save
  is versioned on its own (the save game, the world, each of the game's sections), so a change to one
  is a step in that one's chain. Saves written by earlier versions are kept as fixtures and never
  regenerated; `world-v1.save` was written by the phase 6d build.
- **Definitions migrate** (2026-11-12, phase 8a, the decision entry 13 left open). A definition is
  authored content that mods ship, and a mod's author is not there when the game changes a
  definition's format; refusing every file of an older version would break every mod at each change.
  A type's YAML migrations are run on its files as they load, and cost nothing for a file that is
  current. Saves hold no definitions, only their names, so a definition's format can change without
  touching a save.
- **Saves and settings are written durably** (2026-11-12, phase 8a, the fsync entry 10 left open).
  A whole-file write flushes the partial file before the rename and the directory after it: without
  the first, a power cut after the rename can leave an empty file where the save was, since the
  rename can reach the disk before the data; without the second, the rename itself may not. Saves and
  settings are small (4 KB for the orbit scene's world) and written when the player asks, so the two
  flushes cost nothing anyone waits on.
- **A joystick is named by its device** (2026-11-12, phase 8a). A binding's joystick is a number; one
  the player has bound is named after the GUID of the device that answered to it then, and from then
  on is that device wherever it is plugged in, and nothing while it is not. Numbers no name claims
  are the unclaimed devices in slot order, which is GLFW's slots when nothing is named, so defaults
  and a player who never rebinds see no change. Two devices of one model share a GUID, and a name
  takes the first of them free; telling them apart needs the device's path, which GLFW does not give.
- **The UI draws into an overlay the renderer reads** (2026-11-12, phase 8b), rather than a
  Vulkan render interface for RmlUi inside `render`, or RmlUi's own Vulkan backend. The overlay is a
  plain draw list in render's public headers (vertices, indices, draws with a translation, a scissor
  and a transform, and textures by identifier), so `ui` depends on `render` for its CPU side, RmlUi
  stays private to `ui`, Vulkan to `render`, and the UI's suite reads what a document drew without a
  device. RmlUi's backend sample makes its own allocator, swapchain and pipelines; the overlay is
  drawn in the output pass with the renderer's pipeline cache, VMA and frame slots. RmlUi's geometry
  is copied into the overlay each frame, as ImGui's is: a few thousand vertices for a screen. Colours
  are multiplied and filtered as encoded, as a browser does, and decoded once before an sRGB
  swapchain writes them, so blending is in linear light; a browser blends encoded values, which
  leaves the half-covered edge of light text on dark a little brighter here.
- **Data models read reflected types in place, from snapshots** (2026-11-12, phase 8b). A screen
  binds any type registered with Reflect, read through the field addresses the registration now
  records, with no code per type: the reflection the archives, the console and the definitions use
  is the UI's too. What a screen reads is presentation's copy of what a snapshot carries, never the
  world, which keeps holding once the simulation leaves the main thread; a world shows a component
  to presentation by name (`ShowInSnapshots`), and its copy is taken with the transforms at each
  capture. A screen's data is built from a snapshot when a step brings a new one and the model told
  only when what it shows has changed, so a still screen costs a comparison a step.
- **The sample game is Manifest's plan** (2026-11-12, phase 8b; the project was the Civilisation
  Simulation until it was renamed the same day). The shell plays the game's part from the plan in
  `~/projects/manifest`, the local copy over the GitHub one, slice by slice as the engine systems that each needs arrive
  ([sample game](sample-game.md)). Its rules are the shell's, standing in for the game's, and none
  of them is engine code.
- **The GPU drawn with is the one with the monitor** (2026-11-12). A machine may have a second card
  kept for compute with no monitor on it, and on Wayland that card still says it can present,
  through the compositor's copy across the bus; taking the first device Vulkan lists that has the
  features, as the renderer did, could draw a game on it. So a device is ruled out for what it cannot
  do, each reason logged, and of the rest the one whose DRM card has a connector reading `connected`
  (`VK_EXT_physical_device_drm`, then sysfs) is chosen, then by type, then in Vulkan's order, which
  Mesa's device-select layer puts the display's GPU first in. On the development machine, a 5060 Ti
  with the monitor and a 5070 Ti for inference, the 5060 Ti is chosen on Wayland, and lavapipe under
  Xvfb, where neither card can present. A laptop's monitor on its integrated GPU takes the integrated
  GPU, as the device-select layer would; `--gpu` takes the discrete one.
- **x86-64 with AVX2 is the minimum CPU**: Intel Haswell (2013) or AMD Excavator (2015) and
  later. Jolt adds its AVX2 instruction-set flags publicly, so they reach every target that links
  it, and the engine accepts that rather than building Jolt for an older instruction set
  (catalogue entries 6 and 22).

- **The simulation's jump is the engine's, in `ai`** (2026-11-12, phase 9a). What sends a ship along
  a lane (a pilot through a gate, a trader's route) is the game's; carrying it is the engine's
  (`Ai::LaneTravel`): the command, the component a save holds, and the system that brings it out,
  after physics, so physics removes its body the step it enters and makes it again the step after it
  arrives. A transit is whole steps, counted from the clock's step rather than compared in seconds,
  so it is the same every run however its seconds round. `ai` depends on `physics` for it, as 9b's
  steering does.
- **A ship comes out of a gate at rest, in its station's frame, facing the planet** (2026-11-12,
  phase 9a). A ship's speed carried through a lane would send one that went into a gate at speed out
  of the far one at a station a few kilometres ahead of it. So a ship arrives still relative to the
  station the gate keeps by, in that station's frame, where it can meet and dock with the station
  without changing frames, turned the way the gate faces: at the planet it orbits, or else the
  nearest planet, or else the nearest station (`Celestial::JumpPointFacing`). The gate's ring is
  drawn facing the same way. A gate of no station is its own frame.
- **Gates keep station by their stations** (2026-11-12, phase 9a). The sample's gates were hundreds
  of millions of kilometres from any station, days at a ship's 150 m/s. Each now orbits its station
  in the station's plane, at its phase and with its period (given, since a station has no gravity to
  derive one), so it stays straight out from the planet beyond the station: Halcyon Gate and Vesper
  Gate 5 km out, half a minute's flight, and Vesper Deep Gate 6 km out at 30° ahead along the orbit,
  so a ship coming out of either sees the planet with the station before it. In a station's frame,
  which does not turn, a gate moves at the orbit's rate times its distance, 1.1 m/s at Corva
  Refinery, which a ship at rest drifts from. Marrow, with no station, has its gate three of
  Ashfall's radii out, where the world is 40° across.
- **A route is over jump points, with the crossings the caller's** (2026-11-12, phase 9a). A graph of
  systems joined by lanes would make a route of two short jumps across a system whose gates are a
  long way apart cheaper than one long jump. Each jump point is a node instead, its lane an edge at
  the transit time and every other gate of its system an edge at what crossing costs, which the
  galaxy cannot say (it has no positions) and the caller can: `CrossingByCourse` makes it from a
  course through the system's waypoints at a speed.
- **Steering is velocities, which flight assist holds** (2026-11-12, phase 9b). The stub had each
  behaviour return an acceleration to be summed and clamped by the flight model. But assist already
  takes a velocity and reaches it at the thrusters' limits, axis by axis, so a behaviour that is a
  velocity is what assist flies, and the flight model, not steering, is what knows how fast the ship
  can change it. Arrival and the limit before an obstacle brake at half the weakest thruster, which
  leaves the rest for the turn and for a dodge, and the last metres of each are at the distance a
  second: √(2ad) alone, its slope unbounded at the end, was overshot a step at a time.
- **An obstacle is the sphere that holds its shape, and the guarantee is the braking** (2026-11-12,
  phase 9b). Avoidance finds what is near through the spatial index, not Jolt: the index answers for
  a whole system in identifier order, and a sphere's closest approach is arithmetic, where a cast a
  ship each step is a query into every frame. The turn aside from the first threat takes a ship
  round; what keeps it off any hull is that it never closes on an obstacle faster than it can stop
  before the clearance, which holds whatever the turn does. Twelve ships converging on a station kept
  31.8 m between hulls and 25 m from its sphere.
- **The spatial index is made from the components each step** (2026-11-12, phase 9b). It is not state:
  a save does not hold it, and a world loaded from one has it from its next step. Kept from the
  simulation, by the system `spatial` as the step's systems begin, it is in `spatial`, which now
  depends on `sim` (a sibling edge in the World tier), so that a sensor or a game system reads it as
  steering does. Steering orders itself after it and before `flight`, through the scheduler's new
  `before`: physics cannot name steering, which is above it.
- **Docking is the engine's, as pads a station gives** (2026-11-12, for 9c; the question entry 22
  left to 9c's behaviour trees). Stations are to be large enough to land a dozen of the largest
  transports, each on a pad of its own, and the game is to be multiplayer in time, so who holds a
  pad is authoritative state that every player sees the same: simulation state, saved, given by
  commands in their order, in a module of its own (`dock`, entry 31), before the traders that
  would otherwise be written against the shell's one port.
- **Hulls and pads at the middle of the scales weighed** (2026-11-12, for 9c). A transport up to
  60 m, the largest transport 140 m and the display ships 300 m. Twice that was chosen first and
  set back the same day: its twelve-pad station was three kilometres across, past the cluster range
  (2 km, entry 19), with the gates to move out and a buggy's trip from bay to pad minutes long. At
  this scale a twelve-pad station, four piers of three, is about a kilometre and a half across,
  within the cluster range from its centre, and the gates at 5 and 6 km stay beyond its piers.
- **A ship comes down onto its pad from outside its station's sphere** (2026-11-12, phase 9c).
  Steering already takes a ship round a sphere, so a ship flown to a pad is flown first to the point
  above it, outside the station's sphere along the pad's normal, round the sphere by a course
  (`Ai::Waypoints`); and only then, inside the sphere, kept clear by its hull, straight down. A
  course over the station's own hull is not plotted: a layout is checked as it is defined to leave
  the column over every pad clear, out to the sphere, and the shape cast along the way is what
  stops a ship short of anything there regardless. A shuttle 10 km out on Highport's far side is
  over its pad in 111 s and clamped 24 s later.
- **A docked ship is a kinematic body on its station's rails** (2026-11-12, phase 9c), rather
  than a dynamic one held by its assist or a constraint to the deck. On rails it is where its pad
  is at every step, exactly, and is placed the same in every build; it meets nothing kinematic, so a
  station's deck and the ships parked on it make no contacts; and what holds it is its
  `Physics::OnRails`, saved like any component. Let go, it is dynamic again where it lay, at rest
  in the station's frame. The one contact undocking makes is the hull on its pad as it is let go.
- **Saves from before the docks are refused** (2026-11-12, phase 9c). The shell's world held its
  docking port (`Shell.DockingPort`), which the docks replace, and a world refuses a component it
  does not register; its section held the port too. The section is version 2, without it, and a
  save of 9b's is refused by name rather than loaded into a world with no pads. The engine's own
  formats are unchanged in layout: the world's fixtures load as they did.
- **Cargo is carried in the simulation** (2026-11-12, for 9e). The buggies that carry a trade's
  crates between a station's bays and a ship's hatch decide when the ship can leave, so they are
  simulation state, deterministic and saved, rather than presentation's show of a trade already
  done; every player sees the same buggy at the same place.
- **A display pad's ship is presentation's** (2026-11-12, phase 9d). No ship is given a display pad
  (scope), and the ship it shows never moves, trades or docks, so a body there would be state every
  save carries for nothing. The layout names the hull it shows (`Dock::PadLayout::ship`), checked to
  be on a display pad alone, and the dressing draws that hull's proxy on it; the docks still make
  the pad an entity, shown on display, and refuse it to `Park`.
- **The dressing reads the pads from the snapshots** (2026-11-12, phase 9d). A pad's lamp shows its
  state, which is the simulation's, so the docks show `Dock::Pad` in every snapshot
  (`Docks::Attach`) and the lamps are turned from the latest one, as the market screen is from its
  market; the shell finds a station's simulation entity from its celestial body only when its pads
  are not yet among them, as when a world is loaded over the one it was found in.
- **A bay is the middle of its door's sill, on the deck** (2026-11-12, phase 9d), rather than the
  middle of its door: a buggy drives through it there, so the road graph of 9e has its nodes on the
  deck the buggies run on. Highport's and Corva's wall doors moved down onto their piers' decks.
- **A draw under a pixel across is not drawn** (2026-11-12, phase 9d). The view leaves out a draw
  whose breadth (its model's radius times its transform's middle scale) covers less than a pixel at
  its nearest point, as it leaves out one behind a horizon (`Render::TooSmallToSee`). The middle
  scale rather than the largest, so a long, thin stroke of a pad's paint goes when it is a sliver,
  and rather than the smallest, so the thin plate it is painted on stays. What is left out draws
  nothing a viewer could tell from its absence, and on lavapipe each of its triangles shaded a block
  of the lit program: from 1.5 km at 640 × 480 it leaves out 529 of Highport's 925 meshes and takes
  the opaque pass from 31 ms to 22 ms. Shadow casters are not culled by it; a skinned draw never is.
- **A CPU device filters without anisotropy** (2026-11-12, phase 9d), as it is given RGBA8 rather
  than BC7 (5e): it takes each tap in software, and at 16x on the display wing's round hulls, seen
  at grazing angles through the triplanar program's nine fetches a pixel, the orbit scene's opaque
  pass on lavapipe was 59 ms against 22 ms without. Hardware filters at 16x.
- **A station's roads are made from its layout** (2026-11-12, phase 9e), rather than drawn in it:
  a road down each pier's side, legs joining each bay and pad to it and to each other where the deck
  allows, and each pad routed once by A*. A layout already says where its decks, pads and bays are,
  and roads written beside them would have to agree with all three; made from them, a mod that
  moves a pad moves its road, and a layout whose pad no bay reaches is refused as it is defined.
  The graph is the layout's, rebuilt as it is defined, and a buggy's route in a save is its nodes,
  which the same layout makes the same.
- **A buggy is out only while it carries** (2026-11-12, phase 9e). It comes out of its pad's bay
  when the pad's ship has cargo for it and goes back in when there is none, an entity only while it
  is on the deck, so a station at rest holds no buggies in its state and costs nothing a step, and
  a pad's buggies are a cap on how many are out at once. Parked buggies are the dressing's, never
  the simulation's: a station whose buggies do not work (`DeckTraffic::working`) moves its cargo at
  the rate working ones would, a load each way at each round trip of the pad's road shared among
  them, so the choice changes what a station costs and not how long it takes, within a load.
- **Buggies give way at nodes by holding them** (2026-11-12, phase 9e). A buggy holds a node while
  it is within 6 m of it, before or after, and one coming up to a node another holds stops short of
  its reach; in a lane it keeps 8 m behind the buggy ahead; the lanes are 4 m apart, so two buggies
  pass in opposite lanes. Who holds a node is where the buggies are, not state of its own, and of two
  that reach for it in one step the first in entity order has it. Every edge is at least twice the
  reach, so a buggy never holds the node ahead while it holds the one behind, and no ring of
  buggies can each wait for the next: twelve pads loading at once at Highport finish.
- **A ship is held until its cargo has crossed** (2026-11-12, phase 9e): until every crate for it
  is aboard and every crate off it has been taken from its hatch, not until the last has reached the
  bay. The trade settled the crates as it was made, so what holds the ship is its hatch: crates on a
  buggy on its way to the bay are the station's to bring in.
- **The simulation's agents act through commands applied at once** (2026-11-12, phase 9f). A task
  of a tree sends the commands a player would (a pad asked for, a trade, a lane entered), and
  `Sim::SimWorld::Apply` applies each as it is sent, through the handler the queue's go through, so
  it meets the same checks and the docks answer an agent's request and a player's alike. Not queued
  for the next step: the queue is what a session records, and a replay feeding back the agents'
  commands would have them twice, since the replaying world's trees send them again. The trees run
  first in the step, so an agent's commands find the world where the queue's do.
- **Behaviour trees resume their running child, and nothing interrupts it** (2026-11-12, phase 9f).
  A sequence or a selector goes back to the child that was running rather than ticking its children
  again from the first, and no node leaves a running one, so a task that sent a command and waits on
  its answer is never left half done and needs no undoing, and an entity's place in its tree is a
  memory and a flag a node, saved with the world. A repeat or a loop starts its child's next run at
  the next tick, so a step's work is bounded. A tree that must notice something mid-task wants
  reactive nodes, which halt what they leave ([carried forward](#carried-forward)); none is built
  until a game's tree needs one.
- **Multiplayer in time; single-player first** ([scope](scope.md), 2026-11-12). No networking is
  built yet, but what the simulation holds is planned for several players: commands carry who sent
  them, shared facilities (pads, queues, bays) are given by the step in command order, and nothing
  in a simulation module assumes one player's ship.
- **Pack files are compressed with zstd, one copy shared with libktx** (2026-11-13, phase 11a, the
  open decision of phase 5). libktx compiles its own zstd 1.5.5, with public `ZSTD_*` symbols, in
  v4.4.2 and in v5.0.0-rc2 alike, and has no option for another. The engine builds zstd 1.5.7 once,
  the library alone, and `ktx_read` is built against it: its copy is taken out of the target's
  sources and its directory off its include path in `source/external/CMakeLists.txt`, with no patch
  to libktx's files, since it calls the stable API alone (`ZSTD_decompressDCtx` and the error codes).
  One `ZSTD_versionNumber` is in the executable, and the planets' zstd-supercompressed textures draw
  as before, the reference frames unchanged. A file in a pack is its own frame, compressed only
  where that pays (vfs/Pack.h); LZ4, the fallback, would have been a second compressor for the same
  job.
- **Mods are directories or packs in the user's data directory, ordered by name** (2026-11-13, phase
  11a; the question [scope](scope.md) left for packaging). A mod is a directory, for its author, or a
  pack, for its players, under `mods/` in the user's data directory, mounted between the base and the
  user's own files in the order of their names' bytes, so the order is the same on every machine and a
  player sets it by naming. Not zip archives: a pack is a few hundred lines the engine owns, reads a
  file without reading the rest, and checks each file it reads, where a zip reader would be another
  dependency for the same thing. A list of enabled mods, in the settings, is the game's to add.
- **Definitions layer by patches, merged as YAML** (2026-11-13, phase 11a, entry 13's finer
  layering). A mod's `<name>.patch.yaml` beside a definition changes what it names over every layer
  below it, and a whole file still replaces one: merging the YAML before it is read, rather than the
  read values, keeps one reader for whole files and patches, with the errors and lines it already
  gives, and lets a patch say what a value cannot (a field removed, a list appended to, its elements
  merged by a key). Each layer migrates on its own, so a YAML step edits only what the data has.
- **WAMR, not wasmtime, runs the scripts** (2026-11-13, phase 11b, the open decision of phase 11).
  WAMR is C, built from its embedding script inside the engine's own build with the engine's
  toolchain and sanitisers, a few hundred kilobytes; its fast interpreter counts instructions
  (`WAMR_BUILD_INSTRUCTION_METERING`), which is the deterministic fuel the simulation needs, and its
  thread manager lets a watchdog stop a call. wasmtime's fuel is as deterministic and its Cranelift
  code much faster, but it is Rust built by cargo, or a prebuilt C library outside the sanitised
  build, and a JIT whose speed nothing needs yet: a handler call costs 0.27 µs and an input call 9 to
  10 µs, almost all of it the fresh instance (Release, the development machine). WAMR is built with
  bulk memory and reference types, which clang's and rustc's defaults use, software bounds checks
  (no signal handler of its own under the sanitisers or 11d's crash handler), and no libc or WASI:
  a script imports nothing but the host's functions. Revisit should a profile find scripts dear;
  WAMR's ahead-of-time compiler is the next step before another runtime.
- **Scripts are C or Rust, the shell's Rust** (2026-11-13, phase 11b). Both compile to wasm32 with
  the devenv's toolchain (clang and lld's wasm-ld, and nixpkgs' rustc, whose `wasm32-unknown-unknown`
  library comes with it), against bindings generated for each from the same layouts. The shell's own
  scripts, the smoke mod's among them, are Rust; the script suite runs its host API module in both
  languages, so C stays proven.
- **An input call starts from the module as it was instantiated** (2026-11-13, phase 11b). An input
  is answered inside the step, so its answer may depend on the option's inputs and on nothing a
  script kept, which a save does not hold and a replay would have to reproduce; an instance made per
  call costs 9 to 10 µs, and one without a host heap (an input is given nothing to copy in) makes it
  four times cheaper than with one. A handler, outside the step, keeps its instance. A trap is not a
  script's end, since it would do the same again: only a call past the time limit stops a script,
  the one outcome that depends on the machine.

## Open decisions

To be settled when the phase that needs them begins. None is open: the last, the Wasm runtime, was
settled as phase 11b began.

