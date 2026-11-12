# Roadmap

The order is set by what is expensive to change later, then by what later work stands on. It
is not set by what looks good in a screenshot. Architectural decisions (layout, the
simulation split, coordinate precision, colour space, serialisation) come before the systems
that would otherwise bake in the wrong answer. "Done" means the gate covers it, with the
commit that did it in the history. Phases 0–8, and the catalogue entries for the systems they
built, are recorded in [history](history.md).

## Phases

| Phase | Work | Exit criterion |
|---|---|---|
| 0–8 | **Done** 2026-11-10 to 2026-11-12: plan, layout and scaffolding, core, runtime architecture, coordinates and render foundations, space rendering, star systems and galaxy, physics and flight, UI, audio and persistence. Recorded in [history](history.md). | Met. |
| 9 | **AI.** Sub-phases 9a–9c; catalogue entry 26. | Automated traders choose trades, plot routes across systems and fly them without collisions. |
| 9a | **Pathfinding.** Done 2026-11-12. A* (`ai/Pathfinding.h`): a consistent estimate or none, the open nodes expanded by estimate and then index, a way in replaced only by a cheaper one, so of equal routes the same one every run; a graph, a cost or an estimate that is not one refused. Routes across the galaxy (`ai/Routes.h`, `PlotRoute`) over its jump points: a lane joins its ends at its transit time, and a system's jump points each other at a crossing cost the caller gives, so two jumps are weighed against one and a long crossing. Courses across a system (`Waypoints`, `PlotCourse`): a ring of six waypoints about each star, planet and moon, legs joined where they clear every body by a clearance, an end near a body given its own way out, searched with the straight-line distance; `CrossingByCourse` makes a route's crossings from them. The simulation's jump (`ai/Lanes.h`, `LaneTravel`): `EnterLane` sends an entity along the lane through a jump point of its system, in none for the transit in whole steps, and the system `lanes`, after physics, brings it out at the far gate at rest, facing the way the gate faces (`Celestial::JumpPointFacing`: its planet, with its station before it), in the frame of the station the gate keeps by; events at both ends and a refusal saying why. `ai` depends on `celestial` and `physics`. The sample gates moved beside their stations (5 and 6 km, beyond each station from its planet, orbiting it with its period), and their rings face the way a ship leaves them. In the shell, a jump along a lane takes the ship (flown again as it comes out), binds it to the arrival system's station, and the console's `route` plots the way from the ship to a system. | Met: in the AI suite, A* against a graph's known answer, a grid's known detour with and without an estimate, and against Floyd and Warshall over every pair of twenty random graphs, each path walked and found the same twice; ties broken the same in either adjacency order; routes across the sample galaxy end to end and back (105 s, two jumps), a shortcut weighed against a slow crossing, crossings asked the same every run; courses round a planet at the known length over its ring and no shorter than the sphere's own geodesic, kept off by a clearance, out from under a ring's face, from Ostra Highport to Pell round Ostra across an orbit; a ship entering Halcyon Gate, out of physics for the 2,400 steps of its 40 s, out at Vesper Gate at rest in Corva Refinery's frame looking at it 5 km off, and a world saved mid-transit, and a replay, arriving with the same state hash. |
| 9b | **Steering.** Seek, arrive, obstacle avoidance and separation, over the spatial index and physics. | A group of ships reaches a station without colliding. |
| 9c | **Decisions.** Utility AI (considerations, response curves, scored actions) and data-defined behaviour trees, ticked in the simulation step. | Phase 9's exit criterion. |
| 10 | **Presentation polish.** Sub-phases 10a–10d; catalogue entries 27 and 28. | |
| 10a | **Atmosphere and planets.** The atmosphere scattering shell; planet shading (clouds, night lights, rings); the planets in the environment's capture, so a hull reflects them; a frame written to an image file from the command line, first, since the rest of phase 10 is judged by looking ([carried forward](#carried-forward)). | |
| 10b | **The transparent pass.** Sorted back to front and lit through the clusters; GPU particles; glass. | |
| 10c | **Post-processing and screen-space effects.** A chain of post passes on the resolved HDR target before the output pass; bloom; screen-space ambient occlusion, from the depth prepass, which writes view-space normals for it and runs whenever it is on; stencil on the depth target (`D32_SFLOAT_S8_UINT`), with separate depth and stencil layouts and its bits allotted, and an object ID buffer written by the depth prepass; outlines of the selected and targeted, by stencil and by ID; material effects through time-driven parameters. Screen-space reflections stay out (see [decided](#decided)). | |
| 10d | **Animation.** Keyframed TRS tracks on any node for mechanical rigs; clip events (a clamp closing plays a sound). | |
| 11 | **Scripting and shipping.** Sub-phases 11a–11d; catalogue entries 29 and 30. | A data mod and a script mod load from the user mount, and a packed build runs. |
| 11a | **Packs.** Data pack layering finished (finer than whole-definition replacement); pack files behind the VFS; their compression (an open decision, with libktx's zstd). | A packed build runs, and a data mod loads from the user mount. |
| 11b | **Scripting.** WAMR or wasmtime decided; the Wasm host with fuel and time limits; a capability-scoped host API generated from reflection; calls from AI considerations and events. | A script mod loads from the user mount and is called from an event. |
| 11c | **Cinematics and grading.** The cinematic camera and sequencer; the colour-grading LUT. | |
| 11d | **Crash handling and localisation.** A backtrace and the log tail written on a fatal signal; localisation string tables. | |

Phase 1 was the last step before large-scale work: every system now has a home, a namespace, a
target and a test, and phases 2–11 fill stubs in rather than invent structure.

Phases 5–11 are split into lettered sub-phases (phases 5's to 7's are in [history](history.md)), each a block of related work that touches the
same modules and documents, so it can be done in one session and the context cleared after it.
A sub-phase names the catalogue entries that are its brief; read those, not the whole catalogue.
A phase is done when its last sub-phase meets the phase's exit criterion, so a reference to
"phase 7" in the code or the documents still means the whole phase. A sub-phase with no exit
criterion yet gets one when it starts, as phase 10's rows do.

## Carried forward

Work that phases 1–9a moved on, and where it lands. Their full record, and catalogue entries 1–25,
are in [history](history.md).

- **Logging in the shell** (entry 3). The app shell does not call `InitLogging`, and the renderer
  writes to `std::cerr`, because the smoke test reads that output; they move to `Log.h` with the
  smoke test's patterns.
- **A body's frame chosen for it** (entry 22, 7b): `Physics::LocalFrame` is set by the game, as the
  shell sets its ship's, and by lane travel as a ship comes out of a gate, into the frame of the
  station the gate keeps by (9a). Taking a ship into a station's frame as it closes and out as it
  leaves, by a radius about each body, lands with steering (9b), whose traffic is the first to cross
  frames between a station and the open system.
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
  `overflow` clipped by a rounded border, or clipped while transformed), which want the stencil the
  depth target gains in 10c, and its layers, filters and shaders (`filter`, `backdrop-filter`,
  `mask-image`, gradients drawn as shaders), which want render targets of the post chain's (10c).
  Until then RmlUi's defaults draw nothing for them, and a screen uses none.
- **The UI, not yet placed** (entry 23, 8b): a stylesheet or document reloaded when its file
  changes (RmlUi keeps a stylesheet for the run); a document's images decoded on the I/O worker
  rather than as the document lays out, which is on the main thread and fine for a screen's icons;
  RmlUi's debugger in the dev tools.
- **The audio, not yet placed** (entry 24, 8c): music that crossfades between pieces and a playlist
  to choose them; Doppler, once something reports velocities to presentation; the ship's own sounds
  (its engines, the cockpit's) and occlusion by hulls; a sound file streamed from its mount rather
  than read whole, which waits on a streaming read in the VFS, with its pack files (entry 10). The
  voice bus has nothing on it yet.
- **The sample game's later slices** ([sample game](sample-game.md)): traders that choose, plot and
  fly trades (9b, 9c), CivSim's order book, the economy's turns and the skill tree's effects, then
  scripting (11).
- **A gate flown through** (entry 26, 9a): the shell sends its ship along a lane wherever it is in
  the system, as the scene jumps, standing in for the game's rule. A gate that sends what flies
  through its ring (a sensor whose contact pushes `EnterLane`), and traders that fly to it, land with
  steering (9b) and behaviour trees (9c). The smoke sequence's jump sends the ship and does not wait
  the 40 s for it to come out; the AI suite covers the arrival.
- **Courses past moving bodies** (entry 26, 9a): a course is straight legs between bodies where they
  are when it is plotted, plotted again as a ship goes. Fine across a station's few kilometres and
  for estimating a crossing; a course across a system flown at real speeds (days, past a planet that
  moves) needs its bodies where they will be, and a way of crossing a system at that scale at all,
  which nothing in the engine has yet. With steering (9b), which flies them.
- **Pack files and their compression** behind the VFS (entry 10), and definition layering finer
  than whole-file replacement (entry 13): 11a.
- **The cinematic camera** (entry 18): 11c.
- **The crash handler hook** in `platform` (entry 6): 11d.
- **A light's intensity driven by time**, as the beacon's lamp blinks (entry 19): with the material
  effects of 10c.
- **The planets in the environment's capture**, so a hull reflects them (entry 19): 10a.
- **A frame written to an image file** (entry 28): `render_test --screenshot <file.png>
  --after-frames <n>` (and a console command) reads back the swapchain image the output pass wrote,
  after the given frame, and writes it as a PNG through stb, then exits if asked. Today a frame is
  seen by running the app on Xvfb and grabbing the root window with `xwd` and ImageMagick, with keys
  driven through `xdotool` and held longer than a lavapipe frame (up to a second at 1280×720), and a
  fixed wait for the scene to settle: slow, timing-dependent, and blind to anything off the virtual
  screen. A capture from inside the frame loop is exact to the frame, needs no display tools, and
  gives image comparisons (a frame against a kept reference, within a tolerance) for the passes of
  phase 10, whose results are otherwise judged by eye. With a scripted camera it also stands in for
  the input the `xdotool` keys provide. It lands first in 10a; nothing earlier needs it, though 8b's
  market screen would be checked with it if it were there.
- **The spatial index kept from the simulation** (entry 21): a system in the simulation step keeping
  one `SpatialIndex` per star system from the entities' `Transform`s, each in the index of its
  `Sim::InSystem` (6d) and an entity on a lane in none. It lands with steering (9b), its first
  reader.
- **A jump's own effect** (entry 21): the fade to black is the effect today; a jump's particles and
  flash come with GPU particles (10b).
- **A destination prefetched** before it is shown (entry 21): the renderer asks for a model when it
  first draws it, so a jump holds its effect over the new system's first frames while they stream
  in, 0.2 to 0.4 s on lavapipe for the sample systems. Not yet placed.
- **A body's look as data** (entry 21): the system scene draws a body it builds as its kind is, from
  a table in code. A look per body or per kind, as a scene fragment a mod can shadow, is not yet
  placed.
- **A glTF model's collision mesh** (entry 22, 7b): a station's mesh is made from the procedural
  shapes the scene draws it with (`app/src/Piloting.h`), which is every station the sample systems
  have; a model's triangles, read from its file on the I/O worker as its rig is, or a collision mesh
  beside it, are not yet placed, and a model on a station is reported and left out.
- **Physics, not yet placed** (entry 22): a shape offset in its body, and compound shapes; collision
  filtering beyond static against moving (the game's own layers and groups); Jolt's debug renderer
  drawn through debug draw; Jolt's job system on more than one thread, once a profile asks for it
  (deterministic either way); the capacities tuned against real traffic; overlap queries (every body
  in a shape) and casts that return every hit, with steering (9b) their likely first reader.
- **Docking as the engine's** (entry 22, 7c): the shell docks its ship in a sensor before the dock
  (`app/src/Docking.h`), standing in for a game. Whether docking is a state the engine keeps, for AI
  traffic to request and wait on, is decided with behaviour trees (9c), whose "request docking, dock"
  is the first to need it.
- **Picking a paused world, and under the cursor every frame** (entry 22, 7c): a pick is answered by
  the next step, so a paused world answers when it resumes, and a hover highlight would send a
  command a frame. Both want a query presentation makes itself, against the bodies' shapes in a
  snapshot. Not yet placed.
- **Not yet placed** (entries 19 and 20): the sun's shadow distance and split blend tuned by
  measurement; hysteresis between levels of detail, and levels for model files; the planets' own
  shadows past the shadow distance (a station in eclipse), which would need an analytic test against
  the bodies; programs that hot-reload; cube maps and texture arrays in files; normal mapping, which
  waits for tangent-space vertex data (`math/Tangents.h` makes it); a glTF model's own
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

Entries 1–25, for phases 1–8, are in [history](history.md#system-catalogue-entries-125). The
numbers carry on here.

### Shaders · phase 10

Phases 4 and 5 made programs found by name through the asset VFS and built by `PipelineCache` for
each render state a pass asks for, with shared GLSL in `shaders/include`, and added the shaded
set: the metallic-roughness BRDF under one lighting include, the sky and its bake, the cluster
compute programs, the shadow and prepass depth programs, and the instance buffer every mesh
program reads ([history](history.md#shaders--phase-5)). A material names its program and supplies
its parameters and texture slots, so adding a program is data plus GLSL, not a new code path.
Phase 10 adds atmosphere, planet surface, particle and glass programs on the same terms, lit
through the same include, and the post chain's programs. Shader variants are compile-time defines
on one source (`jettison_compile_shader_variant`). Programs do not hot-reload yet; materials and
textures do.

### 26. AI · phase 9 · `engine/ai`

Utility AI (considerations, response curves, scored actions), behaviour trees (data-defined,
ticked in the simulation step), A* over graphs (jump lanes, in-system waypoints), steering
(seek, arrive, obstacle avoidance, separation). Everything runs in the simulation and is
deterministic.

### 27. Animation · phases 1 and 10 · `engine/animation`

Phase 1 moved the glTF clip player and the rotation animation out of `Model` and `Scene` into
this module as `Rig`, `Components.h` and `Playback.h`, unchanged in behaviour. Phase 10
generalises it to keyframed TRS tracks on any node and adds clip events (for example, "clamp
closed" triggering a sound).

### 28. Presentation polish · phase 10 · `engine/render`

The atmosphere scattering shell, planet shading (clouds, night lights, rings), GPU particles
with a sorted transparent pass, glass on that transparent pass. The transparent pass is
forward-lit through the same clusters as the opaque one, which is one of the reasons the
renderer is clustered forward.

Post-processing is a chain of passes in the pass list, on the resolved HDR target between the
opaque and transparent passes and the output pass's tonemapping: bloom first (stars, engines and
lit windows are far brighter than the hull), and the colour-grading LUT of 11c later. Screen-space
ambient occlusion reads the depth prepass (5e), which then also writes view-space normals and runs
whenever SSAO is on; the occlusion attenuates the image-based and ambient light in `lighting.glsl`,
not the sun or the clusters' lights, so it is computed before the opaque pass shades rather than
darkening the frame after it. With MSAA the prepass's depth and normals are resolved to one sample
for it. Masks come two ways ([decided](#decided)). The depth target gains a stencil aspect
(`D32_SFLOAT_S8_UINT`, with the device's separate depth and stencil layouts, and resolved to sample
zero for the single-sampled post passes): bits 0 to 2 the engine's (the cockpit, the selected or
targeted, and what post-processing leaves alone), 3 to 7 the game's (a scanner's hologram, say).
The prepass, when it runs, writes it, so the opaque pass and the post passes test it, and the
hardware skips what a full-screen pass masks out before shading it. And the depth prepass writes an
object ID buffer beside its normals, which no material program has to know of: from it a
jump-flood pass draws wide, soft outlines and target glows that read at any distance, where the
stencil gives the crisp one-pixel kind for nothing. Screen-space reflections stay out
([decided](#decided)).

### 29. Scripting · phase 11 · `engine/script`

A Wasm host: module loading from mod mounts, fuel/time limits, a capability-scoped host API
generated from reflection, and calls into scripts from AI considerations and events.
*Library*: WAMR (WebAssembly Micro Runtime), to be confirmed against wasmtime when the phase
starts. WAMR 2.4.5 is fetched and proven to link and run a module on Linux, built from its
embedding script with the fast interpreter only.

### 30. Shipping · phase 11

Pack files, crash handling (a backtrace and the log tail, written on a fatal signal), the colour-grading LUT, the cinematic camera and sequencer, localisation string tables.

## Decided

Settled on 2026-11-10:

- **C++20.** The language standard stays where it is. `std::expected` and `std::print` are not
  available; see [coding standards](coding-standards.md#errors) for what is used instead.
- **Linux only**, on x86-64: Wayland for the desktop, X11 for the headless tests, one
  toolchain (Clang 21 from the devenv shell).
- **spdlog for logging** (catalogue entry 3).
- **Clustered forward rendering.** Lights are assigned to view-space clusters in compute, and
  every surface is shaded once in its own material program. There is no G-buffer; SSAO, when it
  comes (10c), reads the depth prepass's depth and normals. Phases 4, 5
  and 10 are planned around it (catalogue entries 17, 19 and 28), and
  [architecture](architecture.md#rendering-clustered-forward) has the frame and the rules it sets.
- **KTX-Software stays on v4.4.2** (2026-11-11, phase 5a). Phase 5 was to move to v5 if it was out
  of release candidate; its newest tag is still v5.0.0-rc2. v4.4.2 reads every file the engine
  needs (KTX 2, Basis Universal ETC1S and UASTC, zstd supercompression). Revisit when v5 is
  released.
- **The cluster grid is 32 × 18 × 48** (2026-11-11, phase 5e), up from 16 × 9 × 24, with the
  index list at 32 entries a cluster on average and the cluster range at 2 km. Against 265 lights on
  the orbit scene's station, the opaque pass fell from 0.43 ms to 0.34 ms on an RTX 5060 Ti and from
  47 ms to 36 ms on lavapipe, with the assignment no dearer; 16 × 9 × 48 and 32 × 18 × 24 each
  gained about half that. The list's busiest frame, from 20 m, used 16 entries a cluster. The cluster
  range was not varied: every light measured was within 200 m.
- **The depth prepass is kept, and off** (2026-11-11, phase 5e). Measured on `orbit.yaml`, it
  cost more than it saved on both devices: 0.023 ms for 0.016 ms of the opaque pass on hardware,
  3.4 ms for 0.5 ms on lavapipe, where front-to-back batches already let the depth test reject what
  is behind. It stays as the place SSAO's normals are written (10c), which turns it on.
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
- **The sample game is the Civilisation Simulation's plan** (2026-11-12, phase 8b). The shell plays
  the game's part from the plan in `~/projects/civilisation-simulation`, the local copy over the
  GitHub one, slice by slice as the engine systems that each needs arrive
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
  steering would have.
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

## Open decisions

To be settled when the phase that needs them begins:

- **Wasm runtime.** WAMR or wasmtime (phase 11).
- **Pack file compression** (phase 11). zstd is the natural choice, but libktx (phase 5)
  compiles its own zstd 1.5.5 with public `ZSTD_*` symbols, so a standalone zstd cannot be
  added to the same executable. The options, in order of preference: KTX-Software upstream
  accepts an external zstd, which removes the copy; the engine builds one zstd and patches
  libktx to use it; or pack files use a compressor nothing else in the set carries (LZ4, which
  Tracy carries only in its own namespace).
