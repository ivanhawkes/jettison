# History

The finished part of the [roadmap](roadmap.md): phases 0–6 as they were closed, and catalogue
entries 1–21, the briefs for the systems those phases built, with what each one became. Entry
numbers are the roadmap's, so a reference to "entry 3" means the same system in either document.
Work these phases moved to a later one is listed in the roadmap under
[carried forward](roadmap.md#carried-forward).

## Phases

| Phase | Work | Exit criterion |
|---|---|---|
| 0 | **Plan.** Scope, architecture, roadmap, standards. | These documents are reviewed and agreed. |
| 1 | **Layout and scaffolding.** Done 2026-11-10. The module tree, `jettison_add_module`, the existing code moved into `core`/`app`/`render`/`animation` (the scene parked in `render`, see [architecture](architecture.md#parked-code)), a stub for every in-scope module, doctest and CTest, `Jettison::Engine` and the consumer check, EnTT at v4.0.0. | Met: the gate is green, every module has a passing suite (25 CTest tests), and `source/test/consumer` links `Jettison::Engine` through FetchContent. The install export waits on its dependencies ([build and gates](build-and-gates.md#consuming-the-engine)). |
| 2 | **Core.** Done 2026-11-10. Assertions, logging, Tracy, configuration, string IDs, math additions, deterministic RNG, clocks, platform paths. `core` is filled in (logging over spdlog, assertions reported through it, string IDs and hashing pinned to reference values, layered configuration), and so are `math` (bounds and intersection tests in float and double, splines, easing, PCG32, noise and tangents), `profile` (Tracy zones, frame marks and plots), `time` (the real and simulation clocks, the fixed-step accumulator, the calendar) and `platform` (XDG paths, the executable path, and the start-up CPU check, five phases early). Two pieces moved to the phase that has a place for them: GPU zones to phase 4, with the pass list, and the app shell's move to `Log.h` to the smoke test's move. | Met: the math and RNG suites pass, the RNG checked against PCG32's published output and its distributions and the noise against recorded values. |
| 3 | **Runtime architecture.** Done 2026-11-11. Scheduler and tick phases, the simulation/presentation split with a command queue, events, the FSM utility, reflection, serialisation, VFS mounts, the resource manager, the definition registry, input actions, the console. Filled in are `reflect` (types, fields, enumerations and attributes over EnTT meta, with registration errors that hold in release builds), `serialise` (YAML and binary archives over it, each tested as a round trip and against data that does not fit), `vfs` (ordered mounts, shadowing, enumeration, and whole-file writes to the user mount), the `jobs` I/O worker, `resource` (handles, reference counts, loads on the worker, hot reload), `ecs` (the scheduler and the state machine), `sim` (the world, commands, saves, events, snapshots and the stepper that runs it from frame time, and the definition registry), `input` (actions, contexts and bindings) and the console and dev tools. The shell's frame is the scheduler's phases, and the scene shows the simulation through proxies interpolated between its last two steps. Three pieces moved to the phase that has a place for them: HOTAS axes and the mouse wheel to phase 7, with flight, and loading rebinds from the user's settings to phase 8, with persistence. | Met: a world stepped from a command stream, saved part-way and loaded, carries on to the same bytes as the original, and its state hash is pinned and the same in Debug, Release and under the sanitisers. The smoke test still renders, through the phases, with validation enabled. |
| 4 | **Coordinates and render foundations.** Done 2026-11-11. Double-precision positions, camera-relative rendering, reversed-Z infinite projection, the celestial clock and on-rails orbits, VMA, linear/HDR colour pipeline, a pass list with compute passes beside graphics ones, the MSAA HDR target and its resolve, per-frame storage buffers, debug draw, cameras (orbit, chase, cockpit, map). Filled in are `celestial` (frames, Kepler orbits evaluated on rails, body rotation, star systems read from YAML and resolved, and presentation time interpolated between steps) and, in `render`: VMA, with a leak reported at teardown; Vulkan 1.3, dynamic rendering and synchronization2; the declared pass list (`PassList.h`), its barriers derived at start-up; a 4× multisampled RGBA16F target resolved for the output pass; the frame constants, skin palette, light list and debug lines per frame in flight; pipelines by program and render state, with shared GLSL includes; scene transforms in double precision, made camera-relative before they become floats; reversed-Z with an infinite far plane; debug draw; the four camera rigs and eased transitions; procedural spheres and boxes; and the sun in the frame constants. Debug builds run synchronization validation. Two pieces moved: the renderer interface (`IRenderer.h`) to phase 6, with the scene's move out of `render`, since the scene still owns device models ([parked code](architecture.md#parked-code)); and the first compute program on the GPU to phase 5's cluster passes (compute nodes are planned and their barriers tested today). | Met: in `assets/scene/orbit.yaml` a station orbits an Earth-sized planet 150 million km from its star, at real scale. Taken through the engine's path and compared with a double-precision reference for ten seconds of frames at 1080p, the station is within 0.0002 px of where it should be and the planet within 0.004 px, following either one; a pipeline that makes positions floats first is 1,200 px out on the same frames. The smoke test walks that scene and the sample scene with validation, synchronization validation and VMA, and a leaked allocation fails it. |
| 5 | **Space rendering.** Done 2026-11-11. Sub-phases 5a–5e; catalogue entries 19 and 20, and the shader plan before entry 19. | The sample scene is a lit, shadowed station over a planet, its floodlights and beacons point and spot lights assigned to clusters, drawn within a frame budget measured on lavapipe and on hardware. The lights-per-cluster debug view shows the assignment. Met: `orbit.yaml`, measured by `--benchmark` (see [architecture](architecture.md#the-budget)): 0.30 ms of GPU time a frame on an RTX 5060 Ti at 1248 × 1358, against a budget of 4 ms, and 33 ms on lavapipe at 640 × 480, against 50 ms; with 265 lights on the station, 0.47 ms and 46 ms. F3 shows the assignment, which the smoke test checks every frame. |
| 5a | **Textures, materials and geometry.** Done 2026-11-11. KTX2 through `ktx_read` with BasisU transcoding (UASTC and ETC1S, to BC7 where the device has it and RGBA8 where not), as a `resource` loader with a GPU `finish` and a `retire` that frees a replaced image once no frame in flight reads it; stb_image and CPU mip generation, filtered in linear light, for development textures; the material system as data (`render/Material.h`: a material file names a program and sets its parameters, texture slots and time-driven parameters; a program descriptor beside its SPIR-V says what it takes); ico and cube spheres, rings and cylinders; a triplanar program built from the mesh shaders with a define. KTX-Software stays on v4.4.2 (see [decided](roadmap.md#decided)). | Met: `materials/hull.material.yaml` names `mesh_triplanar` and two KTX2 textures, loads through the VFS and the resource manager, and draws on the station in `orbit.yaml`; `panels.material.yaml` draws the same texture through texture coordinates in the sample scene. A texture rewritten on disk reloads through the resource manager in the render suite, and the smoke test's reload stage reloads every texture and material on the GPU, with validation and synchronization validation, in both scenes. |
| 5b | **PBR, image-based lighting and the sky.** Done 2026-11-11. glTF's metallic-roughness BRDF (`shaders/include/brdf.glsl`) under one lighting include (`lighting.glsl`) that adds the sun, the stand-in light list and the environment; a procedural sky per system (`SkyComponent`: a seeded starfield, the galaxy's band, a nebula) drawn behind the scene by `sky.glsl`, and baked by `Environment` into a cube map, GGX-prefiltered specular levels and an irradiance cube by filtered importance sampling, with the split sum's lookup table made once; the `unlit` program; Khronos PBR Neutral tonemapping in the output pass. The light's ambient colour is retired: a scene that sets one is refused, and the sky lights what the sun does not. | Met: `orbit.yaml`'s station is lit by Halcyon and by its sky, against the starfield, and the sample scene by its key light and a brighter sky. The render suite compiles `brdf.glsl` and `tonemap.glsl` as C++ and checks them against the glTF specification's formulas and a quadrature of the split sum (`scripts/brdf-reference.py`). The smoke test bakes each scene's environment at load and again on its reload, under validation and synchronization validation. |
| 5c | **Clustered lights.** Done 2026-11-11. `LightComponent` is a point or a spot with a range and, for a spot, `KHR_lights_punctual`'s inner and outer cone angles; the falloff is inverse square windowed to zero at the range, and a scene that leaves a range out is refused, while the one-light limit is gone. The grid (16 × 9 × 24, exponential slices from the near plane to a 2 km cluster range) is one GLM-compatible file, `clusters.glsl`, which the first compute programs (`cluster_bounds`, kept between frames until the projection changes, and `cluster_lights`, one invocation per cluster reserving its share of one index list by atomic add), the lighting include and the CPU reference in `render/Clusters.h` all compile. The pass list gains transfer passes, to zero the list's allocator and copy the grid out for the check. The frame UBO's light-list stand-in is retired. F3 shows the lights-per-cluster heat map, and F2 each light's reach. | Met: `orbit.yaml`'s station carries four hull floodlights, two dock lights, a beacon light, two navigation lights and two work lights, and the floodlights visibly light the hub through its clusters; the heat map shows them assigned. The headless render test reads every frame's assignment back and compares it with the CPU reference (21 to 24 frames a scene on lavapipe, none disagreeing), and fails the run on a disagreement, as it did with an assignment broken on purpose; the render suite checks the reference's bounds against the projection and its light tests against brute-force sampling. |
| 5d | **Sun shadows.** Done 2026-11-11. Four cascades of 2048² in one reversed-depth array image (`sun-shadows`, a pass at the head of the pass list that clears each layer and draws every mesh into it), split by the practical scheme (blend 0.8) between the near plane and a 400 m shadow distance. Each cascade covers the bounding sphere of its slice of the view, so its texel size depends only on the projection, and its centre is snapped to whole texels in the sun's frame measured from the camera's frame origin (`CameraView::frameOrigin`, the body an orbit camera circles), in double precision. `shadow_depth` is `mesh.vert` built with `SHADOW_DEPTH`, a vertex stage alone, drawn with depth clamp (now a required device feature) and a slope bias; the lighting include selects a cascade by view depth through `shadows.glsl`, which `render/Shadows.h` compiles as C++, offsets the receiver along its normal, takes nine filtered comparison taps, and blends into the next cascade across the last tenth of each. F4 shows the cascades. | Met: in `orbit.yaml` the hub shadows the collar and the collar the dock-side half of the hub, seen from the opening 160 m in the fourth cascade and from 45 m in the second (F4 shows the hub, one cylinder through the collar, darker under the sun alone on the collar's far side); with the camera still, two frames three seconds apart differ only in the windows' emission and the planet's limb while the station crosses some 100 km of the system. The render suite checks that a point fixed to a moving station lands at the same place within its texel to a thousandth of one as the camera orbits and turns (and that it slides by more than a twentieth when the grid is fixed to the system instead), that turning keeps every texel's size, and that every depth a cascade is selected for lands inside its map with the border to spare. The smoke test runs the cascades in both scenes under validation and synchronization validation, 36 s to 40 s on lavapipe. |
| 5e | **Culling, instancing, LOD and the budget.** Done 2026-11-11. Each draw's bounds in render space in double precision (`render/Culling.h`); meshes and lights outside the frustum or wholly behind a planet's horizon left out, and each cascade's casters culled exactly for a sphere against the prism its map shadows; the procedural shapes in five levels of detail with their geometric errors, the coarsest whose error projects under a pixel drawn; every draw instanced, those sharing a model, a level and a material batched into one and ordered front to back (`render/DrawBatches.h`), transforms in a per-frame instance buffer. The depth prepass is built, measured and kept, off (it is where SSAO's normals will come from); the grid is 32 × 18 × 48, from a light-budget scene of 265 lights (`scripts/make-light-budget-scene.py`); a CPU device gets RGBA8 rather than BC7 textures. GPU time per declared pass, from timestamps the engine reads itself and as Tracy GPU zones (`render/vulkan/GpuProfiler.h`); `--benchmark` reports them with the frame's counts. | Phase 5's exit criterion: met (row 5). In `orbit.yaml` twenty racked containers are one draw, the planet is out of every cascade it cannot shadow (the shadow pass from 15.5 ms to 4.2 ms on lavapipe), and on hardware the frame is 0.30 ms of GPU time. The render suite checks the frustum, horizon and caster tests against brute force, the levels' errors against their meshes, and the batching; a Tracy capture shows a GPU zone per pass. |
| 6 | **Star systems and galaxy.** Done 2026-11-11. Sub-phases 6a–6d; catalogue entries 21 and 16. | A jump between two systems unloads one and loads the other while the galaxy-level simulation keeps ticking. Met: row 6d. |
| 6a | **The scene leaves `render`.** Done 2026-11-11. `IRenderer.h` is the renderer the engine sees (the window, the frame of a scene, the UI, the frame's cost) and `CreateRenderer` the Vulkan backend behind it, its headers private under `render/src/vulkan` and Vulkan a private library. The scene is `engine/scene`: its components, its file, `SceneUpdateSystem` and the camera rigs, naming no device. The file is version 2, read and written through `YamlArchive` with one check shared by the loader and the writer, every path in it virtual; `serialise` learnt `std::optional` fields. Models are a resource keyed on their paths (`ModelCache`), retired after the frames in flight rather than behind an idle device; a model's rig is a resource of its own, read from the glTF file by `animation`, which moved to the World tier, below `scene`. | Met: `Application` holds `Render::IRenderer` alone and includes no Vulkan header; the sample and orbit scenes render as before (the Fox posed once its rig is in), and the smoke test, unchanged, passes both under validation. |
| 6b | **System scenes and layers.** Done 2026-11-11. `SystemScene` builds a star system's scene onto a `Scene`, around what a scene file authored or from the system alone (`Scene::LoadSystem`, `--system`): each body no entity of the file presents gets an entity looking as its kind does (a sun on the star, a sphere the body's size, a hull for a station, a ring marker in the effects layer for a jump point), a scene with no sky one seeded from the system's name, and one with no active camera an orbit camera on the first station. What it builds is a `BuiltComponent`, which the writer leaves out, so a saved scene is the file that was loaded. It places the bodies (the celestial sync moved from `app`), and makes the traffic layer from the simulation's snapshots: a proxy from a factory the game supplies for each entity in them, destroyed with its children when the entity leaves (`scene` now depends on `sim`, and reads its snapshots alone). Layers are a `layer` in the scene file, inherited by children; one hidden (`Scene::SetLayerVisible`, the dev tools' Scene panel) is neither drawn nor lit by. | Met: `halcyon.yaml` alone builds an entity for every body, each at its orbit's position at three times, the station moving about the planet at 7.7 km/s with the camera on it; the orbit scene builds only the gate it does not present, and both render, the orbit scene in the smoke test under validation. |
| 6c | **Spatial index.** Done 2026-11-11. `SpatialIndex` is an octree (`spatial/Octree.h`) over simulation entities as points in the system frame: sphere and box queries in identifier order, and the count nearest a point within a limit, by distance then identifier, every structure deciding membership with the same functions (`spatial/Queries.h`). Its root is a power of two metres across, so every node's bounds are exact in double; a leaf splits past 64 points and its subtree gathers back at half that; a point outside the root is held by the root. Looseness is a setting, at 1. A loose octree and a sorted grid were built and measured against each other (commit `64d5488` has both) and the octree kept (see [decided](roadmap.md#decided)); range results past 512 are radix sorted. | Met: the suite runs every query after inserts, steps, jumps across the system, removals with identifiers reused, a hundred points at one place and points past the root, loose and not, against brute force, and agrees to the last bit; the measurement is `tests/SpatialBenchmark.cpp`, recorded under [decided](roadmap.md#decided). |
| 6d | **Jumps.** Done 2026-11-11. The jump-lane graph is `celestial/Galaxy.h`: the systems by name and file (and a scene file where one is authored), the lanes between a jump point in each of two systems, travelled both ways, with a transit time, a jump point the end of one lane (see [decided](roadmap.md#decided)), each system's file checked against the lanes that end in it, and a system's exits in the galaxy's order; `systems/galaxy.yaml` joins Halcyon to two new systems, Vesper and Marrow, in a line. The simulation is the galaxy's: `Sim::InSystem` says which system's frame an entity's `Transform` is in (the system's name hashed, `kNoSystem` on a lane), snapshots carry it, no interpolation crosses a jump, and a system scene's traffic layer shows its own system's entities alone. `JumpTransition` (`scene/JumpTransition.h`) jumps behind a fade in the output pass, under the UI: the destination's files read on the I/O worker as the fade rises and parsed on the main thread (see [decided](roadmap.md#decided)), a destination that fails abandoned with the fade lifting on the system left, the scene replaced once the fade covers the frame, and the fade held until nothing is loading for the first time (`ResourceManager::Loading`) or for 8 s. `SceneFile` is a scene file read and checked before it replaces anything. The shell reads the galaxy (`--galaxy`), jumps with J, the console's `jump` and the Scene panel, and frames the jump point it arrives at; the smoke sequence gains a jump stage. | Met: the scene suite runs a galaxy's simulation with ships in Halcyon, in Vesper and on the lane between them, one step every frame, and jumps presentation from Halcyon to Vesper and back. Vesper's bodies and traffic replace Halcyon's, among them the courier that came out of the lane while the jump was under way, at the gate, and Halcyon's ships are where every step put them. The smoke test jumps once in each scene, under validation and synchronization validation, the simulation running 94 to 105 steps through each jump: 1.6 to 1.8 s on lavapipe, 0.2 to 0.4 s of it streaming the new system in. |

## System catalogue, entries 1–21

### 1. Build, layout and packaging · phase 1

`jettison_add_module(<name> DEPENDS ...)` creates `jettison_<name>`, the `Jettison::<Name>`
alias, the include layout, the private `jettison_warnings` link and the tests directory.
It exports the CMake package for the game repository.
*Stub*: the helper, `Jettison::Engine`, `cmake/JettisonConfig.cmake.in`.

### 2. Testing · phase 1

doctest, one test executable per module, registered with CTest and run by `scripts/ci.sh`
before the smoke test. The existing `smoke_sequence_test` becomes a doctest case.
*Library*: doctest.

### 3. Core · phases 1–2 · `engine/core`

Assertions (`JETTISON_ASSERT` in debug, `JETTISON_VERIFY` always, `JETTISON_NOT_IMPLEMENTED`),
logging (severity, category, sinks: stderr, file, console), configuration (engine and user
settings, layered), hashed string IDs (`StringId`, compile-time hashing), typed handles with
generation counters, hashing utilities.
*Library*: spdlog, configured with `SPDLOG_USE_STD_FORMAT` as a compile definition on its target,
so the engine uses one formatting library (`std::format`, which C++20 checks at compile time)
rather than also carrying spdlog's bundled fmt. Engine code logs through `Log.h`, never through
spdlog directly, so categories and sinks stay the engine's.
*Stub*: `Assert.h`, `Log.h`, `Config.h`, `StringId.h`, `Handle.h`.
*Filled in* (phase 2): `Log.h` dispatches to spdlog's stderr and file sinks and to engine
callbacks (the console's, in phase 3), with the category per message; a failed assertion logs at
Fatal, flushes, and stops in an attached debugger before it aborts. `StringId` and `Hash.h` hash
with 64-bit FNV-1a, pinned by the published test vectors. `Config` reads and writes the user
layer as nested YAML, round-trip tested. The app shell does not call `InitLogging` yet, and the
renderer still writes to `std::cerr`: the smoke test reads that output line by line, so it moves
to `Log.h` together with the smoke test's patterns.

### 4. Math · phase 2 · `engine/math`

Over GLM: `Aabb`, `Obb`, `Ray`, `Plane`, `Sphere`, `Frustum`, intersection tests, Bezier and
Catmull-Rom splines, easing functions, `Random` (PCG32 with the engine's own distributions),
noise (FastNoiseLite), tangent generation (MikkTSpace), and the double-precision aliases the
simulation uses.
*Stub*: `Bounds.h`, `Intersect.h`, `Spline.h`, `Easing.h`, `Random.h`, `Noise.h`; `Tangents.h`
was added when it was filled in.
*Filled in* (phase 2): each bounding volume is a template with float and double aliases (`Aabb`,
`DAabb`), and the tests are compiled for both. `FrustumFromMatrix` reports a plane at infinity
as one every point is in front of, so reversed-Z with an infinite far plane needs no special
case in culling. `CatmullRom` is centripetal, so a rail through unevenly spaced points does not
overshoot them. `Random` is PCG32, pinned by the reference implementation's published output,
with its distributions and `Noise` pinned by recorded values. `GenerateTangents` returns a new
vertex list, because MikkTSpace splits vertices on UV seams. The module is `DETERMINISTIC`.

### 5. Profiling · phase 2 · `engine/profile`

Tracy zones, frame marks, GPU zones for Vulkan, plot values. It compiles to nothing when
`JETTISON_PROFILE=OFF`. The client version moves only with the profiler's, because the two must
speak the same protocol: v0.13.1, the version nixpkgs provides at the current devenv lock. Jolt's
profiler hooks (`JPH_USE_EXTERNAL_PROFILE`) feed Tracy once physics arrives.
*Library*: Tracy.
*Filled in* (phase 2): `Profile.h` expands `JETTISON_PROFILE_ZONE`, `_FUNCTION`, `_FRAME` and
`_PLOT` to Tracy's macros, and Tracy's own disabled macros make them nothing when
`JETTISON_PROFILE` is off. Tracy is fetched in every tree now and is a public dependency of
`profile`, so `TRACY_ENABLE` travels with its target. Integers plot as `int64_t`, which Tracy's
three overloads would otherwise make ambiguous. The app shell marks each frame and zones its tick.
GPU zones for Vulkan move to phase 4: a zone per declared pass is what the frame needs, and
today's renderer has one pass and no pass list to put them in.

### 6. Platform · phases 1–2 · `engine/platform`

User data, config and cache directories (XDG), executable path, high-resolution time source,
crash handler hook (filled in phase 11). Windowing stays GLFW, behind `render`'s window type.

A CPU check at start-up: a machine without AVX2 gets a message naming the requirement, not an
illegal-instruction crash. Once Jolt's flags reach the engine, the compiler may emit AVX2
anywhere, static initialisers included. The check therefore lives in a translation unit
compiled without those flags and runs before any engine code, and it lands no later than
phase 7, when Jolt is first linked into the engine.
*Filled in* (phase 2): `ResolvePaths` follows the XDG base directory specification (0.8) for data,
config, state (logs, console history) and cache, ignoring a variable that is empty or relative as
the specification says. It touches nothing on disk; a writer creates its directory. The
environment is injectable, so the tests do not depend on the machine's. `ExecutablePath` reads
`/proc/self/exe`. The high-resolution time source is `steady_clock`, which is `CLOCK_MONOTONIC`
here, behind `Time::RealClock`.
The CPU check landed early: it is small, and its hazard was already understood. It is a guard in `.preinit_array`,
which the dynamic loader calls before any static initialiser: on a machine without AVX2, BMI1,
LZCNT, POPCNT or F16C it writes the missing list to stderr with `write(2)` and exits with
status 1. Its translation unit is compiled with `-mno-avx -mno-bmi -mno-lzcnt -mno-popcnt
-mno-f16c` after every other flag, so neither Jolt's `-mavx2` nor a developer's `-march=native`
reaches it, and the guard calls no inline function or template, whose linked copy could come from
an AVX2-compiled translation unit. `platform` links with `--require-defined=JettisonCpuGuard`,
so every executable that links it carries the guard or fails to link; the app shell depends on
`platform` for that reason. A build of the guard with AVX2 forced missing printed the message and
exited before a static initialiser ran.

### 7. Time · phase 2 · `engine/time`

`RealClock`, `SimClock` (pause, compression factor, fixed step, accumulator with a spiral-of-death
clamp), `Calendar` (simulation seconds to an in-game date).
*Stub*: `Clock.h`, `FixedStep.h`, `Calendar.h`.
*Filled in* (phase 2): `RealClock` ticks over `steady_clock`, clamped to a quarter of a second so
a breakpoint does not become a minute of animation, and takes an instant the caller already read,
so the frame loop reads the clock once. `SimClock` turns a frame's real seconds into simulation
seconds (zero while paused, scaled by compression) and counts steps. Compression runs more fixed
steps per frame, not longer ones, so the step stays the one physics and AI were tuned for; past
the accumulator's cap of eight steps a frame, simulated time falls behind the requested rate.
`FixedStepAccumulator` drops whole steps over the cap, keeps the fraction for interpolation, and
reports the time it dropped. `Calendar` takes a whole number of seconds per day, so a moment's
date is integer arithmetic after one floor: hours, minutes and seconds are SI, a day that is not a
whole number of hours ends with a short hour, moments before the epoch are dates before it, and
`Format` writes an ISO 8601 ordinal date. Months wait until a game needs them. The module is
`DETERMINISTIC`.

### 8. Reflection · phase 3 · `engine/reflect`

Registration helpers over EnTT `meta`: types, fields, enums, attributes (saved, editable,
moddable, console). Used by serialisation, the editor inspector, console variables and the
definition schema.
*Stub*: `Reflect.h`.
*Filled in* (phase 3): `RegisterType<T>(name)` and `RegisterEnum<E>(name)` return builders that
add fields (member object pointers) and enumerators, each with `Attribute` bits held in EnTT's
user traits, which leave 15 bits. EnTT keeps a name's pointer rather than a copy, so `Name` has a
consteval constructor: a name that would dangle does not compile. EnTT's duplicate checks are
assertions, absent in release, and a field registered twice silently replaces the first, so
`Reflect` checks itself and throws `RegistrationError` naming the clash: a type registered twice,
a name already taken, a field or enumerator name used twice in one type, and two names whose
32-bit hashes collide, tested with a published FNV-1a collision pair. `Resolve` and `FindField`
compare text as well as the hash for the same reason. `Fields(type, attributes)` lists fields in
registration order, which is the order an archive writes them in, and `EnumName` and `EnumValue`
let a file store an enumerator's name rather than its number. Bases are not reflected, because
components and definitions are plain structs. GLM vectors are not registered here: whether a
`vec3` is a sequence or a map in YAML is a format decision, and the existing scene format writes
sequences, so `serialise` owns it.

### 9. Serialisation · phase 3 · `engine/serialise`

Reflection-driven archives: YAML (authored content and the existing scene format) and binary
(saves). Every format is versioned, and a version mismatch runs migrations or fails with the
version named. Loaders and writers are written as one pair and tested as a round trip, because
the scene's loader and writer once each enforced a rule the other did not know: the loader refused
an animation without a mesh that the writer had saved.
*Library*: yaml-cpp (already pinned).
*Stub*: `Archive.h`.
*Filled in* (phase 3): `YamlArchive` and `BinaryArchive` write a class registered with `reflect`,
its Saved fields in registration order, and read it back into a default-constructed value, so a
field the data leaves out keeps its default, a nested one the default its owner gave it. Fields
are bools, integers, floats, strings, GLM vectors (a flow sequence in YAML, as the scene format
writes them), registered enumerations (by name) and registered classes, and vectors of any of
these. A field the data names but the type does not save is refused rather than ignored, so a
misspelt key fails, and so does a file that tries to set runtime state. Numbers go through
`to_chars` and `from_chars`: the shortest text that reads back exactly, whole-scalar parses at the
field's own width, YAML's `.inf` and `.nan`, and 1.2's `true` and `false` only. A string another
YAML reader would take for a number or a bool is quoted. Errors name the field's path
(`Ship.holds[0].mass`) and the line. YAML puts `version` beside the root's fields, as the scene
file does; the binary layout is little-endian, with a table of the names the file uses, enumerators
included, and every field written with its name's index and its length, so fields are found by
name and a field whose type changed size is reported. Every prefix of a save is refused as a
`FormatError`, tested exhaustively. A version mismatch throws `FormatVersionError` until phase 8
brings migrations. `BinaryWriter` and `BinaryReader` are the streams the binary archive is
made of, public for a format that is a sequence of values rather than one, as a world save is;
they read into a reference as well as into an owned value, and `entt::entity` is a leaf, its
32-bit identifier. So are `glm::quat` and `glm::dquat`, written x, y, z, w, GLM's storage order. The scene stays on its own loader until the scene archive (entities, optional
components, the mesh path relative to the file) moves onto these, with `ecs`.
*Filled in* (6a): the scene file is read and written through `YamlArchive`, as records registered
with `reflect` (`scene/src/Scene.cpp`), at version 2, where a model's path became a virtual path. A
`std::optional` field is left out of the file when it is empty and read as present when the file
names it, in both archives, which is how an entity's optional components are said; an animation's
`time` is refused, as runtime state.

### 10. Virtual file system · phase 3 · `engine/vfs`

Ordered mounts (base, DLC, mods, user), with path lookup through the mount stack and
enumeration across mounts. Read-only except the user mount. Pack files and compression come in
phase 11 behind the same interface.
*Stub*: `FileSystem.h`, `Mount.h`.
*Filled in* (phase 3): lookup walks the mounts from the last added, so a mod shadows the base path
by path. A virtual path is relative and forward-slashed, with no empty, `.` or `..` segment and no
backslash, colon or control character, so data cannot name a file outside a mount and a mod's
paths mean the same on any platform; every call throws `std::invalid_argument` for a path that
breaks the rules, rather than answering as if the file were absent. Mounts are checked as they are
added: unique names, read-only sources that exist, and at most one writable mount, which is last,
so a file written is the file read back. `Write` writes beside the target under a name no virtual
path can have (`:partial`) and renames it over, so a reader or a crash never sees half a save;
there is no fsync yet, which the save-game work in phase 8 decides. `Enumerate` is recursive,
sorted and deduplicated across mounts, and leaves out names no virtual path could reach. Mounts
are added at start-up; the reads are then safe from the I/O worker.

### 11. Resource manager · phase 3 · `engine/resource`

`ResourceHandle<T>` with a generation counter, asynchronous loading on the I/O worker, reference
counting, hot reload in development builds. Retires the "cache keyed on a model's address"
hazard the renderer works around today.
*Filled in* (phase 3), the I/O worker (`engine/jobs`): one thread, work run in order and
completions run in the same order by `Drain()` on the main thread. Work that throws does not take
the worker down; its completion is skipped and `Drain()` rethrows on the main thread, so a failure
is never silent. The destructor finishes the item in progress and drops the rest without running
their completions. The suite passes under ThreadSanitizer, built by hand, since no preset has it.
*Filled in* (phase 3), the manager: `ResourceHandle<T>` is `Core::Handle<T>`, a table per type.
A module registers a `Loader<T>`: `decode` on the worker, from the file's bytes, and an optional
`finish` on the main thread for what must happen there (a GPU upload). Each `Load` is paired with
one `Release`; loading a path again returns the same handle and counts a reference, and the last
release frees the slot under a new generation, so old handles go stale and a result still in
flight is dropped. A missing file or a throw from either stage fails the resource with the reason
kept (`ErrorOf`), and logs it. Hot reload, on in development builds, polls each loaded file's
resolved path, time and size from `Update()`, so a mod that shadows a base file counts as a
change; a reload keeps the handle and bumps `RevisionOf`, which a cache rebuilds on, and a reload
that fails keeps the last good value. Completions hold a weak pointer to the manager, so one
destroyed with work in flight drops the results. The scene's model cache moves onto it when the
scene leaves `render`.
*Filled in* (6a): models are a resource (`render/src/vulkan/ModelCache.h`), keyed on their virtual
paths, their buffers retired through a queue that waits out the frames in flight, so a scene reload
no longer idles the device; a model's rig is a resource of its own (`animation/RigLoader.h`), held
by the scene.

### 12. ECS, scheduler, events and FSM · phase 3 · `engine/ecs`

The tick phases (see [architecture](architecture.md#frame-and-tick-structure)), system
registration with declared reads and writes, ordering constraints, EnTT signals and dispatcher
for events, and a small `StateMachine<State, Event>` utility.
*Library*: EnTT, upgraded from v3.6.0 to the current release in phase 1.
*Filled in* (phase 3): the scheduler orders each phase by a stable topological sort, its `after`
constraints first and registration order otherwise, so the order is the same every run. A
system's `after` may name one registered later; a cycle, a duplicate name and an `after` across
phases are refused at the `Register` that makes them visible, and a name that never arrives is
refused by `Validate()`, which the app calls at start-up and `Run` calls the first time. Declared
access (`Access::Of<Reads...>().Writing<Writes...>()`) does not change the order today:
`Ambiguities(phase)` lists the conflicting pairs that no chain of constraints orders, the ones
registration order decides now and a parallel scheduler would have to keep apart. `StateMachine`
searches its tables in order, needing only `==`, refuses a second transition on one event from
one state, runs the exit hook then the entry hook (a self-transition runs both), and refuses a
`Dispatch` from inside a hook. Events inside a world, or inside presentation, are EnTT's
dispatcher as it is; the simulation's events to presentation are `sim`'s own queue, because one
queue keeps the order they were raised in across types. `kTickPhases` lists the phases in frame
order, and the shell's `Tick` is a run of each in turn: `shell.input`; nothing yet in commands;
`sim.step`; `sim.sync` and `sim.events`; `scene.update` and `shell.ui`; `render` and, after it,
`shell.smoke`. The scene's update (animation and world transforms) moved out of
`Renderer::Render` to be a presentation system, so the renderer only reads the scene. Each
system is a Tracy zone, and `sim.steps` is plotted.

### 13. Simulation · phase 3 · `engine/sim`

`SimWorld` (its own registry, fixed step, deterministic), `CommandQueue`, snapshots to
presentation, and the definition registry: typed, reflected definitions loaded from data and
overridable by mods. Determinism rules are in the
[coding standards](coding-standards.md#simulation-code).
*Filled in* (phase 3), the world: the game registers, in the same order every run, its
components, its commands and its simulation-phase systems. Components and commands are classes
registered with `reflect`; a command is a plain struct carried as a `meta_any`, so the queue, a
save and a recording all hold it the same way, and the virtual `Command` base of the stub is gone.
`Step()` checks every queued command has a handler before applying any, applies them in order,
runs the systems and advances the clock by the world's fixed step. The world's generator lives in
the registry's context, where systems reach it. `Save()` writes the clock, the generator, the
commands still queued, and the registry through EnTT's snapshot, which keeps the entity storage
as it was (packed order, free list, next identifier) and each component storage in packed order,
so a loaded world creates the same entities and its views visit them in the same order; counts
are checked against the bytes left before the loader reserves for them. `Load()` reads into a
fresh registry and commits only at the end, so a save that does not fit leaves the world as it
was. `StateHash()` is FNV-1a over the save. With recording on, applied commands are kept with
their step, and fed to a world from the same start they replay the session. The suite is the
phase's exit test, and passes under the address and undefined-behaviour sanitisers too.
*Filled in* (phase 3), snapshots and events: `Sim::Transform` is a position and an orientation
in double precision, the component physics will write; `Capture` takes every entity that has
one, sorted by identifier, and `Interpolate` runs from one snapshot to the next along the line
and the shorter arc, an entity only the later snapshot has standing where it is and one only the
earlier has having no transform. `Stepper` is the frame's simulation phase: the frame's real
seconds, scaled by the world's clock (paused, or compressed into more steps rather than longer
ones), go through the accumulator, and it keeps the states either side of the last step, so a
frame of many steps captures twice and presentation, a step behind, interpolates `Alpha()` of the
way. After a load, `Resync` makes both snapshots the loaded state. Events are raised with
`EventQueue::Emit`, from a command or a system, stamped with their step and kept in one queue in
raised order; the queue lives in the registry's context beside the generator, is not part of the
state (a save leaves it out and a load empties it), and presentation takes the lot each frame.
On the presentation side, a scene entity with an `App::SimProxyComponent` names a simulation
entity, and `SyncSimProxies` writes its interpolated transform into the scene, relative to an
origin in double precision before it becomes a float. The shell's world is empty until a game
registers into it.
*Moved* (6b): the proxy component and `SyncSimProxies` are the scene's (`scene/SystemScene.h`),
where the system scene's traffic layer makes and unmakes proxies from the snapshots.
*Filled in* (phase 3), the definition registry: a definition is one YAML file under a
directory, in `YamlArchive`'s form, named by its path under the directory without `.yaml`
(`military/corvette`). The VFS gives the topmost file at each path, so a mod overrides a
definition by shipping a file at the same path and adds one with a new path; the whole definition
is replaced, and finer layering is phase 11's. A load replaces the type's set only once every file
has read, so a bad file, refused as `FormatError` naming it, leaves the previous set. A definition
of another version is refused the same way until phase 8 decides whether definitions migrate. The
sets are ordered maps, so a walk over them is the same every run.

### 14. Input · phase 3 · `engine/input`

Devices (keyboard, mouse, gamepad, joystick/HOTAS through GLFW), actions with
button/axis/vector types, contexts pushed and popped (flight, map, UI, console), rebinding saved
to user settings.
*Filled in* (phase 3): inputs are GLFW's own numbers, so the module does not include GLFW; the
shell's `GlfwInput` polls every bound input once a frame and holds a sample of the codes to
GLFW's headers with `static_assert`. Actions are looked up by name once and queried by `ActionId`.
`Poll()` reads the context stack from the top: a context claims the inputs it binds, so a lower
context does not see them (the map's W pans while flight's W would throttle), and an opaque
context, the console, hides everything below. A button pressed and released between polls is
pressed for one frame. Bindings take a negative input (two keys make an axis), a scale and a dead
zone, and a vector's component. Defaults come from `Bind`; `Rebind` and `LoadBindings` override
them, and `SaveBindings` writes only the overrides, as text (`Key.W/Key.S`,
`y:GamepadAxis.LeftY*-1~0.15`) under `input.<context>.<action>`, so a default changed later
reaches a player who never touched it. The shell's F1 and Escape are actions in a `shell`
context. Joysticks that are not gamepads, a HOTAS among them, and the mouse wheel, which needs a
callback the ImGui backend owns, come with flight in phase 7; the shell does not load bindings
from the user's settings until settings are wired in phase 8.

### 15. Console · phase 3 · `engine/ui/imgui`

ImGui console: log view, command registry, console variables bound through reflection, history
and completion. The existing F1 demo window becomes the dev tools menu.
*Filled in* (phase 3), as the `devtools` module in `engine/ui/imgui`; the ImGui renderer, the
Vulkan side, stays in `render`. `Console` draws nothing and is tested as text: it captures the log
through a log callback, under its own lock since a message can come from any thread, and keeps the
most recent lines. A line is a command or a variable and its arguments, with double quotes for
spaces. `BindVariables(prefix, object)` makes each field carrying `Attribute::Console` a variable
(bools, integers parsed at their own width, floats, strings, registered enumerations by name),
and refuses a type it cannot set before binding any field. A command that throws prints its error
rather than ending the run. History keeps each line once in a row; Tab extends the first word as
far as the candidates agree. `DrawConsole` and `DrawDevTools` put it on screen, and their tests run
ImGui with no renderer, typing a line into the window and entering it. F1 opens the dev tools (the
console, the ImGui demo and metrics), and ` the console, whose opaque input context keeps the
keys typed into it from anything below; the smoke test renders all three windows.

### 16. Large-world coordinates and celestial · phase 4 · `engine/celestial`

System and local frames in double precision, the camera-relative conversion, `Orbit`
(Keplerian elements, on rails, evaluated at simulation time), planet rotation, and the system
description (bodies, stations, jump points) loaded from data.
*Stub*: `Frames.h`, `Orbit.h`, `StarSystem.h`.
*Filled in* (phase 4): an orbit's mean anomaly is taken from the clock with `fmod`, which is
exact, so a century-long clock loses nothing to its size, and Kepler's equation is solved by
Newton's method from a starting point that cannot overshoot (pi, for eccentricities of 0.8 and
up) with a fixed iteration limit; the position comes with its velocity. The astronomical frame is
turned onto the engine's Y-up axes. A body's `Rotation` is a sidereal period, an axial tilt and
a phase. `Resolve` is the one place a system's hierarchy is checked and completed (one primary
at the origin, parents listed first, eccentricities below 1, a period left out derived from the
parent's GM), and `ReadStarSystem` and `WriteStarSystem` are its YAML pair. `EvaluateSystem` and
`EvaluateBody` sum the hierarchy in the same order, so they agree to the bit, and a position is
pinned to its bits in Debug and Release. `PresentationTime` is the celestial clock's half on the
presentation side: bodies are evaluated at the time between two steps, not lerped between
positions. `assets/systems/halcyon.yaml` is the sample system.
*Moved* (6b): placing a scene's bodies (`PlaceCelestialBodies`, once the app's
`SyncCelestialBodies`) is the system scene's, in `scene`, which builds an entity for each body a
scene file does not present.
*Added* (6d): the galaxy (`Galaxy.h`), the jump-lane graph between the systems, read from
`systems/galaxy.yaml` and checked against each system's jump points. Routes over it are the AI's
(9a).

### 17. Render foundations · phase 4 · `engine/render`

VMA in place of raw `vkAllocateMemory`. Linear colour, an sRGB swapchain and HDR targets.
Reversed-Z infinite projection. Dynamic rendering and a declared pass list. Debug draw. A
renderer interface the rest of the engine sees, with Vulkan types kept inside
`render/vulkan`. Fills `IRenderer.h`, an empty placeholder today.
*Library*: Vulkan Memory Allocator.
*Filled in* (phase 4): everything above but the interface, which moved to phase 6 with the scene
(the shell still builds the backend's objects, and the scene loads models through the device).
The frame is a `PassList` of an opaque pass, into the multisampled target and resolved as it
ends, and an output pass to the swapchain with the UI over it; `Compile` derives every barrier
from the passes' declared uses, and a first use in a frame waits on the last use in the frame
before. Positions reach the GPU camera-relative: the scene's transforms are doubles, and
`ToRenderSpace` takes the camera off before anything becomes a float. `Camera.h` has the
reversed-Z infinite projection; `DebugDraw.h` the frame's lines, kept in double precision until
they are built; `Procedural.h` spheres and boxes for the scenes that need a planet.
*Filled in* (6a): the interface. `IRenderer.h` is the window, the frame of a `Scene::Scene`, the UI
drawn over it and what the frame cost; `CreateRenderer` makes the Vulkan backend behind it, whose
headers are private (`render/src/vulkan`) and whose Vulkan is a private library, so the shell
includes no Vulkan header.

This phase lays the ground the clustered forward frame
([architecture](architecture.md#rendering-clustered-forward)) stands on, so phase 5 adds passes
rather than restructuring. Compute passes are nodes in the pass list like graphics passes, with
the barriers between them declared. The HDR colour target is multisampled and resolved before
post-processing. Storage buffers are allocated per frame in flight, for the light list and the
cluster data phase 5 writes.

#### Shaders · phase 4

Phase 4 turned the one vertex/fragment pair and its one pipeline into programs by name: `mesh`,
`output` (the fullscreen pass from the HDR target) and `debug_line`, found through the asset VFS
and built by `PipelineCache` for each render state a pass asks for. `shaders/include` holds the
frame layout (`frame.glsl`, which `FrameData.h` mirrors and asserts), the mesh vertex inputs and
the colour helpers, and the build tracks each program's includes.

The plan was to turn the one pipeline into pipelines keyed by shader and render state, created
from the declared pass list. Shader programs are found by name through the asset tree (the VFS
from phase 3), compiled by `jettison_compile_shaders`, which already takes a list. Shared GLSL
(the frame UBO layout, the vertex inputs, colour-space helpers) moves into include files that
every program uses, so the layout the C++ side asserts (`sizeof(FrameUBO)`) has one source. The
first new programs are the debug-draw lines and a fullscreen pass for the HDR resolve.

### 18. Cameras · phase 4 · `engine/render`

Orbit (stations, planets), chase and cockpit (piloting), map (system and galaxy, logarithmic
zoom). Smoothing and transitions use the easing functions. The cinematic camera follows with
cinematics in phase 11.
*Filled in* (phase 4), in `CameraRigs.h`, with no device: each rig smooths what the player moves,
the same at any frame rate, and follows its target exactly, since a station at 7.7 km/s trailed
by a frame of smoothing would sit 130 m off centre. The chase camera lags its target's rotation,
never its speed; the map zooms by ratios from 1 km to 10^13 m; `CameraTransition` eases between
two poses, the second of which may move. A scene's camera can carry an orbit rig
(`OrbitCameraComponent`), which `UpdateOrbitCameras` runs after the world transforms.

### 19. Space rendering · phase 5 · `engine/render`

Clustered forward shading, as [architecture](architecture.md#rendering-clustered-forward)
describes. PBR, image-based lighting, the sky and tonemapping were done in 5b, the clustered
lights in 5c, the sun's cascaded shadows in 5d, and culling, instancing, levels of detail and the
frame's budget in 5e. Point and spot lights have finite ranges with windowed inverse-square
falloff, and are assigned to a grid of view-space clusters by compute: 32 × 18 × 48, tuned in 5e
against a station of 265 lights, and covering the cluster range. The directional sun is separate,
in the frame constants, with cascaded shadows. The opaque depth prepass was measured in 5e, and is
kept and off ([decided](roadmap.md#decided)). Also in this phase: PBR (glTF metallic-roughness) with image-based lighting
from per-system environment cubemaps, a starfield skybox, frustum and planet-horizon culling,
instancing, LOD selection and tonemapping. `LightComponent` has a type, a range and spot cone
angles, and the frame UBO's light stand-in is retired (5c). Debug draw has a lights-per-cluster
heat map (5c), and F4 the shadow cascades (5d). Done in 5e (`render/Culling.h`,
`render/DrawBatches.h`, `render/vulkan/GpuProfiler.h`): a draw is left out when its bounds are
outside the frustum or wholly behind a planet's horizon, and so is a light whose reach is; a caster
is drawn into a cascade only when its sphere reaches the prism the cascade's map shadows; the
procedural shapes have five levels of detail each, chosen by their error projected in pixels; draws
that share a model, a level and a material are one instanced draw, front to back; and each declared
pass is timed on the GPU and shows in Tracy. Not tuned in 5e: the shadow distance and split blend
(the maps' cost on hardware is 0.03 ms a frame, so their size stays 2048), and the cluster range.
Levels of detail have no hysteresis yet: a draw at a level's threshold can change level from one
frame to the next, and a moving camera has not yet been watched for it. A glTF model is one level; levels for model
files wait for a format that carries them.
The planets' own shadows (a station in eclipse, a moon's shadow on its planet) are drawn by the
cascades only where the cascades reach; past the shadow distance an analytic test against the
bodies would be needed, and is not planned yet. A light's
intensity driven by time, as the beacon's lamp blinks, waits for the material effects of 10c.
Materials are data: a shader, parameters and texture slots, with time-driven parameters for
effects.

### Shaders · phase 5

Phase 4 made programs found by name through the asset VFS (`mesh`, `output`, `debug_line`), built
by `PipelineCache` for each render state a pass asks for, with shared GLSL in `shaders/include`
([shaders, phase 4](#shaders--phase-4)). Phase 5 added the shaded set. In 5b: the
**metallic-roughness BRDF** (glTF's PBR model: GGX distribution, height-correlated Smith visibility,
Schlick Fresnel, image-based lighting from a prefiltered environment and a BRDF lookup table) in
`brdf.glsl`, under `lighting.glsl`, which every lit program shades through; `unlit`; `sky` and its
capture variant; the bake programs (`prefilter_specular`, `prefilter_irradiance`, `brdf_lut`); and
tonemapping in `output`. Every full-screen program's vertex stage is `fullscreen.vert`. In 5c: the
first compute programs, `cluster_bounds` and `cluster_lights`, over `clusters.glsl`, whose lookup
`lighting.glsl` uses; `PipelineCache` makes compute pipelines from `<program>.comp.spv`. In 5d:
`shadow_depth`, the mesh vertex stage built with `SHADOW_DEPTH` and no fragment stage, which
`PipelineCache` builds for a depth-only render state; the cascade selection in `shadows.glsl`, which
`lighting.glsl` shadows the sun through. In 5e: `depth_prepass`, the mesh vertex stage built with
`DEPTH_PREPASS`, which every variant matches through `invariant gl_Position`; and the instance
buffer (`mesh_vertex.glsl`), from which every mesh program reads its transform and skin by
`gl_InstanceIndex`. A material names its program and supplies its parameters and texture slots
(entry 19), so adding a program is data plus GLSL, not a new code path.

Shader variants (skinned and static, alpha-masked and opaque) are compile-time defines on one
source, not copies of it: `jettison_compile_shader_variant` builds one under its own program name,
as `mesh_triplanar` is `mesh` with `TRIPLANAR` (5a). Materials and textures hot-reload through the
resource manager since 5a; programs do not yet.

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
textures do); cube maps and texture arrays in files (5b's environment cubes are baked on the
device, from the procedural sky, and need neither); and normal mapping, which has the BRDF since
5b but still waits for tangent-space vertex data (`math/Tangents.h` generates it). A glTF model's
own material reads its base-colour image only, not its metallic-roughness factors and textures.
*Library*: KTX-Software (libktx) v4.4.2; stb_image stays for development.

### 21. Scene management and spatial index · phase 6 · `engine/scene`, `engine/spatial`

A scene per star system, built from the system description and the simulation state. Layers
within it. Jump transitions with asynchronous loading behind a transition effect. The spatial
index over simulation entities, an octree per system. The jump-lane graph lives in `celestial`;
the queries over it live in `ai`.
*Filled in* (phase 6): the scene left `render` for `engine/scene` (6a), naming no device, its
models by virtual path, its file at version 2. `SystemScene` builds a star system's scene around a
file or from the system alone, places the bodies, and keeps the traffic layer's proxies from the
simulation's snapshots; layers are an entity's own or its parent's, and a hidden one is neither
drawn nor lights the scene (6b). `SpatialIndex` is an octree, kept over a loose octree and a sorted
grid by measurement (6c, [decided](roadmap.md#decided)). Jumps (6d): an entity's system is
`Sim::InSystem`, a system scene's traffic is its own system's, and `JumpTransition` replaces the
scene shown behind a fade, reading the destination's files on the I/O worker, checking them before
anything is touched, and holding the fade until the new system has streamed in. The simulation
steps through a jump as through any frame; the stepper is not resynced, since the world did not
change.
Left for later: the index kept from the simulation (9b), a jump in the simulation (9a), a jump's
particles (10b), a destination prefetched before it is shown, and a body's look as data, all under
[carried forward](roadmap.md#carried-forward).
