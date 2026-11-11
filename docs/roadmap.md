# Roadmap

The order is set by what is expensive to change later, then by what later work stands on. It
is not set by what looks good in a screenshot. Architectural decisions (layout, the
simulation split, coordinate precision, colour space, serialisation) come before the systems
that would otherwise bake in the wrong answer. "Done" means the gate covers it, with the
commit that did it in the history. Phases 0–5, and the catalogue entries for the systems they
built, are recorded in [history](history.md).

## Phases

| Phase | Work | Exit criterion |
|---|---|---|
| 0–5 | **Done** 2026-11-10 to 2026-11-11: plan, layout and scaffolding, core, runtime architecture, coordinates and render foundations, space rendering. Recorded in [history](history.md). | Met. |
| 6 | **Star systems and galaxy.** Sub-phases 6a–6d; catalogue entries 21 and 16. | A jump between two systems unloads one and loads the other while the galaxy-level simulation keeps ticking. |
| 6a | **The scene leaves `render`.** Done 2026-11-11. `IRenderer.h` is the renderer the engine sees (the window, the frame of a scene, the UI, the frame's cost) and `CreateRenderer` the Vulkan backend behind it, its headers private under `render/src/vulkan` and Vulkan a private library. The scene is `engine/scene`: its components, its file, `SceneUpdateSystem` and the camera rigs, naming no device. The file is version 2, read and written through `YamlArchive` with one check shared by the loader and the writer, every path in it virtual; `serialise` learnt `std::optional` fields. Models are a resource keyed on their paths (`ModelCache`), retired after the frames in flight rather than behind an idle device; a model's rig is a resource of its own, read from the glTF file by `animation`, which moved to the World tier, below `scene`. | Met: `Application` holds `Render::IRenderer` alone and includes no Vulkan header; the sample and orbit scenes render as before (the Fox posed once its rig is in), and the smoke test, unchanged, passes both under validation. |
| 6b | **System scenes and layers.** A scene per star system, built from the system description and the simulation state; layers within it. | `halcyon.yaml` builds a scene whose bodies and stations follow their orbits. |
| 6c | **Spatial index.** A loose octree and a sorted grid measured against each other over simulation entities, one kept (an open decision). | Range and nearest queries are tested against brute force, and the measurement is recorded. |
| 6d | **Jumps.** The jump-lane graph in `celestial`; jump transitions with asynchronous loading behind a transition effect; the galaxy simulation ticking across them. | Phase 6's exit criterion. |
| 7 | **Physics and flight.** Sub-phases 7a–7c; catalogue entry 22. | A piloted ship collides with a station, docks through a sensor, and is picked with the mouse. |
| 7a | **Jolt in the simulation.** Jolt in double precision and `CROSS_PLATFORM_DETERMINISTIC`; rigid and static bodies; box, sphere, capsule, cylinder, convex-hull and mesh shapes; stepping inside the fixed step; physics state in saves. | Bodies collide, and the world's state hash after a recorded run is the same in Debug, Release and under the sanitisers. |
| 7b | **Flight.** HOTAS and other joystick axes and the mouse wheel (moved from phase 3); flight controls as commands; the piloted ship interpolated; the chase and cockpit rigs on it. | A ship is flown with keyboard, gamepad or HOTAS and collides with a station. |
| 7c | **Sensors, contacts and queries.** Sensors (docking zones), contact events to the simulation, ray and shape casts, picking with the mouse. | Phase 7's exit criterion. |
| 8 | **UI, audio and persistence.** Sub-phases 8a–8c; catalogue entries 23, 24 and 25. | A market screen bound to simulation data; a save written by version N loads in N+1. |
| 8a | **Persistence.** Versioned save games of the simulation world and the galaxy, with migrations as a chain from N to N+1; whether definitions migrate; fsync on the user mount; settings and keybindings stored apart from saves, and the shell loading the user's bindings (moved from phase 3). | A save written by version N loads in N+1, and a rebinding survives a restart. |
| 8b | **UI.** RmlUi through the Vulkan backend, documents and stylesheets through the VFS, FreeType shared with ImGui, data models bound to simulation snapshots. | A market screen bound to simulation data. |
| 8c | **Audio.** miniaudio; buses (music, effects, UI, voice) with volumes from settings; 3D sources on presentation entities; streamed music; simulation events mapped to sounds. | A simulation event plays a positioned sound on the effects bus, and music streams. |
| 9 | **AI.** Sub-phases 9a–9c; catalogue entry 26. | Automated traders choose trades, plot routes across systems and fly them without collisions. |
| 9a | **Pathfinding.** A* over the jump-lane graph and over in-system waypoints. | Routes are tested against known shortest paths, and the same every run. |
| 9b | **Steering.** Seek, arrive, obstacle avoidance and separation, over the spatial index and physics. | A group of ships reaches a station without colliding. |
| 9c | **Decisions.** Utility AI (considerations, response curves, scored actions) and data-defined behaviour trees, ticked in the simulation step. | Phase 9's exit criterion. |
| 10 | **Presentation polish.** Sub-phases 10a–10d; catalogue entries 27 and 28. | |
| 10a | **Atmosphere and planets.** The atmosphere scattering shell; planet shading (clouds, night lights, rings); the planets in the environment's capture, so a hull reflects them. | |
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

Phases 5–11 are split into lettered sub-phases (phase 5's are in [history](history.md)), each a block of related work that touches the
same modules and documents, so it can be done in one session and the context cleared after it.
A sub-phase names the catalogue entries that are its brief; read those, not the whole catalogue.
A phase is done when its last sub-phase meets the phase's exit criterion, so a reference to
"phase 7" in the code or the documents still means the whole phase. A sub-phase with no exit
criterion yet gets one when it starts, as phase 10's rows do.

## Carried forward

Work that phases 1–5 and 6a moved on, and where it lands. Their full record, and catalogue entries
1–20, are in [history](history.md).

- **Logging in the shell** (entry 3). The app shell does not call `InitLogging`, and the renderer
  writes to `std::cerr`, because the smoke test reads that output; they move to `Log.h` with the
  smoke test's patterns.
- **Jolt's profiler hooks** (`JPH_USE_EXTERNAL_PROFILE`, entry 5) feed Tracy: 7a. GPU zones
  for Vulkan, a zone per declared pass, were done in 5e.
- **HOTAS and other joystick axes, and the mouse wheel** (entry 14): 7b.
- **Migrations** for saves (entry 9) and the decision whether definitions migrate (entry 13),
  fsync on the user mount (entry 10), and the shell loading the user's bindings (entry 14): 8a.
- **Pack files and their compression** behind the VFS (entry 10), and definition layering finer
  than whole-file replacement (entry 13): 11a.
- **The cinematic camera** (entry 18): 11c.
- **The crash handler hook** in `platform` (entry 6): 11d.
- **A light's intensity driven by time**, as the beacon's lamp blinks (entry 19): with the material
  effects of 10c.
- **The planets in the environment's capture**, so a hull reflects them (entry 19): 10a.
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

Entries 1–20, for phases 1–5, are in [history](history.md#system-catalogue-entries-120). The
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

### 21. Scene management and spatial index · phase 6 · `engine/scene`, `engine/spatial`

A scene per star system, built from the system description and the simulation state. Layers
within it. Jump transitions with asynchronous loading behind a transition effect. The spatial
index over simulation entities, either a loose octree or a sorted grid per system, chosen by
measurement. The jump-lane graph lives in `celestial`; the queries over it live in `ai`.

### 22. Physics · phase 7 · `engine/physics`

Jolt in double precision. Rigid and static bodies; box, sphere, capsule, cylinder, convex hull
and mesh shapes; sensors; contact events to the simulation; ray and shape casts. It steps inside
the simulation's fixed step, and the presentation world interpolates. Jolt is built with
`CROSS_PLATFORM_DETERMINISTIC`, so Debug and Release builds step identically, and with RTTI and
exceptions on to match the engine. Its public instruction-set flags make x86-64 with AVX2 the
engine's CPU baseline from this phase on, as decided; the start-up CPU check (entry 6) that makes
it safe has been in place since phase 2.
*Library*: Jolt Physics.

### 23. UI · phase 8 · `engine/ui`

RmlUi rendered through the Vulkan backend, with documents and stylesheets loaded through the
VFS so mods can restyle. Data models bound to simulation snapshots, never to simulation state
directly. Fonts through FreeType, shared with ImGui.
*Library*: RmlUi, FreeType.

### 24. Audio · phase 8 · `engine/audio`

Buses (music, effects, UI, voice), 3D sources attached to presentation entities, streamed music,
and simulation events mapped to sounds.
*Library*: miniaudio.

### 25. Persistence · phase 8 · `engine/serialise`, `engine/sim`

Save games of the simulation world and the galaxy, with format version and engine version.
Migrations are a chain of functions from version N to N+1. Settings and keybindings are stored
apart from saves.

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
- **x86-64 with AVX2 is the minimum CPU**: Intel Haswell (2013) or AMD Excavator (2015) and
  later. Jolt adds its AVX2 instruction-set flags publicly, so they reach every target that links
  it, and the engine accepts that rather than building Jolt for an older instruction set
  (catalogue entries 6 and 22).

## Open decisions

To be settled when the phase that needs them begins:

- **Wasm runtime.** WAMR or wasmtime (phase 11).
- **Pack file compression** (phase 11). zstd is the natural choice, but libktx (phase 5)
  compiles its own zstd 1.5.5 with public `ZSTD_*` symbols, so a standalone zstd cannot be
  added to the same executable. The options, in order of preference: KTX-Software upstream
  accepts an external zstd, which removes the copy; the engine builds one zstd and patches
  libktx to use it; or pack files use a compressor nothing else in the set carries (LZ4, which
  Tracy carries only in its own namespace).
- **Spatial index structure** (phase 6), by measurement.
