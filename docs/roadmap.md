# Roadmap

The order is set by what is expensive to change later, then by what later work stands on. It
is not set by what looks good in a screenshot. Architectural decisions (layout, the
simulation split, coordinate precision, colour space, serialisation) come before the systems
that would otherwise bake in the wrong answer. "Done" means the gate covers it, with the
commit that did it in the history. Phases 0–4, and the catalogue entries for the systems they
built, are recorded in [history](history.md).

## Phases

| Phase | Work | Exit criterion |
|---|---|---|
| 0–4 | **Done** 2026-11-10 to 2026-11-11: plan, layout and scaffolding, core, runtime architecture, coordinates and render foundations. Recorded in [history](history.md). | Met. |
| 5 | **Space rendering.** Sub-phases 5a–5e; catalogue entries 19 and 20, and the shader plan before entry 19. | The sample scene is a lit, shadowed station over a planet, its floodlights and beacons point and spot lights assigned to clusters, drawn within a frame budget measured on lavapipe and on hardware. The lights-per-cluster debug view shows the assignment. |
| 5a | **Textures, materials and geometry.** Done 2026-11-11. KTX2 through `ktx_read` with BasisU transcoding (UASTC and ETC1S, to BC7 where the device has it and RGBA8 where not), as a `resource` loader with a GPU `finish` and a `retire` that frees a replaced image once no frame in flight reads it; stb_image and CPU mip generation, filtered in linear light, for development textures; the material system as data (`render/Material.h`: a material file names a program and sets its parameters, texture slots and time-driven parameters; a program descriptor beside its SPIR-V says what it takes); ico and cube spheres, rings and cylinders; a triplanar program built from the mesh shaders with a define. KTX-Software stays on v4.4.2 (see [decided](#decided)). | Met: `materials/hull.material.yaml` names `mesh_triplanar` and two KTX2 textures, loads through the VFS and the resource manager, and draws on the station in `orbit.yaml`; `panels.material.yaml` draws the same texture through texture coordinates in the sample scene. A texture rewritten on disk reloads through the resource manager in the render suite, and the smoke test's reload stage reloads every texture and material on the GPU, with validation and synchronization validation, in both scenes. |
| 5b | **PBR, image-based lighting and the sky.** The metallic-roughness BRDF as the shared lighting include, the sun only; per-system environment cubemaps, prefiltered, and the BRDF lookup table; the unlit/emissive program; the starfield skybox; tonemapping in the output pass. | The station is shaded by the sun and its environment, against the starfield, and the BRDF matches reference values in a test. |
| 5c | **Clustered lights.** `LightComponent` gains a type, a range and cone angles; windowed inverse-square falloff; the cluster grid and the first compute programs (cluster bounds, light assignment), with the lookup added to the lighting include; the frame UBO's single light retired; the lights-per-cluster heat map. | The station's floodlights and beacons light it through the clusters, the heat map shows the assignment, and a CPU reference agrees with the GPU's assignment. |
| 5d | **Sun shadows.** Cascaded shadow maps for the sun: the shadow-depth program, the cascade passes in the pass list, cascade selection in the lighting include. | The station casts a stable shadow on itself at orbit and close-up distances. |
| 5e | **Culling, instancing, LOD and the budget.** Frustum and planet-horizon culling, instancing, LOD selection, the depth prepass measured and kept or dropped, the cluster grid tuned, GPU zones per declared pass (moved from phase 2 to 4 and not yet done). | Phase 5's exit criterion. |
| 6 | **Star systems and galaxy.** Sub-phases 6a–6d; catalogue entries 21 and 16. | A jump between two systems unloads one and loads the other while the galaxy-level simulation keeps ticking. |
| 6a | **The scene leaves `render`.** `IRenderer.h` (moved from phase 4), with Vulkan types kept inside `render/vulkan`; the scene into `engine/scene`; the model cache onto the resource manager; the scene file onto `serialise`'s archives. | The shell talks to the renderer through the interface only, and the sample and orbit scenes load and render as before, the smoke test unchanged. |
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
| 10a | **Atmosphere and planets.** The atmosphere scattering shell; planet shading (clouds, night lights, rings). | |
| 10b | **The transparent pass.** Sorted back to front and lit through the clusters; GPU particles; glass. | |
| 10c | **Post-processing and material effects.** Bloom; material effects through time-driven parameters. | |
| 10d | **Animation.** Keyframed TRS tracks on any node for mechanical rigs; clip events (a clamp closing plays a sound). | |
| 11 | **Scripting and shipping.** Sub-phases 11a–11d; catalogue entries 29 and 30. | A data mod and a script mod load from the user mount, and a packed build runs. |
| 11a | **Packs.** Data pack layering finished (finer than whole-definition replacement); pack files behind the VFS; their compression (an open decision, with libktx's zstd). | A packed build runs, and a data mod loads from the user mount. |
| 11b | **Scripting.** WAMR or wasmtime decided; the Wasm host with fuel and time limits; a capability-scoped host API generated from reflection; calls from AI considerations and events. | A script mod loads from the user mount and is called from an event. |
| 11c | **Cinematics and grading.** The cinematic camera and sequencer; the colour-grading LUT. | |
| 11d | **Crash handling and localisation.** A backtrace and the log tail written on a fatal signal; localisation string tables. | |

Phase 1 was the last step before large-scale work: every system now has a home, a namespace, a
target and a test, and phases 2–11 fill stubs in rather than invent structure.

Phases 5–11 are split into lettered sub-phases, each a block of related work that touches the
same modules and documents, so it can be done in one session and the context cleared after it.
A sub-phase names the catalogue entries that are its brief; read those, not the whole catalogue.
A phase is done when its last sub-phase meets the phase's exit criterion, so a reference to
"phase 7" in the code or the documents still means the whole phase. A sub-phase with no exit
criterion yet gets one when it starts, as phase 10's rows do.

## Carried forward

Work that phases 1–4 moved on, and where it lands. Their full record, and catalogue entries
1–18, are in [history](history.md).

- **Logging in the shell** (entry 3). The app shell does not call `InitLogging`, and the renderer
  writes to `std::cerr`, because the smoke test reads that output; they move to `Log.h` with the
  smoke test's patterns.
- **GPU zones for Vulkan** (entry 5), a zone per declared pass: 5e. Jolt's profiler hooks
  (`JPH_USE_EXTERNAL_PROFILE`) feed Tracy: 7a.
- **The first compute program** (entry 17), the cluster passes: 5c.
- **The renderer interface** (`IRenderer.h`, entry 17), the scene out of `render`, the scene's
  model cache onto the resource manager (entry 11), and the scene file onto `serialise`'s
  archives (entry 9): 6a.
- **HOTAS and other joystick axes, and the mouse wheel** (entry 14): 7b.
- **Migrations** for saves (entry 9) and the decision whether definitions migrate (entry 13),
  fsync on the user mount (entry 10), and the shell loading the user's bindings (entry 14): 8a.
- **Pack files and their compression** behind the VFS (entry 10), and definition layering finer
  than whole-file replacement (entry 13): 11a.
- **The cinematic camera** (entry 18): 11c.
- **The crash handler hook** in `platform` (entry 6): 11d.

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

Entries 1–18, for phases 1–4, are in [history](history.md#system-catalogue-entries-118). The
numbers carry on here.

### Shaders · phases 5 and 10

Phase 4 made programs found by name through the asset VFS (`mesh`, `output`, `debug_line`), built
by `PipelineCache` for each render state a pass asks for, with shared GLSL in `shaders/include`
([history](history.md#shaders--phase-4)). The plan from here:

- **Phase 5** adds the shaded set: the **metallic-roughness BRDF** (glTF's PBR model: GGX
  distribution, Smith geometry, Schlick Fresnel, image-based lighting from a prefiltered
  environment and a BRDF lookup table), an unlit/emissive program, the skybox, shadow depth for
  the sun cascades, and tonemapping. Two compute programs come with it: cluster bounds and light
  assignment. The cluster lookup and the BRDF are one shared include, and every lit program uses
  it, so a material never lights itself its own way. A material names its program and supplies
  its parameters and texture slots (entry 19), so adding a program is data plus GLSL, not a new
  code path.
- **Phase 10** adds atmosphere, planet surface, particle and glass programs on the same terms,
  lit through the same include.

Shader variants (skinned and static, alpha-masked and opaque) are compile-time defines on one
source, not copies of it: `jettison_compile_shader_variant` builds one under its own program name,
as `mesh_triplanar` is `mesh` with `TRIPLANAR` (5a). Materials and textures hot-reload through the
resource manager since 5a; programs do not yet.

### 19. Space rendering · phase 5 · `engine/render`

Clustered forward shading, as [architecture](architecture.md#rendering-clustered-forward)
describes. Point and spot lights have finite ranges with windowed inverse-square falloff, and
are assigned to a grid of view-space clusters by compute: 16 × 9 × 24 to start with, tuned by
measurement, and covering the cluster range. The directional sun is separate, in the frame
constants, with cascaded shadows. Whether an opaque depth prepass pays for itself is measured
here, not assumed. Also in this phase: PBR (glTF metallic-roughness) with image-based lighting
from per-system environment cubemaps, a starfield skybox, frustum and planet-horizon culling,
instancing, LOD selection and tonemapping. `LightComponent` gains a type, a range and spot cone
angles, and the frame UBO's single light is retired. Debug draw gets a lights-per-cluster heat
map. Materials are data: a shader, parameters and texture slots, with time-driven parameters for
effects.

### 20. Geometry and textures · phase 5 · `engine/render`

Done in 5a. Procedural meshes (`render/Procedural.h`): UV, ico and cube spheres, rings,
cylinders and boxes, each tested watertight, wound outwards and of the right volume. Textures
(`render/Texture.h`) decode on the I/O worker without a device: KTX 2 files transcode from Basis
Universal to BC7, or to RGBA8 on a device without block compression, and keep their colour space
from the file's data format descriptor; PNG and JPEG through stb_image get a mip chain made on
the CPU, averaged in linear light for sRGB data. `MaterialCache` uploads them in the loader's
`finish` and hands a replaced image to a queue that frees it `kFramesInFlight` frames later.
Materials (`render/Material.h`) are YAML through `serialise`: a material names a program and sets
its vec4 parameters, texture slots and time-driven parameters (a sine, triangle, saw or square
wave between two values, evaluated in double precision); `shaders/<program>.program.yaml` lists
what the program takes, and every scene program shares one layout (four texture slots in set 1,
the parameters in the frame's material buffer). A material that cannot be drawn - its program
missing, a parameter its program lacks - is logged once and the draw falls back to the model's
own. Triplanar mapping is `mesh_triplanar`, the mesh shaders built with `TRIPLANAR` by
`jettison_compile_shader_variant`, projecting in metres along the object's axes.
The engine links `ktx_read` (transcode and upload) only: `ktx` and `ktx_read` compile the same
transcoder and zstd, and cannot both be in one executable. An offline texture tool that encodes
links `ktx` instead; the sample textures are encoded with KTX-Software's own `ktx` tool
(`scripts/make-sample-textures.py` has the commands).
Left for later: program descriptors and SPIR-V are read once and do not hot-reload (materials and
textures do); cube maps and texture arrays, which 5d's environment maps need; and normal mapping,
which waits for 5b's BRDF and tangent-space vertex data.
*Library*: KTX-Software (libktx) v4.4.2; stb_image stays for development.

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
with a sorted transparent pass, bloom, glass on that transparent pass. The transparent pass is
forward-lit through the same clusters as the opaque one, which is one of the reasons the
renderer is clustered forward.

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
  every surface is shaded once in its own material program. There is no G-buffer. Phases 4, 5
  and 10 are planned around it (catalogue entries 17, 19 and 28), and
  [architecture](architecture.md#rendering-clustered-forward) has the frame and the rules it sets.
- **KTX-Software stays on v4.4.2** (2026-11-11, phase 5a). Phase 5 was to move to v5 if it was out
  of release candidate; its newest tag is still v5.0.0-rc2. v4.4.2 reads every file the engine
  needs (KTX 2, Basis Universal ETC1S and UASTC, zstd supercompression). Revisit when v5 is
  released.
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
