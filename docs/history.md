# History

The finished part of the [roadmap](roadmap.md): phases 0–13 as they were closed, the work finished
since, and catalogue entries 1–31, the briefs for the systems those phases built, with what each
one became. Entry numbers are the roadmap's, so a reference to "entry 3" means the same system in
either document.
Work these phases moved to a later one is listed in the roadmap under
[carried forward](roadmap.md#carried-forward).

## Phases

Phase 1 was the last step before large-scale work: every system now has a home, a namespace, a
target and a test, and phases 2–11 fill stubs in rather than invent structure.

Phases 5–13 were split into lettered sub-phases (now in [history](history.md)), each a
block of related work that touches the same modules and documents, so it can be done in one session
and the context cleared after it.
A sub-phase names the catalogue entries that are its brief; read those, not the whole catalogue.
A phase is done when its last sub-phase meets the phase's exit criterion, so a reference to
"phase 7" in the code or the documents still means the whole phase. A sub-phase with no exit
criterion yet gets one when it starts.

| Phase | Work | Exit criterion |
|---|---|---|
| 0 | **Plan.** Scope, architecture, roadmap, standards. | These documents are reviewed and agreed. |
| 1 | **Layout and scaffolding.** Done 2026-11-10. The module tree, `jettison_add_module`, the existing code moved into `core`/`app`/`render`/`animation` (the scene parked in `render`, see [architecture](architecture.md#parked-code)), a stub for every in-scope module, doctest and CTest, `Jettison::Engine` and the consumer check, EnTT at v4.0.0. | Met: the gate is green, every module has a passing suite (25 CTest tests), and `source/test/consumer` links `Jettison::Engine` through FetchContent. The install export waits on its dependencies ([build and gates](build-and-gates.md#consuming-the-engine)). |
| 2 | **Core.** Done 2026-11-10. Assertions, logging, Tracy, configuration, string IDs, math additions, deterministic RNG, clocks, platform paths. `core` is filled in (logging over spdlog, assertions reported through it, string IDs and hashing pinned to reference values, layered configuration), and so are `math` (bounds and intersection tests in float and double, splines, easing, PCG32, noise and tangents), `profile` (Tracy zones, frame marks and plots), `time` (the real and simulation clocks, the fixed-step accumulator, the calendar) and `platform` (XDG paths, the executable path, and the start-up CPU check, five phases early). Two pieces moved to the phase that has a place for them: GPU zones to phase 4, with the pass list, and the app shell's move to `Log.h` to the smoke test's move. | Met: the math and RNG suites pass, the RNG checked against PCG32's published output and its distributions and the noise against recorded values. |
| 3 | **Runtime architecture.** Done 2026-11-11. Scheduler and tick phases, the simulation/presentation split with a command queue, events, the FSM utility, reflection, serialisation, VFS mounts, the resource manager, the definition registry, input actions, the console. Filled in are `reflect` (types, fields, enumerations and attributes over EnTT meta, with registration errors that hold in release builds), `serialise` (YAML and binary archives over it, each tested as a round trip and against data that does not fit), `vfs` (ordered mounts, shadowing, enumeration, and whole-file writes to the user mount), the `jobs` I/O worker, `resource` (handles, reference counts, loads on the worker, hot reload), `ecs` (the scheduler and the state machine), `sim` (the world, commands, saves, events, snapshots and the stepper that runs it from frame time, and the definition registry), `input` (actions, contexts and bindings) and the console and dev tools. The shell's frame is the scheduler's phases, and the scene shows the simulation through proxies interpolated between its last two steps. Three pieces moved to the phase that has a place for them: HOTAS axes and the mouse wheel to phase 7, with flight, and loading rebinds from the user's settings to phase 8, with persistence. | Met: a world stepped from a command stream, saved part-way and loaded, carries on to the same bytes as the original, and its state hash is pinned and the same in Debug, Release and under the sanitisers. The smoke test still renders, through the phases, with validation enabled. |
| 4 | **Coordinates and render foundations.** Done 2026-11-11. Double-precision positions, camera-relative rendering, reversed-Z infinite projection, the celestial clock and on-rails orbits, VMA, linear/HDR colour pipeline, a pass list with compute passes beside graphics ones, the MSAA HDR target and its resolve, per-frame storage buffers, debug draw, cameras (orbit, chase, cockpit, map). Filled in are `celestial` (frames, Kepler orbits evaluated on rails, body rotation, star systems read from YAML and resolved, and presentation time interpolated between steps) and, in `render`: VMA, with a leak reported at teardown; Vulkan 1.3, dynamic rendering and synchronization2; the declared pass list (`PassList.h`), its barriers derived at start-up; a 4× multisampled RGBA16F target resolved for the output pass; the frame constants, skin palette, light list and debug lines per frame in flight; pipelines by program and render state, with shared GLSL includes; scene transforms in double precision, made camera-relative before they become floats; reversed-Z with an infinite far plane; debug draw; the four camera rigs and eased transitions; procedural spheres and boxes; and the sun in the frame constants. Debug builds run synchronization validation. Two pieces moved: the renderer interface (`IRenderer.h`) to phase 6, with the scene's move out of `render`, since the scene still owns device models ([parked code](architecture.md#parked-code)); and the first compute program on the GPU to phase 5's cluster passes (compute nodes are planned and their barriers tested today). | Met: in `assets/scene/orbit.yaml` a station orbits an Earth-sized planet 150 million km from its star, at real scale. Taken through the engine's path and compared with a double-precision reference for ten seconds of frames at 1080p, the station is within 0.0002 px of where it should be and the planet within 0.004 px, following either one; a pipeline that makes positions floats first is 1,200 px out on the same frames. The smoke test walks that scene and the sample scene with validation, synchronization validation and VMA, and a leaked allocation fails it. |
| 5 | **Space rendering.** Done 2026-11-11. Sub-phases 5a–5e; catalogue entries 19 and 20, and the shader plan before entry 19. | The sample scene is a lit, shadowed station over a planet, its floodlights and beacons point and spot lights assigned to clusters, drawn within a frame budget measured on lavapipe and on hardware. The lights-per-cluster debug view shows the assignment. Met: `orbit.yaml`, measured by `--benchmark` (see [architecture](architecture.md#the-budget)): 0.30 ms of GPU time a frame on an RTX 5060 Ti at 1248 × 1358, against a budget of 4 ms, and 33 ms on lavapipe at 640 × 480, against 50 ms; with 265 lights on the station, 0.47 ms and 46 ms. F3 shows the assignment, which the smoke test checks every frame. |
| 5a | **Textures, materials and geometry.** Done 2026-11-11. KTX2 through `ktx_read` with BasisU transcoding (UASTC and ETC1S, to BC7 where the device has it and RGBA8 where not), as a `resource` loader with a GPU `finish` and a `retire` that frees a replaced image once no frame in flight reads it; stb_image and CPU mip generation, filtered in linear light, for development textures; the material system as data (`render/Material.h`: a material file names a program and sets its parameters, texture slots and time-driven parameters; a program descriptor beside its SPIR-V says what it takes); ico and cube spheres, rings and cylinders; a triplanar program built from the mesh shaders with a define. KTX-Software stays on v4.4.2 (see [decided](decisions.md)). | Met: `materials/hull.material.yaml` names `mesh_triplanar` and two KTX2 textures, loads through the VFS and the resource manager, and draws on the station in `orbit.yaml`; `panels.material.yaml` draws the same texture through texture coordinates in the sample scene. A texture rewritten on disk reloads through the resource manager in the render suite, and the smoke test's reload stage reloads every texture and material on the GPU, with validation and synchronization validation, in both scenes. |
| 5b | **PBR, image-based lighting and the sky.** Done 2026-11-11. glTF's metallic-roughness BRDF (`shaders/include/brdf.glsl`) under one lighting include (`lighting.glsl`) that adds the sun, the stand-in light list and the environment; a procedural sky per system (`SkyComponent`: a seeded starfield, the galaxy's band, a nebula) drawn behind the scene by `sky.glsl`, and baked by `Environment` into a cube map, GGX-prefiltered specular levels and an irradiance cube by filtered importance sampling, with the split sum's lookup table made once; the `unlit` program; Khronos PBR Neutral tonemapping in the output pass. The light's ambient colour is retired: a scene that sets one is refused, and the sky lights what the sun does not. | Met: `orbit.yaml`'s station is lit by Halcyon and by its sky, against the starfield, and the sample scene by its key light and a brighter sky. The render suite compiles `brdf.glsl` and `tonemap.glsl` as C++ and checks them against the glTF specification's formulas and a quadrature of the split sum (`scripts/brdf-reference.py`). The smoke test bakes each scene's environment at load and again on its reload, under validation and synchronization validation. |
| 5c | **Clustered lights.** Done 2026-11-11. `LightComponent` is a point or a spot with a range and, for a spot, `KHR_lights_punctual`'s inner and outer cone angles; the falloff is inverse square windowed to zero at the range, and a scene that leaves a range out is refused, while the one-light limit is gone. The grid (16 × 9 × 24, exponential slices from the near plane to a 2 km cluster range) is one GLM-compatible file, `clusters.glsl`, which the first compute programs (`cluster_bounds`, kept between frames until the projection changes, and `cluster_lights`, one invocation per cluster reserving its share of one index list by atomic add), the lighting include and the CPU reference in `render/Clusters.h` all compile. The pass list gains transfer passes, to zero the list's allocator and copy the grid out for the check. The frame UBO's light-list stand-in is retired. F3 shows the lights-per-cluster heat map, and F2 each light's reach. | Met: `orbit.yaml`'s station carries four hull floodlights, two dock lights, a beacon light, two navigation lights and two work lights, and the floodlights visibly light the hub through its clusters; the heat map shows them assigned. The headless render test reads every frame's assignment back and compares it with the CPU reference (21 to 24 frames a scene on lavapipe, none disagreeing), and fails the run on a disagreement, as it did with an assignment broken on purpose; the render suite checks the reference's bounds against the projection and its light tests against brute-force sampling. |
| 5d | **Sun shadows.** Done 2026-11-11. Four cascades of 2048² in one reversed-depth array image (`sun-shadows`, a pass at the head of the pass list that clears each layer and draws every mesh into it), split by the practical scheme (blend 0.8) between the near plane and a 400 m shadow distance. Each cascade covers the bounding sphere of its slice of the view, so its texel size depends only on the projection, and its centre is snapped to whole texels in the sun's frame measured from the camera's frame origin (`CameraView::frameOrigin`, the body an orbit camera circles), in double precision. `shadow_depth` is `mesh.vert` built with `SHADOW_DEPTH`, a vertex stage alone, drawn with depth clamp (now a required device feature) and a slope bias; the lighting include selects a cascade by view depth through `shadows.glsl`, which `render/Shadows.h` compiles as C++, offsets the receiver along its normal, takes nine filtered comparison taps, and blends into the next cascade across the last tenth of each. F4 shows the cascades. | Met: in `orbit.yaml` the hub shadows the collar and the collar the dock-side half of the hub, seen from the opening 160 m in the fourth cascade and from 45 m in the second (F4 shows the hub, one cylinder through the collar, darker under the sun alone on the collar's far side); with the camera still, two frames three seconds apart differ only in the windows' emission and the planet's limb while the station crosses some 100 km of the system. The render suite checks that a point fixed to a moving station lands at the same place within its texel to a thousandth of one as the camera orbits and turns (and that it slides by more than a twentieth when the grid is fixed to the system instead), that turning keeps every texel's size, and that every depth a cascade is selected for lands inside its map with the border to spare. The smoke test runs the cascades in both scenes under validation and synchronization validation, 36 s to 40 s on lavapipe. |
| 5e | **Culling, instancing, LOD and the budget.** Done 2026-11-11. Each draw's bounds in render space in double precision (`render/Culling.h`); meshes and lights outside the frustum or wholly behind a planet's horizon left out, and each cascade's casters culled exactly for a sphere against the prism its map shadows; the procedural shapes in five levels of detail with their geometric errors, the coarsest whose error projects under a pixel drawn; every draw instanced, those sharing a model, a level and a material batched into one and ordered front to back (`render/DrawBatches.h`), transforms in a per-frame instance buffer. The depth prepass is built, measured and kept, off (it is where SSAO's normals will come from); the grid is 32 × 18 × 48, from a light-budget scene of 265 lights (`scripts/make-light-budget-scene.py`); a CPU device gets RGBA8 rather than BC7 textures. GPU time per declared pass, from timestamps the engine reads itself and as Tracy GPU zones (`render/vulkan/GpuProfiler.h`); `--benchmark` reports them with the frame's counts. | Phase 5's exit criterion: met (row 5). In `orbit.yaml` twenty racked containers are one draw, the planet is out of every cascade it cannot shadow (the shadow pass from 15.5 ms to 4.2 ms on lavapipe), and on hardware the frame is 0.30 ms of GPU time. The render suite checks the frustum, horizon and caster tests against brute force, the levels' errors against their meshes, and the batching; a Tracy capture shows a GPU zone per pass. |
| 6 | **Star systems and galaxy.** Done 2026-11-11. Sub-phases 6a–6d; catalogue entries 21 and 16. | A jump between two systems unloads one and loads the other while the galaxy-level simulation keeps ticking. Met: row 6d. |
| 6a | **The scene leaves `render`.** Done 2026-11-11. `IRenderer.h` is the renderer the engine sees (the window, the frame of a scene, the UI, the frame's cost) and `CreateRenderer` the Vulkan backend behind it, its headers private under `render/src/vulkan` and Vulkan a private library. The scene is `engine/scene`: its components, its file, `SceneUpdateSystem` and the camera rigs, naming no device. The file is version 2, read and written through `YamlArchive` with one check shared by the loader and the writer, every path in it virtual; `serialise` learnt `std::optional` fields. Models are a resource keyed on their paths (`ModelCache`), retired after the frames in flight rather than behind an idle device; a model's rig is a resource of its own, read from the glTF file by `animation`, which moved to the World tier, below `scene`. | Met: `Application` holds `Render::IRenderer` alone and includes no Vulkan header; the sample and orbit scenes render as before (the Fox posed once its rig is in), and the smoke test, unchanged, passes both under validation. |
| 6b | **System scenes and layers.** Done 2026-11-11. `SystemScene` builds a star system's scene onto a `Scene`, around what a scene file authored or from the system alone (`Scene::LoadSystem`, `--system`): each body no entity of the file presents gets an entity looking as its kind does (a sun on the star, a sphere the body's size, a hull for a station, a ring marker in the effects layer for a jump point), a scene with no sky one seeded from the system's name, and one with no active camera an orbit camera on the first station. What it builds is a `BuiltComponent`, which the writer leaves out, so a saved scene is the file that was loaded. It places the bodies (the celestial sync moved from `app`), and makes the traffic layer from the simulation's snapshots: a proxy from a factory the game supplies for each entity in them, destroyed with its children when the entity leaves (`scene` now depends on `sim`, and reads its snapshots alone). Layers are a `layer` in the scene file, inherited by children; one hidden (`Scene::SetLayerVisible`, the dev tools' Scene panel) is neither drawn nor lit by. | Met: `halcyon.yaml` alone builds an entity for every body, each at its orbit's position at three times, the station moving about the planet at 7.7 km/s with the camera on it; the orbit scene builds only the gate it does not present, and both render, the orbit scene in the smoke test under validation. |
| 6c | **Spatial index.** Done 2026-11-11. `SpatialIndex` is an octree (`spatial/Octree.h`) over simulation entities as points in the system frame: sphere and box queries in identifier order, and the count nearest a point within a limit, by distance then identifier, every structure deciding membership with the same functions (`spatial/Queries.h`). Its root is a power of two metres across, so every node's bounds are exact in double; a leaf splits past 64 points and its subtree gathers back at half that; a point outside the root is held by the root. Looseness is a setting, at 1. A loose octree and a sorted grid were built and measured against each other (commit `64d5488` has both) and the octree kept (see [decided](decisions.md)); range results past 512 are radix sorted. | Met: the suite runs every query after inserts, steps, jumps across the system, removals with identifiers reused, a hundred points at one place and points past the root, loose and not, against brute force, and agrees to the last bit; the measurement is `tests/SpatialBenchmark.cpp`, recorded under [decided](decisions.md). |
| 6d | **Jumps.** Done 2026-11-11. The jump-lane graph is `celestial/Galaxy.h`: the systems by name and file (and a scene file where one is authored), the lanes between a jump point in each of two systems, travelled both ways, with a transit time, a jump point the end of one lane (see [decided](decisions.md)), each system's file checked against the lanes that end in it, and a system's exits in the galaxy's order; `systems/galaxy.yaml` joins Halcyon to two new systems, Vesper and Marrow, in a line. The simulation is the galaxy's: `Sim::InSystem` says which system's frame an entity's `Transform` is in (the system's name hashed, `kNoSystem` on a lane), snapshots carry it, no interpolation crosses a jump, and a system scene's traffic layer shows its own system's entities alone. `JumpTransition` (`scene/JumpTransition.h`) jumps behind a fade in the output pass, under the UI: the destination's files read on the I/O worker as the fade rises and parsed on the main thread (see [decided](decisions.md)), a destination that fails abandoned with the fade lifting on the system left, the scene replaced once the fade covers the frame, and the fade held until nothing is loading for the first time (`ResourceManager::Loading`) or for 8 s. `SceneFile` is a scene file read and checked before it replaces anything. The shell reads the galaxy (`--galaxy`), jumps with J, the console's `jump` and the Scene panel, and frames the jump point it arrives at; the smoke sequence gains a jump stage. | Met: the scene suite runs a galaxy's simulation with ships in Halcyon, in Vesper and on the lane between them, one step every frame, and jumps presentation from Halcyon to Vesper and back. Vesper's bodies and traffic replace Halcyon's, among them the courier that came out of the lane while the jump was under way, at the gate, and Halcyon's ships are where every step put them. The smoke test jumps once in each scene, under validation and synchronization validation, the simulation running 94 to 105 steps through each jump: 1.6 to 1.8 s on lavapipe, 0.2 to 0.4 s of it streaming the new system in. |
| 7 | **Physics and flight.** Done 2026-11-11. Sub-phases 7a–7c; catalogue entry 22. | A piloted ship collides with a station, docks through a sensor, and is picked with the mouse. Met: row 7c. |
| 7a | **Jolt in the simulation.** Done 2026-11-11. Jolt in double precision and `CROSS_PLATFORM_DETERMINISTIC`, out of the planned set and linked privately by `physics`, with assertions in Debug trees through the engine's and its profiler zones as Tracy's. `PhysicsWorld` is a service the simulation world owns (`Sim::WorldService`, saved after the components and loaded in two halves; the world's save is version 2): a body is an entity with a `Physics::Body`, a `Sim::Transform` and a `Sim::InSystem`, static, kinematic (moved to its `Transform`, as a station on its orbit) or dynamic (its `Transform` and `Physics::Velocity` written back, and what the game writes over them taken); shapes are definitions by name (box, sphere, capsule, cylinder, convex hull, mesh), built when defined; a Jolt world per star system with bodies, stepped in the fixed step on one thread; body identifiers chosen by the engine, so a loaded world makes the same ones; a save holds the bodies' records and Jolt's state, contacts included. | Met: the physics suite's recorded run, a closed bay in Halcyon with a turning paddle and cargo of every shape thrown about it, two crates meeting in Vesper, and every way a body changes, keeps every body in the bay and is pinned to state hash `0x5b883fdf0f57a951` in Debug, Release and under ASan and UBSan; saved part-way and loaded, it steps on to the same bytes, and its recorded commands replay it. The gate now runs the Release suites, and with `--asan` the `sim` and `physics` suites, against those values. |
| 7b | **Flight.** Done 2026-11-11. Joysticks' raw axes and buttons as input devices, each by its GLFW slot (`Joystick2.Axis2`), so a HOTAS's stick, throttle and pedals bind apart, and the mouse wheel through the one callback, chained to ImGui's. Physics in frames: a Jolt world per frame of each system, the system's own or a station's or planet's on rails (`Physics::LocalFrame`), since in the star's frame Jolt never sweeps a ship against a station sharing its 30 km/s ([decided](decisions.md)); star systems defined in physics, for rails (`Physics::OnRails`, a station's kinematic body on its orbit) and gravity on dynamic bodies; `continuous` bodies swept against what they meet; kinematic bodies' velocities written. The flight model (`physics/Flight.h`): `Thrusters` as accelerations, `FlightControls` set by the `SetFlightControls` command, with assist (speeds and rates held, relative to a reference) or without (accelerations); a system `flight` before `physics`. The scene's `PilotCameraComponent` puts the chase and cockpit rigs on the ship's interpolated proxy. The shell flies a ship beside Highport, with a collision mesh made from the shapes the station is drawn with, through a `flight` input context bound for keyboard, gamepad and HOTAS; the smoke test pilots it from behind and from the seat. | Met: the physics suite flies a ship at 60 m/s into Highport's hub, both in the station's frame as it goes round its orbit: the ship meets the hub's end and comes no nearer than its own half-width, slides across it pushing ahead, and is brought to rest by its assist when let go; the run is pinned to state hash `0xce15aba2a75c5a79` in Debug, Release and under ASan and UBSan, carries on to the same bytes saved in contact and loaded, and its four recorded commands replay it. A ball fired at 240 m/s at a panel 15 cm thick passes through it placed and stops swept, and a ship let drift beside the station for a minute stays within 5 mm (1.8 m in the star's frame, as a float velocity rounds). The app suite flies the same controls from keys, gamepad axes and buttons, and a HOTAS's stick and throttle; the shell, flown headless with xdotool, put its ship against Highport's dock and held it there. The bay's run in the 7a row is re-pinned to `0xa3a1ff1e793d1be1`, since a save now holds frames and the new components. |
| 7c | **Sensors, contacts and queries.** Done 2026-11-11. A sensor is a `Physics::Body` marked `sensor`: it meets nothing, sees dynamic bodies enter and leave it, never sleeps, so a ship come to rest in it is still seen (a static sensor is kinematic to Jolt, which a static one would not do), and is placed rather than swept. `Physics::OnRails` gains an offset and a turn, a place on its body, which is how a docking zone rides before a station's dock. Contacts are the simulation's (see [decided](decisions.md)): `PhysicsWorld::Contacts` is what the last step began and ended, in order of pair, a beginning with the point, the normal and the speed the two closed at, and `Touching` is what touches a body now, both saved (the physics layout is 3), so a system before `physics` reads after a load what it read before the save; a pair touches while Jolt's cache holds a contact between them, while one was added this step, or while both sleep. `CastRay` and `CastShape` find the first body along a ray or a swept convex shape across every frame of a system, sensors and the caster's own body left out unless asked for, with the frames placed at a time of the caller's (`CastOptions::framesAt`). Picking is a command answered by an event (see [decided](decisions.md)): `Physics::Pick` is a ray presentation drew from what it shows, cast at the time it showed, and `Physics::Picked` the answer; `Render::ScreenRay` is the ray through a point of the window. The shell gives the station a docking port, a sensor 40 m on a side before its dock, and docks a ship moving under 2 m/s in it (`app/src/Docking.h`: `DockingPort`, `Docked`, the system `shell.docking` after `physics`, and `ShipDocked` and `ShipUndocked` events); a left click picks, and the hit is marked and named. The smoke sequence gains a pick stage. | Phase 7's exit criterion: met (row 7). The physics suite's exit run flies a ship at 60 m/s through Highport's docking zone into its hub as the station goes round its orbit: the zone sees it enter and pushes nothing; the hub's contact begins at 60.0 m/s on the hub's end, its normal from the station towards the ship, and ends two steps on as the ship bounces; brought to rest by its assist where it slid, the ship is still in the zone; and a pick drawn from 30 m behind it, half a step behind the simulation as presentation shows it, finds it. The run is pinned to state hash `0x31230f3312efb2be` in Debug, Release and under ASan and UBSan, carries on to the same bytes and the same contacts saved mid-impact and loaded, and its five recorded commands, the pick among them, replay it. The suite checks sensors static and kinematic, a body asleep against a block and in a zone, bodies removed and made again, rays and shapes cast across frames, and a ray drawn at the time shown, which misses the station without its frames placed then. The app suite docks a ship slowed in a port and undocks it as it backs out or as the port goes; in the shell, driven headless with xdotool, a click picked Highport at 143 m and the ship at 280 m, and the ship, flown into the port and let go, docked, picked the dock from behind at 56 m, and undocked as it backed out. The bay's run in the 7a row is re-pinned to `0x4895fc9865cd3dc6`, since a save now holds contacts and the new fields. |
| 8 | **UI, audio and persistence.** Done 2026-11-12. Sub-phases 8a–8c; catalogue entries 23, 24 and 25. | A market screen bound to simulation data; a save written by version N loads in N+1. Met: rows 8a and 8b, and the sound of both in 8c. |
| 8a | **Persistence.** Done 2026-11-12. Migrations as a chain of steps per format (`serialise/Migration.h`), each from one version to the next, run by the readers given one: a binary step edits the archive as a document (its table of names, through which a name is renamed everywhere, and its payload), a YAML step the parsed tree, its lines kept for errors; a version newer than the build's or one the chain does not reach is refused by name. The world's save migrates from version 1 (before services) to 2. A save game (`sim/SaveGame.h`, `Jettison.Save` version 1): its info (the name, the engine version, when, the world's clock, the galaxy's file and the system shown), the world's save and the game's sections, each part versioned on its own. Definitions migrate ([decided](decisions.md)). Every whole-file write, the VFS's and the settings file's, is durable (`Core::WriteFileDurably`: fsync, rename, the directory fsynced). The shell's settings and bindings in the XDG configuration directory, apart from saves in the data directory, which is mounted writable over the base; a joystick a binding names is named after its device by GUID (`input/Joysticks.h`); `bind`, `unbind`, `save`, `load` and `saves` in the console, and F5 and F9; the shell's ship in a section of its own; the smoke sequence gains a save stage. | Met: the sim suite loads a world saved by the phase 6d build at version 1 (`world-v1.save`, commit `a5d0c83`), through its migration, into exactly the save this build makes at the same step, which steps on to the pinned hash of the whole run; `save-v1.save`, the save game's first version, is kept for every later build to load. The app suite rebinds an action and a joystick, restarts on the settings file and finds both, and puts one back and finds that; in the shell, run headless twice on one user directory, a key rebound in the console in the first run jumped in the second. The smoke test saves and loads in both scenes, each loading back to the state hash saved, under validation. |
| 8b | **UI.** Done 2026-11-12. RmlUi behind `ui/UiSystem.h`, with no RmlUi type in its interface: documents, stylesheets, templates, fonts and images read through the VFS, its messages through the log under `ui`, counted. It draws into `Render::Overlay` (`render/Overlay.h`), a plain draw list the renderer draws in the output pass after the tonemap and under the dev tools, with the `overlay` program, uploading a texture as a draw first names it and releasing it once the overlay lets it go (`vulkan/OverlayRenderer.h`; the texture upload shared with materials, `vulkan/TextureUpload.h`). FreeType in every tree, linked by RmlUi's font engine and by ImGui's rasteriser (`imgui_freetype`). Data models (`ui/DataModel.h`) bind any type registered with Reflect, read in place through the field addresses `Reflect::TypeBuilder::Field` now records (`Reflect::AddressOf`), and events that become commands; snapshots carry the components a world shows (`SimWorld::ShowInSnapshots`, `Snapshot::Components`, `Snapshot::Get`). The shell feeds the UI the window's events in order, from GLFW callbacks chained over ImGui's, and a click on a panel is not a pick. The sample game's first slice, from the Civilisation Simulation's plan ([sample game](sample-game.md)): eight goods and two markets as definitions, prices by scarcity and a listing fee, market ticks, the ship's hold and credits, and trading when docked (`app/src/Trading.h`); the market screen (M) and the saved-games screen (L, the load screen carried from 8a); the smoke sequence gains a trade stage. | Met: the market screen shows the market of the station the ship is at, made from the simulation's snapshot and never from the world, and its Buy sends a trade the step answers, which the screen shows: in the app suite, through `ui/market.rml` itself, with a mouse click a single trade; in the render test's orbit scene under validation, its station and stock checked against the snapshot and the refusal of an undocked ship shown; and in the shell on Xvfb, flown into Ostra Highport's port, docked, and bought from by mouse. The UI suite holds RmlUi to the VFS (a mod's stylesheet shadows the base's), to the overlay, and to the data model; the render test fails on any warning RmlUi logs. |
| 8c | **Audio.** Done 2026-11-12. miniaudio out of the planned set, linked privately by `audio`. `Audio::AudioSystem` (`audio/AudioSystem.h`, [architecture](architecture.md#audio)): sounds read through the VFS, so a mod's mount shadows one, a short sound decoded whole as it starts and music streamed from FLAC a page at a time; five buses as miniaudio sound groups, master over music, effects, UI and voice, their volumes in the settings (`audio.<bus>`) and set by the console's `volume`; positioned sources with inverse falloff, placed relative to the listener in float each frame as rendering is camera-relative, the listener the active camera; output to the device, to miniaudio's null backend (when no device opens, with `--no-sound`, and in the headless run), or to none, mixed on the caller's thread (`Mix`) the same every run, for the tests. Simulation events map to sounds as data: `Audio::EventSound` definitions under `defs/sounds` name an event type by its Reflect name, a sound, a bus and the event's entity field it is placed at, checked against Reflect as `Audio::EventSounds` is made; the shell's docking and trading events are registered by name, and the events about the player's ship are played, placed where presentation shows their entity. `Audio::SoundSources` plays the presentation world's `SoundComponent`s from their world transforms. The sample game's sounds, synthesised by `scripts/make-sample-sounds.py` so nothing in them is anyone else's: a trade filled and refused on the UI bus, the clamps closing and letting go on the effects bus, the station's hum, and *Drift*, 32 s of music in a loop (1.2 MB of FLAC); `.flac` goes through Git LFS. | Met: the app suite docks a ship in a port and undocks it, and plays the steps' `ShipDocked` and `ShipUndocked` through the shipped definitions, each once on the effects bus and heard from the station's side. The audio suite streams the music past its end and loops it, silences a bus, places a sound by side and by distance (a tenth of the gain at ten times the minimum distance) and identically with the listener 1.4 × 10¹¹ m from the origin, refuses a definition naming an event or a field that is not there, and starts, moves and stops a presentation entity's sound. In the render test, under ASan, the trade stage's answer plays on the UI bus through the null backend, and the run fails on any warning from the audio or any sound that would not play. In the shell on the desktop, through the machine's own device, the music, the station's hum, both trade answers, and the clamps closing and letting go were all heard. |
| 9 | **AI and docks.** Done 2026-11-12. Sub-phases 9a–9f; catalogue entries 26 and 31. | Automated traders choose trades, plot routes across systems and fly them without collisions, docking on the pads their stations give them while their cargo is carried across the deck. Met: row 9f. |
| 9a | **Pathfinding.** Done 2026-11-12. A* (`ai/Pathfinding.h`): a consistent estimate or none, the open nodes expanded by estimate and then index, a way in replaced only by a cheaper one, so of equal routes the same one every run; a graph, a cost or an estimate that is not one refused. Routes across the galaxy (`ai/Routes.h`, `PlotRoute`) over its jump points: a lane joins its ends at its transit time, and a system's jump points each other at a crossing cost the caller gives, so two jumps are weighed against one and a long crossing. Courses across a system (`Waypoints`, `PlotCourse`): a ring of six waypoints about each star, planet and moon, legs joined where they clear every body by a clearance, an end near a body given its own way out, searched with the straight-line distance; `CrossingByCourse` makes a route's crossings from them. The simulation's jump (`ai/Lanes.h`, `LaneTravel`): `EnterLane` sends an entity along the lane through a jump point of its system, in none for the transit in whole steps, and the system `lanes`, after physics, brings it out at the far gate at rest, facing the way the gate faces (`Celestial::JumpPointFacing`: its planet, with its station before it), in the frame of the station the gate keeps by; events at both ends and a refusal saying why. `ai` depends on `celestial` and `physics`. The sample gates moved beside their stations (5 and 6 km, beyond each station from its planet, orbiting it with its period), and their rings face the way a ship leaves them. In the shell, a jump along a lane takes the ship (flown again as it comes out), binds it to the arrival system's station, and the console's `route` plots the way from the ship to a system. | Met: in the AI suite, A* against a graph's known answer, a grid's known detour with and without an estimate, and against Floyd and Warshall over every pair of twenty random graphs, each path walked and found the same twice; ties broken the same in either adjacency order; routes across the sample galaxy end to end and back (105 s, two jumps), a shortcut weighed against a slow crossing, crossings asked the same every run; courses round a planet at the known length over its ring and no shorter than the sphere's own geodesic, kept off by a clearance, out from under a ring's face, from Ostra Highport to Pell round Ostra across an orbit; a ship entering Halcyon Gate, out of physics for the 2,400 steps of its 40 s, out at Vesper Gate at rest in Corva Refinery's frame looking at it 5 km off, and a world saved mid-transit, and a replay, arriving with the same state hash. |
| 9b | **Steering.** Done 2026-11-12. Behaviours (`ai/Steering.h`) that are velocities, which flight assist holds: `Seek` at full speed, `Arrive` at the speed it can still stop from (√(2ad), the last metres at the distance a second), `Avoid` turning a ship aside from the first obstacle it would pass within its clearance of, at the speed that makes up the shortfall in time, and never closing on any faster than it can stop before the clearance, and `Separate` pushing apart ships nearer than their separation. The simulation's steering (`Ai::Steering`, the system `steering`, after `spatial` and before `flight`): an `Ai::Destination` (a point in the ship's frame, or an offset from an entity in its axes, with a course's waypoints), an `Ai::Steered` (lookahead, clearance, separation), commands to set and clear one, and events as a ship arrives and as a destination is lost; obstacles are every body but a sensor within reach in the ship's frame, each the sphere of its shape (`PhysicsWorld::ShapeRadius`), and its controls the velocity in its axes and a turn of its nose towards it. The spatial index kept from the simulation (`spatial/SystemIndices.h`, the system `spatial`): an octree a system, from the components each step, not state; `spatial` depends on `sim`. The scheduler's `before`, so a module orders itself ahead of another's system. In the shell, the console's `traffic` sends up to sixteen shuttles to places on a ring about the station, and the smoke run sends four. | Met: in the AI suite, arrive stopping a point mass from 2 km without passing the target by a centimetre, avoidance's turn, its braking before and within a clearance and two ships head on turning their own ways, a point mass flown at a station's centre going round it 10 m clear and on; through physics, a ship arriving at rest by Ostra Highport facing its way, one whose way is through the station flying round it more than 100 m from its centre, a course's waypoints passed in order, a destination by the station turned with it and lost with it; twelve ships from 1 to 2 km all round the station to places on a ring 250 m about it, their ways crossing at it, all arriving in 34.5 s with no contact begun, their hulls 31.8 m apart at the nearest and 25 m from the station's sphere; the same flight from the same start, and from a save part-way, to the same state hash, pinned in Debug and checked in Release and under the sanitisers, whose gate now runs the `spatial` and `ai` suites too. |
| 9c | **Docks.** Done 2026-11-12. Catalogue entry 31. The module `dock`, a simulation module above `ai` and `physics`. Definitions (`dock/Definitions.h`): a hull (`defs/hulls`) with its length, beam and height, within its pad class's largest hull, its class and its cargo hatch, colliding as its box; a station layout (`defs/stations`), named by a station in its system's file (`Celestial::Body::layout`), with its decks and piers as boxes, which the station collides as (`DeckShape`), its pads (class, place, normal, heading, and for display or not) and its cargo bays, checked as it is defined: every pad all on a deck or a pier, none overlapping, nothing of the station over any out to its sphere. The simulation's (`dock/Docks.h`): `MakeStation` builds a station's kinematic body on its rails and an entity for each pad on the same rails, free, reserved until a step, or holding a ship; `RequestPad` and `ReleasePad`, in command order, give the smallest free pad a hull fits, the lowest numbered first, or a place in a first come, first served queue served from the front as pads come free; a reservation lapses after the layout's seconds; one claim a ship; a pad clamps only its own ship; each command carries its sender (`Sim::Controller`) and is refused from one that does not control the ship (`Sim::ControlledBy`); `Park` puts a ship on a pad as a game starts one. The approach: a course round the station's sphere to the point above the pad, then down its normal kept clear by the hull (steering's `Destination::byHull`, the ship's shape cast along its way) and turned to the pad's heading (`Destination::facing`), clamped within reach as a kinematic body on the station's rails ([decided](decisions.md)); undocking lifts it back to the point above, where it gives the pad up. `PhysicsWorld::RailsPlace`. In the shell, `app/src/Docking.h` is gone: every station naming a layout is built at the start with three ships parked on it, the ship starts docked on Highport's pad 1 and trades there, the console's `undock` lifts it and `dock [manual]` asks for a pad; the layouts are drawn plainly (`app/src/Stations.h`), and the system scene no longer draws a laid-out station as its box. Ostra Highport is 1.5 km across, twelve L pads on four piers, and Corva Refinery 600 m, four M and two S; the stations' radii in their system files are their layouts' spheres, 763 and 303 m, and the gates are where they were. The shell's save section is version 2, without the port, and saves from before 9c are refused ([decided](decisions.md)). | Met: in the dock suite, two shuttles asking in one step given pads 2 and 1 in command order, the other way round when asked the other way, and the same every run; a hull that fits only an L pad given the L and a second queued while both M pads stay free; a queue of three served in order as pads come free, a shuttle given an S pad past a hull waiting for an M; a reservation lapsing at its 120th step, two seconds, and its pad given to the shuttle waiting behind; a display pad never given, and refused to `Park`; a shuttle from 10 km on Highport's far side over its pad in 111 s and clamped 24 s later, with no contact on the way, and lifted clear by undocking with the one contact of its hull on its own pad as it is let go; and the approach saved on the way down and replayed to the same state hash, pinned in Debug at `0x51f97f0abd04afc2` and checked in Release and under the sanitisers, whose gate now runs the dock suite too. Steering's flight is unchanged, still 9b's hash with the new fields unsaved, and is re-pinned at `0x2408699817714f70` with them. |
| 9d | **Station dressing.** Done 2026-11-12. The layout drawn, in the shell (`app/src/Dressing.h`, `DressStation`), under the station's entity as each system scene is built, and none of it the station's collision mesh, which stays its layout's decks and piers: a tall block (the hub) in the hull's plating with its windows and a flat one (a pier, a wing) in deck plating (`materials/deck.material.yaml`); each pad a plate on the deck painted, in the effects layer, with its outline, a centre line with chevrons to the bow, the keep-clear box over its class's hatch with its diagonals, and its number in bar segments, read bow up; floodlight poles along each pad's long sides, at its corners and every 120 m between, each a spot aimed across the pad at half its class's largest hull's height, at the intensity that puts 1.6 (half the sun's irradiance) there, and a lamp under its head in the effects layer for the pad's state (green free, amber reserved, red holding, blue on display), turned from the snapshots, which the docks now show their pads in; each cargo bay's door, a panel up from its sill or a hatch in the deck, a bay now being the middle of its sill ([decided](decisions.md)). Proxies for each hull class from procedural shapes fitted to the hull's box (the shuttle, a transport of cargo pods behind a cab, the largest transport's racked containers under a bridge, a long hull under a superstructure for XL), and the hull's glTF model in their place where it names one (`HullDefinition::model`). Highport's display wing, a deck in the quarter between its +X and +Z piers: XL bays with a liner and a frigate (`defs/hulls/liner`, `frigate`), and four fighters on S pads beside them, each named by the layout (`PadLayout::ship`, only on a display pad) and drawn, not a body ([decided](decisions.md)). The hand-built Highport of `orbit.yaml` replaced by the layout's. The renderer leaves out a draw under a pixel across (`Render::TooSmallToSee`), and a CPU device's material sampler takes no anisotropy ([decided](decisions.md)). | Met: in the app suite, Highport dressed with every deck and pier at its place in its plating, each pad turned to its heading with a plate of its class's size, 410 strokes of paint and 104 lamps in the effects layer, and 104 floodlights, six an L pad, eight an XL and four an S, every one reaching no further than 900 m from the station's centre, so within the 2 km cluster range of a camera anywhere within 1.1 km of it; the display wing's liner, frigate and four fighters each on its own pad and none on a pad a ship lands on; a pad's lamps green, red with its parked transport, blue on display and amber once a snapshot carries it reserved; and every class's proxy inside its hull's box and filling it. In the dock suite, Highport's eighteen pads and their shown hulls, and a ship shown on a pad that is not for display refused; the approach is unchanged in its times, 111 s and 23.75 s, and re-pinned at `0xac920de90ae724ff` for the six pads and the deck more. Measured from the orbit camera's 1.5 km, where 103 of the 104 are listed and one is past the cluster range: on the RTX 5060 Ti 0.32 ms a frame (0.53 ms from 1.1 km, 0.66 ms from 320 m over the hub), and on lavapipe at 640 × 480 33 ms, 9c's, after the two renderer changes took it down from 84 ms ([architecture](architecture.md#the-budget)). The smoke runs' cluster check agrees in every frame of the orbit scene, its busiest using 80,937 of the index list's 884,736 entries and 28 lights in one cluster. |
| 9e | **Deck traffic.** Done 2026-11-12. Cargo carried across the deck in the simulation ([decided](decisions.md)). A station's roads from its layout (`dock/Roads.h`, `Dock::Roads`), made as it is defined and never state: a road down each pier's side, in the wider strip of its top beside its pads; each pad's apron, the middle of its aft edge, and its hatch, on its centre line where the dressing's keep-clear box is (`Dock::HatchAft`); straight legs joining each bay and apron to every road at its nearest point, and each bay to each apron, wherever the leg is on the deck, through nothing of the station and across no pad; each pad routed once by A* (9a) from the bay nearest it by road, and a layout with bays refused if a pad ships land on is not reached. A layout's deck traffic (`Dock::DeckTraffic`): buggies a pad, crates a trip, speed, and working or parked. A trade becomes a transfer: the shell's `Trade` settles its crates and price at once, as before, and calls `Dock::Carry`, which gives the docked ship a `Dock::Transfer` of crates to go aboard and to come off, a purchase and a sale of the same crates netted; the docks' step 5 (`dock/Deck.cpp`) sends a `Dock::Buggy` out of the pad's bay while the ship has cargo for one and the pad has fewer out than its buggies, an entity with a Transform on the station's rails and no body, driven along its route at its speed on the right of the road, a buggy's gap behind the one ahead in its lane, holding a node within 6 m of it while the next waits short of it, in entity order; at the hatch it hands its crates aboard and takes a load off, at the bay it leaves its load and goes out again or in. The ship is held until every crate for it is aboard and every one off it has been taken from its hatch (`Dock::CargoCarried`), and `ReleasePad` refuses it until then (`Dock::ToCarry`). Parked buggies are the dressing's, across each pad's stern, and their station's cargo moves a buggy's load each way at each round trip of the pad's road shared among them; a station with no bays has its cargo at once. Buggies and transfers saved and shown in snapshots. In the shell: each buggy drawn from the snapshots, interpolated, a flatbed tug with its crates on its bed (`MakeBuggyProxy`, `ShowBuggyLoads`); the roads' lane dividers painted; the market screen's status saying how many crates are crossing; the console's `trade <good> <crates>`; Highport's buggies three a pad and Corva's two, four crates a trip at 8 m/s. | Met: in the dock suite, Highport's roads (44 nodes, 44 edges, every edge at least 12 m) with each pad's bay and its way to it, 90.6, 396.8 and 606.8 m out along each pier, and Corva's; a layout whose pad no bay reaches refused; a shuttle's twenty crates aboard Highport's pad 1 in 45.4 s with `undock` refused on the way, three buggies out at most, every buggy on the deck and none nearer another than the 4 m between lanes, and its buggies back in the bay after; twenty crates off pad 2 in 160.2 s; every one of Highport's twelve pads loading or unloading forty crates at once, all done in 537.8 s with no buggy nearer another than 4 m; parked buggies moving twenty crates in 2,925 steps, five loads at 585 steps, the round trip of the 117 m road at 8 m/s shared by three; a station with no bays done in a step; a ship gone mid-transfer, its buggies back in; crates bought and sold back netted; and a transfer of two ships saved part-way and replayed to the same state hash, pinned in Debug at `0x428e1325bd13a2c9` and checked in Release and under the sanitisers. The approach's hash re-pinned at `0x8ace2f180f55bb5c` for the new empty storages alone, 9d's without them. In the app suite, Highport's 32 road lines among its paint, a buggy's proxy within its 4 × 2.4 m footprint carrying the crates its snapshot has, and 36 parked buggies across the sterns of a parked Highport's twelve pads. |
| 9f | **Decisions.** Done 2026-11-12. Catalogue entry 26. Utility AI (`ai/UtilityAi.h`): response curves (linear, power, logistic and a step, clamped, and inverted on asking), considerations over an input's range, an action's score the product of its considerations made up for their number, times its weight, and decisions as data (`defs/decisions`, `Ai::DecisionDefinition`), checked as they are defined; `Ai::Choose` scores the game's options in order against the best so far, asking an option for no more of its inputs once it cannot win, the earliest of equals first and none when every one scores nothing. Behaviour trees (`ai/BehaviourTree.h`): trees as data (`defs/behaviours`, `Ai::BehaviourDefinition`) of sequences, selectors, inverts, succeeds, repeats, loops, waits in whole steps and the game's tasks by name with arguments, checked as they are defined against the tasks defined (`Ai::Behaviours::DefineTask`); an entity's place in its tree (`Ai::Behaviour`), a memory and a flag a node, saved; the system `behaviour`, first in the step, before `spatial`, ticking every tree in entity order, its composites resuming their running child, nothing interrupting it, and a repeat's or a loop's next run at the next tick ([decided](decisions.md)). A task's commands applied at once and not recorded (`Sim::SimWorld::Apply`, [decided](decisions.md)). A ship comes out of a gate clear of every body there and of what came out before it in the step, on rings across the gate a hull's breadth and 20 m apart, for which `LaneTravel::Attach` takes the physics world. In the shell (`app/src/Traders.h`): seven traders, five transports and two shuttles parked at Highport and Corva at the start, each commanded by the AI (`App::TraderController`), following `defs/behaviours/trader.yaml` and choosing their runs by `defs/decisions/trade.yaml`, on the margin, the rivals after the same shelf and the credits a second over the route (`Ai::PlotRoute` by course); their tasks lift off a pad, fly round the station to the gate, enter its lane within 150 m of its centre, ask for a pad and wait for one held off the station in their place in the queue, trade, and wait while the cargo crosses the deck. The console's `traders` reports them from the snapshots. | Met, and phase 9's: in the app suite, the seven traders over twenty minutes sell fourteen runs across the lane, every trader at least one at a profit, in 28 crossings; they queue five times at Corva's one free pad and are each served; they begin no contact but a hull's on its own pad as it is let go (30), come no nearer each other in flight than 34.97 m between hulls, and have no trade, pad or lane refused. Alone, a trader buys electronics at Corva for Highport, and with its rivals all after that shelf it buys copper; with no credits it waits, and with copper in its hold it sells it. A world saved four minutes in carries on to the same state hash, pinned in Debug at `0xe3d0ffb25ad741ee` and checked in Release. In the AI suite, each curve at its known values, a score made up for its considerations and cut short once beaten, the best option chosen, the earliest of equals and none of nothing; a tree's sequences and selectors, a running child resumed, the decorators, waits in whole steps, a world saved mid-tree carrying on to the same hash, and every shape a tree must not have refused; two ships through a gate in one step out a hull's breadth and 20 m apart across it, and a third a second later clear of both, touching nothing. In the sim suite, a system's own commands applied at once and not recorded, and a replay of what was fed alone reaching the same hash. |
| 10 | **Presentation polish.** Done 2026-11-13. Sub-phases 10a–10d; catalogue entries 27 and 28. | Each sub-phase's, met: rows 10a to 10d. |
| 10a | **Atmosphere and planets.** Done 2026-11-13. Catalogue entry 28. A frame written to a PNG from inside the frame loop (`render/Screenshot.h`): `--screenshot` after `--after-frames` quiet frames, `--frame-step` for clocks moved a fixed step and held while anything loads, `--run` for console lines before the first frame, the console's `screenshot` and `orbit` commands, and `scripts/capture.sh`, whose reference views (`source/test/frames/views.txt`) are a stage of the gate. Air and rings on a celestial body (`AtmosphereComponent`, `RingsComponent`), as maths over a sphere in `atmosphere.glsl` and `rings.glsl`, which the render suite runs as C++ against brute force ([atmospheres](architecture.md#atmospheres)): single Rayleigh and Mie scattering with the Chapman function in closed form; rings of three bands and two gaps over noise, filtered to the pixel. The frame's body list; the planet program's reddened terminator, haze, rings' shadow and rings in front of the surface; an atmosphere program after the sky for the limb and the rings over space. Clouds and city lights on the planet program from noise, the clouds drifting by a time-driven parameter, the lights with a glow standing in for bloom. The celestial bodies in the environment's capture, lit spheres with their air and rings, baked again as they move by a texel of it. Ostra has Earth's air, clouds and cities; Corva, in a new Vesper scene, a gas giant's air and rings. | Met: the five reference views, the orbit scene as it opens, Ostra by day and by night, Corva and its rings, and the Vesper scene as it opens, captured twice on lavapipe with no pixel differing, and the gate holds every later change to them. |
| 10b | **The transparent pass.** Done 2026-11-13. Catalogue entry 28. A program's blend in its descriptor (`ProgramBlend`): a transparent program's draws and the particle emitters in view sorted back to front (`BuildBackToFront`) and drawn last in the opaque pass's rendering, after the sky and the air, lit through the clusters, tested against the surfaces' depth and writing none, and blended by what they add and what they let through by colour (`BlendMode::Transmissive`, dual-source blending, which the device is now required to have) ([transparency](architecture.md#transparency)). Glass (`glass.frag`), a thin tinted dielectric pane, on the ships' canopies and a buggy's windscreen. GPU particles (`ParticleEmitterComponent`, a scene file's `particles`): a pool of 2^17 slots stepped in place by a compute pass, each emitter a ring of it whose births the CPU tells and nothing read back (`render/Particles.h`, `particles.glsl`, which the render suite runs as C++), at rest in the frame of the emitter or of an entity it names, drawn as sprites facing the camera, drawn out along their motion, adding light, hiding a share of what is behind them, and lit as small Lambertian spheres ([particles](architecture.md#particles)). A plume at each nozzle of the simulation's ships, from their throttle (physics shows `FlightControls` in snapshots); a jump's streaks past the camera, and its flash. A test bench, `scene/effects.yaml`, and the console's `pilot`. | Met: the bench three seconds in (clear and amber glass over coloured stripes, a plume, lit smoke, sparks) captured twice on lavapipe with no pixel differing, and a reference view of the gate's, the six views together in 24 s; in the shell, the shuttle's plumes from full ahead in the chase view, and a jump's streaks and flash from the orbit and chase cameras. On the RTX 5060 Ti at 1243 × 1338, the bench's frame takes 0.72 ms, of which its glass 0.09 ms and its particles 0.03 ms, and the orbit scene's 0.29 ms, its parked ships' idle plumes neither stepped nor drawn (9d: 0.32 ms). On lavapipe at 640 × 480 the bench's frame takes 25 to 27 ms, its particles about 4 ms and its glass about 2 ms of it, and the orbit scene's 36 ms (9d: 33 ms). |
| 10c | **Post-processing and screen-space effects.** Done 2026-11-13. Catalogue entry 28. A chain of full-screen post passes in the pass list on the resolved targets, over one GLM-compatible file (`post.glsl`, which the render suite runs as C++, `render/PostProcess.h`) ([post-processing](architecture.md#post-processing)). Bloom, Jimenez's chain of five levels down from the frame and back up, the first weighing its boxes by brightness, mixed into the frame by its strength in the output pass. Ambient occlusion, Scalable Ambient Obscurance from the depth prepass's depth and view-space normals at sample zero, blurred over its 4 × 4 pattern, before the opaque pass shades, dimming the image-based light alone, the specular by Lagarde's fit; F6 shows it. The depth prepass gains a fragment stage for the normals and object IDs, and runs when asked for, with ambient occlusion, or with an outline in view. A stencil on the depth target (`D32_SFLOAT_S8_UINT`, its depth and stencil resources of their own in their own layouts, `separateDepthStencilLayouts` now required), written by every opaque draw as its batch's reference and resolved to sample zero with the depth: bits 0 to 2 the cockpit, the outlined and what post-processing leaves alone (`MaskComponent`), 3 to 7 the game's ([masks](architecture.md#masks)). An object ID buffer from the prepass, a whole number in a float, since lavapipe has no multisampled integer targets. Outlines (`OutlineComponent`), the selected's line a pixel wide by the stencil and the targeted's glow by a jump flood from the IDs, in one stencil-tested pass ([outlines](architecture.md#outlines)); the shell outlines what is picked and marks the hull flown from its seat as the cockpit. A light's intensity driven by time (`LightAnimation`), by the wave a material's parameters move by (`Scene::Wave`, moved from render); a reserved pad's lamps blink. A test bench, `scene/post.yaml`, `--[no-]ambient-occlusion`, `--bloom` and the console's `view`. Screen-space reflections stay out (see [decided](decisions.md)). | Met: the post bench two seconds in (occlusion under the crates, the selected crate's line, the targeted pod's glow about it and its fin as one shape, the panels' bloom, the beacon bright and lighting the wall) a seventh reference view, the seven captured twice on lavapipe with no pixel differing, in 29 s; the smoke test's synchronization validation clean over the new passes. On the RTX 5060 Ti at 1243 × 1338 the orbit scene's frame takes 0.64 ms with occlusion and bloom (0.42 ms without), the transparent pass's bench 1.32 ms and the post bench 1.58 ms, occlusion the most of each. On lavapipe at 640 × 480 the orbit scene takes 44.2 ms with bloom and without occlusion, a CPU device's default (10b's commit, measured the same day: 35 to 36.5 ms), and 53 ms with it. |
| 10d | **Animation.** Done 2026-11-13. Catalogue entry 27. Keyframed TRS tracks on the scene's own entities ([animation](architecture.md#animation)): a clip file (`animations/<name>.clip.yaml`, `animation/ClipFile.h`) of named clips, each of tracks on named nodes (translation, rotation in radians about X then Y then Z, or scale; linear, step, or cubic for translation and scale) and events, checked as it is read, loaded as a resource whose YAML is parsed on the main thread. A clip's types out of `Rig.h` into `Clip.h`, its sampling (`Sample`) shared by a rig's clips and a file's. Clip events (`Animation::ClipEvent`), crossed by `AdvanceClip` forwards, backwards, round a loop and over more than a lap, each once, and a time set past a clip's end showing the rig there without them. `Scene::ClipPlayerComponent` plays a file's clip on the entities its nodes are bound to, several to a node moving alike; the scene's update loads and holds the files, checks a player's bindings once a revision and a clip, poses each bound entity path by path over its authored transform (`NodePoseComponent`, runtime, never saved), and returns the events crossed (`ClipEventFired`); a scene file's `clip` binds nodes by entity id. A rig's glTF clip now plays without a skin, moving where its mesh is drawn, and the renderer neither draws nor shadows a mesh scaled to nothing. A clip's event sounds by data (`Audio::EventSound::clip`, `PlayClipEvent`), from its player's entity. The docks show each ship's `Docking` and `Hull` in the snapshots. In the shell, four docking clamps on every pad (`App::PadClamps`, `animations/pad-clamp.clip.yaml`): a post telescoping out of the deck beside the hull, an arm rising with it and swinging over onto the hull's side, a jaw sliding out along it to bite, all four played by one player on the pad; fitted to the hull held, two a side a quarter of its length fore and aft and two fifths of its height tall; closed while the pad's ship is docked and opened as it is let go, shown as they are when first seen; a display pad's holding its ship; put away at no size once stowed. The shipped docking sounds are the clamps' events, `clamp.closed` and `clamp.released`, heard from the pad for every ship, where they were the docks' `Landed` and `Left` for the player's ship, from the station ([decided](decisions.md)). | Met: in the animation suite, a clip's events crossed forwards, backwards, round a loop and over more than a lap, each once and in order, and none for a time set past its end; a clip file's rotations as turns about X then Y then Z, its step and cubic tracks, and nine kinds of file refused naming it; the pad clamp's clips through the resource manager. In the scene suite, two clamps bound to one player on a pad: as authored until the file is in, half out of the deck at 0.4 s, closed at 2 s with each jaw where the clip puts it in its own clamp's frame, `clamp.closed` heard once from the pad however long it is held, `clamp.released` at once as they open, put away at no size at the end, and the scene saved as authored throughout; a clip the file lacks and a binding to a node it does not name leaving the rig as authored; a clip's file, name and bindings checked as a scene loads. In the app suite, Highport's eighteen pads each with its clamps, the parked transports held from the first snapshot without a sound and fitted to their hull, a shuttle clamped to pad 1 closing on it from the start and fitted to it, the same snapshot starting nothing again, and its clamps opening as it is let go while the pad is still its; the shipped sounds of the clamps heard from the pad's side, and the docks' own events silent. In the audio suite, a clip's sound placed and flat, and a definition naming both kinds of event, neither, or a clip's at a field, refused. On lavapipe, the shuttle in its clamps from the chase camera an eighth reference view, the eight captured twice with no pixel differing, in 31 s, the orbit and refinery views changed by the parked ships' clamps alone (5 and 6 pixels); and in the shell, the clamps seen part-way open on the shuttle as it lifts from its pad. On the RTX 5060 Ti at 1243 × 1338 the orbit scene's frame takes 0.639 ms (10c: 0.64 ms), and on lavapipe at 640 × 480 43.4 to 43.6 ms (10c: 44.2 ms). |
| 11 | **Scripting and shipping.** Done 2026-11-13. Sub-phases 11a–11d; catalogue entries 29 and 30. | A data mod and a script mod load from the user mount, and a packed build runs. Met: rows 11a and 11b, and each later sub-phase's own, rows 11c and 11d. |
| 11a | **Packs.** Done 2026-11-13. Catalogue entry 30. Pack files behind the VFS (`vfs/Pack.h`): a directory's files in one, each stored or one zstd frame with its checksum, the index checked whole as it is mounted, read by `pread` from a file held open; a mount of a regular file is a pack. `FileSystem::Find` (a file's mount, time and size, which the resource manager's hot reload stamps by) in place of `Resolve`, and `ReadLayers`, every mount's file at a path. zstd 1.5.7, one copy, which `ktx_read` is built against in place of its own ([decided](decisions.md)). `jettison_pack` and `jettison_add_pack`; the sample assets as `jettison_assets_pack`. Definitions in layers: a whole file replaces, a `.patch.yaml` beside it merges over the layers below (`YamlArchive::ReadLayered`: mappings key by key, a null removing, `!append`, `!merge=<field>`), each layer migrated on its own, and every change a mod makes logged ([files, packs and mods](architecture.md#files-packs-and-mods)). The shell's mounts (`App::MountAssets`): the base, the user's mods under `mods/` in the order of their names, the user's data directory; the assets' pack found beside the executable where there is no directory; the default scene and ImGui's fonts read through the mounts. The headless smoke test runs packed, with a data mod in its user's mods (`--packed`). | Met: the headless smoke test, both scenes, from `assets.pack` with validation on and a pack of the test's mod in its user directory, the shell saying it mounted the base and the mod as packs and that beer is patched by the mod, once a scene; in the app suite, a mod in the user's mods over the sample game's definitions packed patching beer's price and Highport's market by `!merge=good` and adding wine. In the vfs suite, a packed directory read as the directory does, every prefix of a pack and four kinds of damage refused as it is mounted, a damaged file caught as read, and a pack replaced on the disk read as mounted, under the sanitisers too. The sample assets, 136 files and 90.3 MB, pack in 1.2 s to 88.6 MB. |
| 11b | **Scripting.** Done 2026-11-13. Catalogue entry 29. WAMR 2.4.5 decided over wasmtime ([decided](decisions.md)): its fast interpreter, with instruction metering, the thread manager, bulk memory and reference types, software bounds checks, and no libc or WASI. Scripts as definitions (`defs/scripts`, `script/ScriptDefinition.h`), their modules read through the VFS from whichever mount has them; fuel a call by instruction count, and a watchdog's time limit that stops a script past it; a trap logged and survived. The host API generated from reflection (`script/Bindings.h`): the types `Reflect::Attribute::Script` marks laid out as wasm32's C ABI lays them out, an event copied into a handler's memory and a command read out of it with every pointer, bool and enumerator checked, and from the same layouts a C header and a Rust module, each asserting its sizes and offsets, written by the console's `script_bindings`; imports (`log`, `input`, `send:<Command>`) checked against a script's grants as it loads. Calls from events, as the shell takes them, whose handlers send commands into the queue, and from AI considerations, an input a script answers on a fresh instance each call (`ScriptHost::WithScripts`), which the traders weigh their runs with. Scripts in C or Rust (`jettison_add_script`; lld's wasm-ld and rustc in the devenv), the shell's in Rust. The smoke mod gains a Rust script and a patch of the trade decision ([scripts](architecture.md#scripts)). | Met: the headless smoke test, packed, loads the smoke mod's script from the pack in its user's mods once a scene ("Script 'smoke': scripts/smoke.wasm loaded, 1 handler(s), 1 input(s), granted log"), and the script, called from the traders' trade events, says what it heard, twice a scene ("smoke: heard trade 1: bought 9 crate(s) of copper at 61660 credits"), with no trap. In the app suite, the same mod from a user's directory answers 25 inputs as the traders choose in the first step and hears every trade they fill, and the shell's Rust bindings are checked as this build's. In the script suite, under the sanitisers too, the limits (fuel the same on every run, a call past 50 ms stopped, memory, stack, bounds), every refusal of a module or a definition, and events in and commands out and inputs, against the same module in C and in Rust. A handler call takes 0.27 µs and an input call 9 to 10 µs in Release. |
| 11c | **Cinematics and grading.** Done 2026-11-13. Catalogue entry 30, and entry 18's cinematic camera. The colour grade (`render/ColourGrade.h`, `Scene::ColourGradeComponent`): a `.cube`'s 3D table, read on the I/O worker as a resource (`grades/`) and uploaded as a 3D image, applied by the output pass after the tonemap to the display's colours encoded sRGB, by a strength, under the fade and the UI ([decided](decisions.md)); its lookup at texel centres in `post.glsl` (`LutCoordinate`), held to the CPU's (`SampleLut`). Cinematics (`scene/Sequence.h`, `cinematics/`): sequences of shots, each a rail of points in a role's frame flown at an even speed along a centripetal Catmull-Rom spline, eased, looking at a point in another role's frame, its field of view from start to end (`CameraPoseComponent::verticalFovDegrees`), cut or blended from the shot before; the game's camera blended in and out over the rigs, which run under it, a blend moving the camera and the point it looks at along lines ([decided](decisions.md)); a fade, letterbox bars (the output pass's, in whole rows) and events on a clip's timeline (`Animation::AdvanceClip`). `SequencePlayerComponent` and `SequenceSystem`, after the camera rigs, binding roles by name and waiting for a role bound to nothing yet. The shell binds the ship, its pad and its station, plays `cinematics/dock.sequence.yaml` as the ship lands on a pad, cuts it short on Escape and hides the flight readout under it; the console's `sequence <name> [seconds] \| stop` and `grade <name> [strength] \| off`. `OrientationFromAngles` moves to `scene` ([cinematics](architecture.md#cinematics), [colour grading](architecture.md#colour-grading)). | Met: in the shell on Xvfb, the ship undocked and sent back by the console's `dock` landed on pad 1 of Ostra Highport and played the docking sequence ("Playing sequence 'dock' from 0.0 s"), with no warning. Two reference frames, the same to the byte on every run: `dock-sequence`, the sequence 1.5 s in from the chase camera, the shuttle in frame as the camera swings round it and the bars drawn; and `graded`, the orbit scene graded by the sample's `cold-light.cube`, which is within one level at every pixel of `orbit.png` put through the table on the CPU (89% of its pixels moved by more than two), every other reference unchanged. In the scene suite, a sequence read and every refusal of one; a rail flown evenly through its points, on a line to within 0.3 mm in 20 m; the game's camera blended in and handed back exactly, shots cut and blended, the bars and the fade; a rail riding a turned, scaled frame 10^8 m out; and a player through the resource manager, waiting for its file and its roles, hearing each event once and ending. In the render suite, `.cube` files read and refused, and the lookup through a device's trilinear filter equal to the table's grade at 2, 17 and 33 entries a side. A grade costs the output pass 0.7 ms on lavapipe at 960 × 540 (1.1 to 1.8 ms). |
| 11d | **Crash handling and localisation.** Done 2026-11-13. Catalogue entry 30. A crash report on a fatal signal (`platform/Crash.h`, `Platform::InstallCrashHandler`): SIGSEGV, SIGBUS, SIGFPE, SIGILL and SIGABRT, which a failed assertion and `std::terminate` end in, write `crash-<UTC time>-<process id>.txt` into the state directory's `crashes/`, with the signal and its cause and address, the thread, the build and its command line, the frames as module and offset, the same frames named by `addr2line` in a child process (`vfork` and `exec`, after the rest is on the disk), and the log's tail; the head and the frames to stderr, then the signal raised again under its default action. Async-signal-safe until the symboliser: buffers made at installation, glibc's `backtrace` primed there, modules from `/proc/self/maps`, a stack of its own on the main thread, and SIGALRM after 30 s ([decided](decisions.md)). A `std::terminate` handler logs the uncaught exception at Fatal; nothing installed under the address sanitiser; Tracy's own handler compiled out. The log's tail in core (`Core::LogTail`, `WriteLogTail`), its last 64 KiB as the file has its lines, read without the log's lock. `scripts/symbolise-crash.sh` names a report's frames written where there was no symboliser. The shell installs it as its user directories are resolved; the console's `crash fault \| assert \| exception`. String tables (`ui/StringTable.h`): YAML under `strings/<language>/`, nested keys joined by dots, a language read over its primary language and that over English, the base, which has every key; the same file in several mounts merged key by key, so a mod changes the strings it names or adds a language; std::format's positional arguments and plural forms by the CLDR's rules for whole numbers (`Format`, `Plural`); a translation that names an argument or a data expression its base text does not refused as it is read; a key no table has reads as itself and is counted with the UI's problems; numbers grouped by the table's separator (`Number`, and the documents' `thousands`). Documents name keys as `[[market.buy]]`, replaced as RmlUi reads their text (`TranslateString`), before its markup and data expressions; `UiSystem::SetLanguage` reads open documents again, each keeping its handle. The language is the player's setting (`ui.language`), not the environment's ([decided](decisions.md)); the console's `language [tag]`. The market and saves screens, the market's status, the goods' names and crates and the trade's answers come from the sample's tables, in English and German ([crashes](architecture.md#crashes), [localisation](architecture.md#localisation)). | Met: in the shell on Xvfb, the console's `crash fault` ended the run with SIGSEGV (status 139) and wrote its report in the run's `state/crashes/`, its frames named from `Crash`'s write through `Console::Execute`, `Application::RunFrameLoop` and `RunRenderApplication` to `main` with their files and lines, and the log's tail ending in the console's warning; `crash assert` and `crash exception` ended it with SIGABRT, the assertion and the uncaught exception at Fatal in their tails. In the platform suite, the same three in a child each: the signal and its cause, the thread, the build, the frames in the suite's executable and named (`WriteThroughNull`, `Core::AssertionFailed`), the tail with the line logged before the crash, stderr with the head and the frames and not the tail; and a directory that cannot be made refused. In the core suite, the tail past its size beginning at a whole line, and written to a descriptor the same. In the ui suite, tables read, merged across mounts and refused (a key in two files, a file not a table, a brace that is neither, a language that is not one, a base with none), a regional language over its primary over the base, arguments reordered, plural forms in English, Russian, Polish, Czech, French and Japanese, four kinds of translation refused with the base standing, and a document's keys read in English then German, its handle and showing kept and its data expression inside a translation bound. In the app suite, the market screen built in English and German from the sample's tables. The packed smoke test reads 51 strings and no key is missing. A tenth reference frame, `market-de`, the orbit scene with the market screen in German, captured twice with no pixel differing, every other reference unchanged. |
| 12 | **Saves and the step's interface.** Done 2026-11-14. Sub-phases 12a–12c: the first tier of [carried forward](roadmap.md#carried-forward), what a save holds and what leaves the step, before players have saves. | A save made before phase 12 loads; a component, a script's keys, the mods and Jolt's version each have their place in a save, with a test of each migration; and a refusal leaves the step as a reason the string tables word. Met: rows 12a, 12b and 12c. |
| 12a | **The save's shape.** Done 2026-11-13. Catalogue entries 25, 29 and 10 ([decided](decisions.md): a component's chain, a script's keys, a player's mods). A version per component storage: the world's save, at version 3, begins with each storage's version, the step from 2 writing an empty table so every storage of an older save is at 1, and a version past a component's chain is refused by the component's name before anything else is read. A component's chain (`Sim::ComponentMigrations`, `SimWorld::RegisterComponent<T>(chain)`) runs over each value's record (`Serialise::BinaryRecord`, `BinaryReader::ReadRecord`): its fields' names and bytes, sharing the archive's table of names, read with the reader's primitives (`Open`) or as a type (`Get`), set, removed and renamed, then read as the type is now (`ReadInto`). A script's keys: the imports `store` and `load`, granted by `store`, bytes under keys of 1 to 64 bytes, 256 keys and 64 KiB a script (`HostLimits`), refused while it answers an input; kept by the host whether the script is loaded or not (`ScriptStore`), a section of the save game (`Script.Store`, `WriteStore`, `ReadStore`), a load's `SetStore` making each handler's instance again; in the C header and the Rust module. Mods: `mods.yaml` beside the settings, in mount order, each enabled or not, a mod's identifier its directory's name or its pack's without `.pack`, a mod found and not listed added at the end and the list written again, one listed and missing keeping its place; `mod.yaml` at a mod's root (`App::ModDescription`: a name, its `release`, the engine it was written for, `requires`), a mod for a newer engine or whose requirement is not mounted before it left out; the mods mounted and their releases in `SaveInfo::mods`, and `Sim::ModDifferences`, which the shell's load logs and goes on. The smoke mod describes itself, and its script counts the trades it hears under a key. The world's six pinned hashes moved with the layout, each checked to be its old value when the old layout is written ([persistence](architecture.md#persistence), [files, packs and mods](architecture.md#files-packs-and-mods), [scripts](architecture.md#scripts)). | Met: in the sim suite, `world-v2.save`, written by commit `cfb86e9`, loads through the world's step into the state this build saves at step 15, and steps on to the same hash; a component's chain reads a save of its earlier layout, a field removed (a grade, a fine one counted into the tonnes) and a field retyped (a float of credits read by its bits and made whole), and saves it at version 2; without the chain the removed field is refused by name, a later build's version by the component's, and a step missing by its version. In the app suite, the smoke mod's script, mounted from a user's mods, counts two trades, a save game holds its keys and the mods (`smoke` 1.0.0), and a new host loaded from it hears the next as its third; a load under none or other mods names each difference; the player's list orders, disables and adds mods, keeps a missing one's place, is not written when nothing is added, and when it does not read is kept as it is; a mod for engine 0.2.0, one requiring it, one requiring a mod after it and one whose description is misspelt are left out, each with its reason. In the script suite, under the sanitisers too, the keys from C and from Rust kept past an unload and through a section, a fifth key past four and 1,000 bytes past 1,024 refused with what was kept left as it was, a 65-byte key, a store in an input and a module storing ungranted refused; in the serialise suite, a record's enumerator new to the table and a nested struct set, and a field the type does not save refused. The packed smoke test, both scenes, mounts `smoke.pack` as `mod:smoke` ("The mod smoke is Smoke test 1.0.0"), adds it to the run's `mods.yaml`, and saves and loads with the script's keys and the mod named in the save, with no difference said; its script, counting under its key, heard 4 trades. |
| 12b | **Jolt's state across a Jolt upgrade.** Done 2026-11-13. Catalogue entry 25 ([decided](decisions.md): physics keeps Jolt's state). Physics' layout 4: after the layout, Jolt's version (`Physics::JoltVersion`, `BuiltJoltVersion`: 5.6.0) and those of its feature bits that change what it saves or how it steps (double precision, the cross-platform determinism, 32-bit object layers), not the assertions or the profiler, which differ between Debug and Release and change neither; and with each body's record, whether it sleeps. A save of another version is loaded by rebuilding its bodies from the records, each where it was placed, moving as it was and asleep if it was, Jolt's state passed over and the rebuild logged; saved again, it is this build's. Layout 3 is read as 5.6.0's, which every save of it was. `PhysicsWorld::SaveAsJoltVersion` writes a save as another version's, for the tests, and `Asleep` says whether a body sleeps. The six pinned hashes with physics in them moved with the layout, each checked to be its old value when layout 3 is written ([physics](architecture.md#physics), [persistence](architecture.md#persistence)). | Met: in the physics suite, `replay-layout3.save`, written by commit `8177358`, loads with Jolt's state and steps on to the bytes the run not saved reaches. A save marked as Jolt 5.5.0 loads by the rebuild and steps on, and what it loses is measured: the bay of thrown cargo, rebuilt at step 180 with three pairs touching, keeps what touches, begins and ends the same pairs in every step, and is 2 nm out after a step and 57 µm by step 300; saved again, it carries on exactly. A crate asleep against a block and in a sensor sleeps on where it lay, nothing beginning or ending, and a crate in flight goes where it would have to the bit. The warm start: four crates thrusting in a row into a block give 1.2 mm in the first step and 7.1 mm by the tenth, and rest 4.6 mm from where they would have. In the dock suite, the clamps: a shuttle rebuilt on its way down onto its pad docks at the same step in the same place, and rebuilt docked it is where it would have been ten seconds on, and undocks touching only its pad. |
| 12c | **Reasons and the owner's purse.** Done 2026-11-14. Catalogue entry 30, 8b and 9f ([decided](decisions.md): a reason and its arguments, a player's fleet). `Sim::Reason` (`sim/Reason.h`) in place of the text the docks' (`PadRefused`), lanes' (`LaneRefused`), steering's (`DestinationLost`) and trades' (`TradeRefused`) events carried: a key and its arguments, each an integer, an entity or an identifier from the game's data (a definition's, with its type, or a body's), registered with Reflect. Each module's reasons an enumeration registered as `<Module>.Reason` (`Dock.Reason`, `Ai.Reason`, `Shell.Reason`), whose enumerator names the key (`Sim::MakeReason`: `reason.dock.no_pad_fits`), an enumeration registered otherwise refused; `Sim::ReasonKeys` lists them and `Sim::Describe` is a developer's reading. Worded by presentation (`App::WordReason`, `app/src/Reasons.h`) from `strings/<language>/reasons.yaml`, English and German: an integer by the language's numbers, an entity by its name in the snapshot (a market's station, or the body a station rides the rails of, `Physics::OnRails` now shown in the shell's snapshots), a good by its name, then by position (`StringTable::FormatArguments`), a key with plural forms chosen by its first integer (`PluralArguments`); the market's answer, and the shell's log lines of its ship's refusals. The purse on an entity of its owner's (`App::Owner`, `MakeOwner`, `FindOwner`, `PurseOf`), found by the controller its ships are commanded by, the owners and `Sim::ControlledBy` shown in snapshots so the market screen finds it: the player's and each trader's. `Trade` carries its sender and is refused from one that does not command the ship, and for a ship no one owns. A save from before keeps its purses on the ships, which the shell's load moves to their owners (`MovePursesToOwners`). The scripts' bindings carry the reason and the trade's sender. The traders' pinned hash moved with the owners' entities ([simulation and presentation](architecture.md#simulation-and-presentation), [localisation](architecture.md#localisation)). | Met: in the app suite, the market's refusals worded from the tables in English ("Ostra Highport has 6 crates", "4 crates cost more than the purse holds", "Ostra Highport does not deal in salt") and the same six in German ("Ostra Highport hat 6 Kisten", "4 Kisten kosten mehr als das Guthaben"), with no problem counted, and every reason of the docks, the AI and the trade with a text in both; a trade sent for another's ship, by another player and by the AI, refused ("its sender does not own the ship") with the purse and the hold as they were, the game's own rules' trade filled, a second ship of the fleet paying from the same purse, which outlasts the first ship, and a ship no one owns refused; a world saved with three purses on ships, two of them one player's, loads with them moved to two owners, once, and trades on. Over seven hours of the seven traders, every trader's place to 17 digits, its runs, earnings and hold, every market's stock and ask and the number of events are those of the build before. In the sim suite, a reason's key from its enumerator, three kinds of misregistration refused, and its arguments kept through an archive; in the dock and AI suites, each refusal by its reason; in the ui suite, arguments known only as the game runs formatted and pluralised as a call's are, too few and too many counted. |
| 13 | **Crossing a system, its gates and the fleet.** Done 2026-11-14. Sub-phases 13a–13e: the sample's fleet ([sample game](sample-game.md#what-comes-next)) and the space it is played in, the head of the second tier of [carried forward](roadmap.md#carried-forward), with the first tier's last commands to carry their senders. | A ship crosses its system by cruising and leaves it through a gate that cleared it, its owner charged the lane's toll; a player buys a second ship, attaches to either and takes the helm of one while the other trades for them by its behaviour. Met: rows 13a to 13d, and 13e. |
| 13a | **The cruise.** Done 2026-11-14. Catalogue entries 22 and 26 ([decided](decisions.md): a ship crosses a system by cruising). `Ai::Cruise`, with its sender, takes a ship to a body of its system (`ai/Cruise.h`): out of physics and its frame, its body kept in its `Ai::Cruising`, placed each step along its course by the system `cruise`, after the lanes'; and out at rest, facing the body, a quarter of its radius and 4 km beyond it and no nearer than where its gravity pulls at 1 m/s², which a ship's assist holds against, in its frame or, for a gate by a station, the station's, so a frame changes at a cruise's ends and a lane's and nowhere else. Its time is the hull's drive (`Ai::CruiseDrive`, a hull definition's `cruise`), the seconds for 10⁸ m rising as the distance's fourth root; the target's place at arrival and the time are solved from each other in eight rounds; the course is straight legs round the stars, planets and moons in the way (`Ai::Waypoints`, 2 km clear), relative to its carrier, the nearest body both ends orbit, and carried with it, each body in the way placed where the ship passes it; along it the ship eases out and in by an in-out cubic and faces along its leg. Refused with a reason (`reason.ai.*`, in English and German) for a ship its sender does not command, cruising already, of no drive or on rails, within 2 km of a station's radius, already where it would arrive, to a body its system does not have, or of no course; a lane is refused to a cruising ship. The shell gives every ship its hull's drive, sends the player's with `cruise <body>`, and its flight panel, controls and pilot camera follow the ship's frame from the snapshots ([cruising](../../manifest/docs/architecture.md#cruising)). | Met: in the ai suite, the shuttle's drive takes Highport to Pell, 392,995 km, in 56.3 s, the whole steps its course's length gives; out of physics and its frame and in its system throughout, clear of Ostra and Pell at every step, 15 km out a second in; out in the step its arrival names at 2,214.7 km from Pell's centre, where Pell pulls at 1 m/s², to a part in 10⁹, facing it, at rest in Pell's frame, with its body made in the next step; and back to Halcyon Gate, in Highport's frame, to the millimetre. A course from the far side of Ostra goes round it by four points, 389 km above it at nearest; a ship arriving off Ostra, 19,965 km from its centre, is held by its assist to within a metre for a minute; to the star takes 243 s, carried by it; each refusal gives its reason, and a world saved mid-cruise arrives at the hash the one saved does. In the shell under Xvfb, the ship out of the lane at Vesper Gate cruises to Corva, 52,522 km in 34.1 s at up to 3,218 km/s, the flight panel counting down, and arrives facing it 87,504 km from its centre; within Highport's reach it is refused, worded from the table. |
| 13b | **Gates in Manifest's places, and the traders cruising.** Done 2026-11-14. Catalogue entry 26 (9a, 9f). The gates are in Manifest's places: Halcyon Gate a moon gate about Ostra, Vesper Gate one about Corva, Vesper Deep Gate and Marrow Gate star gates about their suns, each on its parent's orbit and facing a planet (`JumpPointFacing`), so a ship comes out in the gate's own frame and no longer beside a station. A second station in Halcyon, Ostra Anchorage, a mining block in low orbit over another of Ostra's economic blocks (its layout and a market, `defs/stations`, `defs/markets`), gives a run within a system to cruise between. A trader's `cruise` task (`app/src/Traders.cpp`), with `gate` or `market` for its argument: flown out of a station's 2 km reach round the station as `fly-to-gate` goes, then `Ai::Cruise` with its sender, running until it arrives, and failing if refused; the tree is `leave`, then for each jump `cruise gate`, `fly-to-gate`, `jump`, then `cruise market` and `dock`. A route's crossings are costed by the ship's drive, the new `Ai::CrossingByCourse` overload taking a time of the course's length (`CourseTime`), the trader giving its drive's seconds and 45 s to leave a station's reach and come in to a gate, so the run's credits a second are weighed in the time it takes. Arrivals are spread: the lanes' ring placement is now `Detail::ClearPlace`, shared, and a cruise's arrival, which `CruiseTravel::Attach` now gives physics for the hulls' radii, comes out clear of every body there and of what arrived before it in the step by `kArrivalClearance`. | Met: in the app suite the seven traders trade for two hours of the galaxy's time, 76 runs sold, 221 cruises of 17 to 73 s and none refused, 289 arrivals by lane and cruise, the tightest 35 m between hulls against the 20 m asked, no contact but the traders' own on their pads, and the world saved at four hours carries on to the hash the original reaches, pinned at `0x487ab0bd2d27a6b8` (moved from `0x4b2e0ff03b7e40b4`, since the traders now cruise where they flew, and a third market changes what they choose; no other suite's pin moved). In the ai suite two ships set out four metres apart arrive at Pell 20.007 m between hulls; the gates are checked at each of their parents. The reference frames are unchanged but the market screen's, whose other markets now name Ostra Anchorage. |
| 13c | **A gate flown through.** Done 2026-11-14. Catalogue entry 26 ([decided](decisions.md): a gate sends the ships it has cleared). Each jump point's gate is an entity (`ai/Gates.h`, `Ai::Gate`), made at the start (`Gates::MakeGates`, the shell's `OpenGates`, which also gives a world saved before gates its own) and saved: a kinematic sensor on the jump point's rails and in its frame, a cylinder of the ring's radius 60 m deep (`Gates::RingShape`), turned as the jump point faces (`Celestial::BodyOrientation`, which physics' rails now use). `Ai::RequestTransit`, with its sender, clears a ship at once if the gate is free or queues it, as a pad does, the ship holding an `Ai::Transit`; refused with a reason (`reason.ai.*`, English and German) for a sender that does not command the ship, a ship not flying or not in the gate's system and frame, one already cleared or waiting, a gate no lane leaves through, a hull larger than the ring, and what the game's `Ai::TollRule` refuses. The system `gates`, after the lanes', sends a cleared ship touching its ring along the lane, charging its owner as it enters (`TransitSent`); lets every other body that begins to touch a ring pass through with a reason, charged nothing (`GatePassed`); lapses a clearance unused for two minutes; gives up one whose ship has gone, left the frame or cruises; and clears the next in the queue. Each lane has a toll, carried by its exits and routes: since the same day, the light years between its systems at the galaxy's rate (`tollPerLightYear`, 500 credits; Vesper 4 light years from Halcyon, Marrow 12 beyond), 2,000 credits the moon gates' hop and 6,000 the star gates' ([decided](decisions.md)), where it was first a number of the lane's; the sample's rule draws it on the owner's purse (`App::ChargeTolls`, `reason.shell.short_of_toll`). A ship comes out of a lane ahead of the ring, clear of its sensor, and gives up its destination as it enters one; arrivals keep clear of no sensor. A trader's tree is now `cruise gate`, `clear`, `fly-through`, `jump`, its runs weighed net of their tolls and the tolls on to its market kept back from what it buys. The shell's `transit [gate]` asks for its ship, the flight panel counts the clearance down, and the scene jumps as the gate sends the ship; `jump`, J and the galaxy panel stay the tools' jump ([gates](../../manifest/docs/architecture.md#gates)). | Met: in the ai suite a ship cleared at Halcyon Gate and flying at 100 m/s from 400 m out is sent as it reaches the ring's near face, about 222 steps on, and charged the 2,000 credits, coming out at Vesper ahead of that ring unseen by it; two ships flying through uncleared, one of them waiting in the queue, pass through 600 m out the far side with `reason.ai.not_cleared`, in Halcyon on their credits; a clearance lapses in the 7,200th step and the gate clears the next in its queue in that step; a cleared ship whose owner has spent the toll passes through with the rule's reason and frees the gate; each refusal gives its reason; and a world saved with a ship cleared and one waiting goes on to the hash the original reaches. In the app suite the seven traders over two hours are sent through gates 65 times for 130,000 credits, 7 waiting their turn, the longest 33.8 s from cleared to sent, with none refused, lapsed or passing through unsent; 81 runs sold, the tightest arrival 34 m between hulls; the pin moved to `0x5546eebbec058964` (from `0xb84ccd89f6ba5ea8`, since the traders now wait at gates and pay tolls; no other suite's pin moved). In the shell under Xvfb, the shuttle cruised to Vesper Gate was cleared by `transit`, the panel counting down; let lapse, then asked again and queued behind a trader, cleared 7 s later, flown through at 144 m/s, sent and charged 2,000 credits, the scene jumping to Halcyon, and out at Halcyon Gate 40 s on with 248,000 credits. |
| 13d | **The fleet and the helm.** Done 2026-11-14. The sample's fleet ([decided](decisions.md): a player has a fleet), and the first tier's commands that carry their senders (9c). Before it, on 2026-11-14, a ship undocked by its pilot, which seemed never to be let go: the docks flew every ship released out to the point above its pad, beyond the station's sphere, 755 m over Highport's pad 1 at the lift's 20 m/s, 39 s with steering writing over the pilot's controls, so the pad was given up only after the run that saw it had stopped watching. `Dock::ReleasePad` gained `fly`, as `RequestPad` has it: a ship the docks fly out (the traders', which cruise on) is flown as before; one its pilot flies (the shell's) is lifted straight off the pad by its thrusters, 40 m (`Dock::kLiftHeight`), and held for the pilot, in the dock suite 2.7 s after the clamps open, answering its controls after; no pin moved. The helm: `Sim::Helm` on a ship, saved and in the snapshots, taken and left by `Ai::TakeHelm` and `LeaveHelm` (`ai/BehaviourTree.h`) from one that commands the ship, a player or an AI, each at one helm at a time, refused with a reason for another's helm, no one in particular, or a helm the sender is not at. While it is held the ship's tree stands down; taking it takes away the ship's destination and lets its controls go, what the game's rules fly it by (the docks, a cruise, a lane) going on, and leaving it starts the tree again from the top. Senders: `Physics::SetFlightControls` is taken only from the one at the helm or no one in particular (`Sim::MayFly`), else refused (`FlightControlsRefused`, physics' first reason, `reason.physics.not_at_helm`); `Ai::SetDestination`, `ClearDestination` (`DestinationRefused`) and `EnterLane` are refused from one that does not command the ship, a gate sending a ship it cleared as no one in particular; `Physics::Pick` carries its sender, which `Picked` hands back. The fleet (`app/src/Fleet.h`): the ships a player commands, each fitted as a trader is (`FitForTrade`: a hold, a `Trader` and the trader's tree), the shell's first among them at the player's helm. `App::BuyShip` sells a hull of `defs/shipyard` (`ShipForSale`: a shuttle at 120,000 credits, a transport at 900,000) at a station where a ship of the buyer's fleet is docked, parked on its lowest free pad the hull fits, the price drawn from the owner's purse, and takes its helm, which hands the ship flown before to its behaviour; refused (`reason.shell.*`, English and German) for no station, a hull not for sale, a buyer who owns nothing, none of the fleet docked there, no free pad, a short purse. The trader's `dock` gives up a pad its pilot was to land on and asks again for the docks to fly it. The shell attaches to a ship of the fleet (`attach <n>`, ] and [, and a ship bought): its camera follows it, the station it is docked at is bound for the market screen, and the scene shows its system, jumping there or along its lane; `fleet` lists the fleet, `buy <hull>` buys, `helm` and H take and leave the attached ship's helm, and P flies it only at its helm, following it otherwise; the flight panel names the ship and who flies it. Every ship with a hull is drawn as its hull from the snapshots, the proxies' map gone. The reference frame `docked` is updated for the panel's new lines. A save from before the helm gives the player the helm of its ship (`GiveOldShipItsHelm`); the steered ships no behaviour flies are the traffic, which a load had taken the traders for. The traders' pinned hash moved with `Sim::Helm` registered, its old value passing with the registration taken out. A shipyard screen is held over ([carried forward](roadmap.md#carried-forward)). | Met: in the app suite, a player at the helm of a shuttle docked at Highport buys a second for 120,000 credits, leaving 130,000; it is parked docked on Highport's next free pad, theirs, at their helm, and the first's helm is left; five crates are bought from it at its helm; their controls fly it, while those for the first, which its behaviour flies, and another player's for the second are refused; two minutes on the first has chosen a run and left its pad more than 300 m behind, while the second stands docked and its tree stood down. A transport the purse cannot pay for, a liner not for sale, a buyer who owns nothing, a station where none of the fleet is docked and one with no station are refused with their reasons, the purse untouched; bought until Highport has no free pad a shuttle fits, each on the next, the last refused for no free pad, with one helm, the last ship's; a world saved before the helm gives its ship the player's helm and fits it to trade, once. In the AI suite, a ship's tree stands down at its helm while another counts on, its destination taken and its controls let go, and taking a second helm leaves the first, whose tree starts at the top; the refusals each by its reason; the helm saved and the tree stood down in a world loaded from it. In the physics suite, flight controls from another player and from the owner of a ship no one is at the helm of are refused, and the helm's and no one's taken; a pick's answer carries its sender. In the sim, steering and lane suites, `MayFly`, and a destination and a lane refused from one that does not command the ship. Under Xvfb, `buy shuttle` at Highport bought Ship 2 for 120,000 credits and took its helm, Ship 1 left to its behaviour; Ship 2 bought two crates of wheat seeds from its pad at the helm, while Ship 1 bought twenty crates of iron, lifted off and made for Ostra Anchorage, which ] followed it to from behind, its panel saying its behaviour flies it; H took its helm and left it, `attach 2` and `helm` went back, and a transport was refused with none of the fleet docked and a liner as not for sale. |
| 13e | **Thrusters seen.** Done 2026-11-14, before 13d, at the user's asking: a ship flown by its AI shows when it fires its engines and its manoeuvring jets. The flight model writes each ship's `Physics::Thrust` every step (`physics/Flight.h`), the share of each axis's thrust, along and about it, that changed its velocity: shown in snapshots, and neither registered, saved nor hashed, since nothing in the simulation reads it, so no pin moved; the snapshots no longer carry `FlightControls`. Each ship proxy of the simulation's (`app/src/Dressing.h`) has a port at each engine nozzle and a jet at each of fourteen manoeuvring ports, a cluster each side of the bow and the stern firing out, up and down, and the bow's forward; `ShowThrust` fires each from the snapshot's thrust, an engine by the thrust ahead and a jet by how far its push and its turn about the hull's middle lie along the thrust and turn fired, its emitter's plume or puff as dense as it fires, at rest in the station's frame. Each nozzle glows at 3% at rest and warms towards how hard it fires with a time constant of a second, cooling over two and a half. | Met: in the physics suite a ship at full ahead fires all of its forward thrust, shown in its snapshot; held at 150 m/s it fires nothing; let go it brakes astern at full and turns at full about the axis asked; one no longer flown has no thrust. In the app suite a shuttle's proxy has two engines and fourteen jets; firing nothing shows nothing; half ahead a plume half as dense as full; astern the two bow jets; a strafe two jets, a climb four, a yaw three; a ship with no thrust nothing; its nozzle at 3% warms to 64.2% of full in a second of firing and cools by e in two and a half. Under Xvfb the shuttle lifted off its pad by the docks fires its four downward jets, their nozzles warm, and holding 20 m/s it fires nothing as they cool. The reference frames `docked` and `dock-sequence` are updated for the nozzles at rest. |

## After phase 13

The work finished since the phases closed, as each was done.

### The comment review

Approved 2026-11-18 and run before the SDK work, because every later
change reads these comments. The rule is [coding standards](coding-standards.md#comments): a
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
  What it left for later is carried forward ([roadmap](roadmap.md#carried-forward)).

### Reference frames that hold under load

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
34, and every view the same count idle and loaded ([build and gates](build-and-gates.md)). The
residuals the other views carried (`effects` 112 pixels, the rest 5 or fewer) were steady stale
references, so at the user's word every view was recorded again and the allowance went from 518
pixels to none, with llvmpipe's vector width pinned for other machines. Codex's first round: a settled
`Update()` now loops until nothing is in flight, so a load a `finish` asks for settles too, which
found `Finish` holding a slot that such a load could move (fixed, and `resource` joins the
sanitised suites); and a shot is taken only once a frame has drawn it, should a retried acquire
still be out of date. Its second round found that retry a frame late; the user settled it: with
`--frame-step` the renderer recreates until the frame draws (`RendererOptions::drawEveryFrame`).

### The gate inside the sandbox

Done 2026-11-18. Claude Code's sandbox (the managed settings in `~/nixos-config`) lets ccache and
`/tmp` be written and, with `sandbox.network.allowAllUnixSockets` (the user's), Xvfb listen, so the
whole gate runs inside it. With no `/dev/dri` there, Mesa's hardware drivers failed to enumerate
and their loader errors failed the smoke test's missing-shader check, so `ci.sh` pins lavapipe
when there is no `/dev/dri`. Met: `./scripts/ci.sh --werror --asan` passed inside the sandbox, the
reference frames at 0 pixels. Xvfb also served over loopback TCP there (`-listen tcp`), should the
Unix sockets be taken away again.

### The pre-review's fixes, ship classes and dry docks

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

### Frames drawn on the GPU, offscreen

Done 2026-11-20 (the user's, placed after N02; entries 21 and 28). The renderer draws with no
surface or swapchain when asked (`--offscreen`): GLFW on its null platform, a device with no present
support or swapchain extension needed, and `Swapchain` owning two `B8G8R8A8_SRGB` images it hands
out in turn, drawn into with no acquire, semaphores or present and exported for a transfer, which
`--screenshot` reads as it read the swapchain's
([architecture](architecture.md#rendering-clustered-forward)). `--run` takes a line at a frame
(`@40 undock`) or a simulation time (`@2.5s traffic`). `smoke-test.sh --offscreen` runs with no
display, pinned to the 5060 Ti by name on `lythir` and by `DeviceChoice` elsewhere, and the gate's
plain smoke test runs so unless it has `--llvmpipe` or no `/dev/dri`. Claude Code's sandbox cannot be
given the 5060 Ti's nodes (its bubblewrap call mounts a fresh `/dev` after every bind), so the user
excluded `ci.sh` and `smoke-test.sh` from it ([build and gates](build-and-gates.md#headless-render-test)).
Exit as met: the plain smoke test and the shell's checks under it passed on the 5060 Ti, validation
clean, in 29 s against 49 on lavapipe under Xvfb, at 1.3 ms a frame against 79; the gate's stage took
35 s with its build, from 66. The rest is the jumps' CPU work, 9 to 11 s each on either device.
The reference frames, the sanitised run and CI stay on lavapipe under Xvfb, unchanged, and a new
view, `orbit-offscreen`, is the `orbit` view drawn offscreen on lavapipe, the same PNG to the byte.
The gate passed with `--werror --asan` in 324 s.

### Neutral SDK and Manifest integration

The current priority's rows as they were closed (approved 2026-11-15), each the Manifest task it is
([delivery order](../../manifest/docs/delivery-order.md)) and the stage it unblocks.

The roadmap's status of this priority, as it was logged while the rows were worked:

Approved 2026-11-15. H02, H03, H04, H05, A01, W01 and M03 are done, in
[history](history.md#neutral-sdk-and-manifest-integration). Their
[cross-review](build-and-gates.md#cross-review) ran 2026-11-18 over `c04303c` to `96c1ace`, two
rounds: its findings were fixed (the commit after `96c1ace` records them), with the host API's
`Game::StarterShip` added. G01, the generic shell, is done, in
[history](history.md#neutral-sdk-and-manifest-integration). H07 closed Manifest's Stage 1 on
2026-11-18: its pin is `e53681e`, and the acceptance matrix passed against that fetch
([Manifest's history](../../manifest/docs/history.md)). Manifest's C01 schema contract,
C02 definitions and C03 content set (2026-11-18) named no engine gap; its C04 (done 2026-11-19)
named map fields, provisioned in `4d60ea0`, Manifest's pin; its C05 scenarios (done 2026-11-19)
named none. Its C09, Jettison's ship and station definitions shared (done 2026-11-19), named
the fit rule without a world (`Dock::Fits`), provisioned in `15e2561`. Its C06, the validator on
the host's options (done 2026-11-19), named the package rules without a world
(`Host::DescribePackages`), provisioned in `30ea3f3`, Manifest's pin. Its C07, the validation
build graph (done 2026-11-19), named none, and its C08 (done 2026-11-19) closed its Stage 2 with
none. Next is its A02, after the stage's cross-review; A01 is done here.

The content-pipeline row, as it was closed with Manifest's Stage 2:

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Content pipeline gaps | C02, C03 (2) | Each gap that Manifest's C01 schema mapping demonstrates in reflected definitions, layering or migration is provisioned with an engine fixture before C02 consumes it. C01, C02 and C03 named none (2026-11-18): layering, migration and per-mount layers are public SDK calls. C04's hull stats demonstrated one, map fields, provisioned 2026-11-19 ([history](history.md#neutral-sdk-and-manifest-integration)). C05's scenarios named none. C09's fit rule and C06's package rules, each without a world, provisioned 2026-11-19. No row of work until a later item demonstrates another. |

The Stage 3 authority row, as it was closed with Manifest's Stage 3 (A07, 2026-11-19):

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Stage 3 authority gaps | A02–A07 (3) | Each gap Manifest's Stage 3 demonstrates in the world's state, its serialisation, the definition registry or commands is provisioned here with an engine fixture before Manifest consumes it. A02 to A07 named none: the hosted state, its admission ledger, provisioning, cargo, queries and the console are Manifest's on `SimWorld`'s registry, services, saved command queue and recording, all on pin `30ea3f3`. A07 closed the stage, its save and replay pinned to one hash in every Manifest gate build ([Manifest's delivered contract](../../manifest/docs/stage-3-authority-plan.md#delivered-a07-2026-11-19)). The stage's cross-review (2026-11-19) demonstrated one: a service could not check its state against the commands a save queues, so Manifest's request ledger loaded beside a queue it did not match. `Sim::WorldService::Load` is now given the queued commands, in the order they will run, held by the `sim` suite's service that refuses a queue it does not expect, the world left as it was. |

The network row, as it was closed with Manifest's N02 (2026-11-20), the engine's provision first:

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Low-level networking | N02 (4) | The gaps N01's contract names, provisioned with an independent consumer over loopback. **Done** 2026-11-20 ([architecture](architecture.md#networking)): the `net` module, on ENet 1.3.18 and on libsodium through robinlinden's CMake build of it, static and fetched as the rest of the set is (the user's, in place of N01's `devenv.nix`); both are in the dependency probe, ENet opening a host on an ephemeral port and libsodium's BLAKE2b matching its empty-input test vector. `Net::Server` and `Net::Client`, single-threaded and polled, with reliable-ordered and unreliable-sequenced channels, a timeout, a peer limit of `maxSessions` plus `maxPending` and an ephemeral port; the hello (protocol name, version, opaque credential, a binary archive) and the game's `Verifier`, refusals carried as ENet's disconnect data (`UnknownProtocol`, `VersionMismatch`, `BadCredential`, `ServerFull`), a handshake timeout, and nothing before admission (`BadMessage`); a packet of a two-byte kind and its body under `Limits::maxMessageBytes`, which ENet's `maximumPacketSize` drops before reassembly and `Send` refuses; a bounded per-session queue (`InboundOverflow`); snapshots from a per-session `SnapshotView`, the step first, at most one a `snapshotInterval`, in unreliable fragments. Bearer tokens: `GenerateToken`, `DigestToken` (BLAKE2b-256) and `TokenMatches` in constant time. | Met: the `net` suite (six cases, stable over twenty runs) admits two clients that exchange messages and are each sent only their own view at the step it was taken, once in the interval; refuses an unknown protocol, an old version, a bad credential and a full server, and admits a reconnection; closes a flood and passes on a game's `Revoked` and a client's own close; refuses to send past the limit, a snapshot's step counted; and closes a raw ENet peer that sends no hello and one that sends a message first. The scratch consumer (`source/test/consumer`, no Manifest code) serves its race on an ephemeral port with a token's digest: a random token is `BadCredential`, a wrong version `VersionMismatch` and a flood `InboundOverflow`, then the reconnected player places its piece by request, the verifier's actor naming it, and its snapshot holds its piece and not the other's. |

Manifest's half followed the same day with no engine edit, first on a local SDK override and then
on the pin `65846bb`: its `remote` module serves a hosted world on `Net::Server`, a session's actor
resolved from its token by the verifier and its snapshot the actor's own view, and `manifest
--serve` steps the world unattended on `Time::FixedStepAccumulator`; `manifest-token` issues and
revokes tokens with `GenerateToken` and `DigestToken`, and the TUI's `connect` and `disconnect` play
on the server through `Net::Client` ([Manifest's history](../../manifest/docs/history.md#n02-the-dedicated-server-and-remote-tui--2026-11-20)).

Manifest's N03 (2026-11-20) needed no engine edit either, on the same pin: several clients at once,
an actor in up to three sessions (Manifest's D-082, its fourth closed as `ServerFull` after
admission, since a `Verifier` refuses only as `BadCredential`), a reconnect's retry under the same
sequence, `InboundOverflow` past `maxInbound` and the world stepping with no session
([Manifest's history](../../manifest/docs/history.md#n03-several-clients-reconnects-and-bounds--2026-11-20)).
One session per actor, for a variant where two would both steer, would want a `Superseded` reason.

Manifest's W03 (2026-11-20) needed no engine edit, on the same pin: its hosted order book settles on
owner and venue accounts through `Sim::SimWorld`'s systems and Manifest's own command path, with
each venue's broker quoting each turn, and its goods are counted in totes, an eighth of an IBC
([Manifest's history](../../manifest/docs/history.md#w03-the-hosted-book-on-owner-and-venue-accounts--2026-11-20)).

The movement order, provisioned ahead of Manifest's W05 (2026-11-20), the one gap its W02 named:

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Movement order | W05 (4) | `dock` and `lanes` both write `Transform` and `InFrame` with no edge between them, so registration order decided theirs and, through `lanes`, `dock`'s against `cruise` ([Manifest's step](../../manifest/docs/stage-4-trading-plan.md#the-step-in-order)); an edge between them, tested in an independent consumer that registers dock, lanes and cruise and finds no conflicting pair in `Scheduler::Ambiguities`. **Done** 2026-11-20: `Ecs::SystemDescription::afterIfRegistered` names systems one runs after when they are registered and is not ordered against when they are not, for two optional modules neither of which depends on the other; `Validate` does not ask for them, and one in another phase is refused as `after`'s is. `lanes` runs after `dock` so, giving Manifest's `dock`, `lanes`, `cruise`; a world with lanes and no docks, or docks and no lanes, is ordered as before. The sample attaches the lanes first, so its step went from `lanes`, `cruise`, `dock` to `dock`, `lanes`, `cruise`, and every pinned hash held. Met: the ecs suite orders a follower after a leader registered before or after it, validates it alone and refuses a leader in another phase; the dock suite attaches physics, steering, the spatial index, the docks, lanes and cruise in both orders and finds the three in that order and `Ambiguities` empty, and failed with the edge taken out. |

The flight out of a station's reach, provisioned in Manifest's W05 (2026-11-20), where its travel
leaves a pad for a cruise, and the cruise by acceleration for its W17 (2026-11-20):

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Out of a station's reach | W05 (4) | A cruise is refused within a station's reach (`NearStation`), and a ship lifted off a pad by the docks is still within it, so a game that cruises from a dock first flies out; the sample's trader did so in its own code, which Manifest would have copied. **Done** 2026-11-20: `Space::ClearOf` gives the `Ai::Destination` out of the reach of the station the ship left, to `kReachMargin` past it on the side of the body it is to cruise to and round the station's sphere by `kReachClearance`, in the ship's frame, or none outside the reach. The sample's `CruiseTo` calls it in place of its own, the same arithmetic, and every pinned hash held. Met: the space suite's flight from a ship behind the hub ends past the reach towards Pell, its waypoints clear of the hub, where the cruise is then accepted, with none outside the reach, for an unknown body or with no frame. |
| Controls let go with a destination | W05 (4) | A ship flown out of a station's reach by steering and then sent on a cruise kept steering's last demand, so where nothing steered it on arrival, a ship waiting in a station's queue, it flew off at that demand (Manifest's queued skiff at 86 m/s towards the station), where `Ai::ClearDestination` and a lost destination let the controls go. **Done** 2026-11-20: a cruise and a lane give up a ship's `Destination` as those do, its `FlightControls` reset; a ship with no destination keeps its pilot's. The sample's traders' pinned hash moved for it, their runs unchanged. Met: the space suite's steered ship comes out of its cruise let go and its piloted one keeps its throttle; Manifest's queued ship waits where its cruise left it. |
| Cruise by acceleration | W17 (4) | A cruise's time and its motion along its course follow an acceleration profile its `Space::CruiseDrive` may give in place of the power law, for Manifest's slow mode ([D-090](../../manifest/docs/decision-ledger.md#d-090)) while its fast Astralis keeps the power law. **Done** 2026-11-20: `Space::CruiseProfile`, the drive's optional `profile` (`Ai.CruiseProfile`): a boost acceleration for at most `boostSeconds` of the whole cruise, half at each end, and a sustained acceleration between, turning at the midpoint; `CruiseSeconds` gives its closed-form time and `CruiseCovered` the metres covered by a time. A profile's time rises as no more than the distance's square root and a slow ship chasing a station round its planet may not settle in the eight rounds, so a profiled cruise takes the fewest whole steps found, up from the solve's by doubling and down by halving, in which it covers the course they give, and is placed along it as the profile covers it, slowed to those steps; the `Cruising` keeps the profile it set out with. `CheckCruiseDrive` refuses a profile of no boost, a sustained rate negative or past the boost, negative boost seconds, neither above 0, or one not finite. An empty optional is left out of a save, so a drive with no profile saves as before. | Met: in the space suite, a profile's times and covered metres match the closed form (2 g for 4 hours and 1 g between: 21,600 s over the distance of 2 hours of boost and one sustained each way; short trips at the boost throughout; 12 g unmanned); a test profile takes Highport to Pell, 393,007 km, in 505.0 s, its hardest step 20,000.1 m/s² of a 20,000 boost (the positions' rounding), 5,000 m/s² sustained at a quarter of the way, ending at rest off Pell; a world saved mid-cruise on a profile arrives at the hash the one saved does; each bad profile is refused; every pinned hash held. |

Manifest's W16 (2026-11-20) needed no engine edit, on the pin `54fb871`: a new player's starter ship
is parked on a `Dock` pad of the scenario's neutral public spaceport, as provisioning parked it at a
home venue before, with no home region or polity
([Manifest's history](../../manifest/docs/history.md#w16-onboarding-at-a-neutral-spaceport--2026-11-20)).

Manifest's W15 (2026-11-20), the vertical slice, needed no engine edit either, on the same pin: its
console buys furs at Ore, sends the ship to Brine through the docks, the flight out and the cruise
by acceleration, and sells them there, stepping the 4.49 days, 23,298,304 steps, in 9 min 35 s of
a release build, about 25 µs a step; a server steps it in real time. A player's view of the journey
and of the book is Manifest's, over `Host::Session` and `net`'s snapshots, which go out at most
once an interval
([Manifest's history](../../manifest/docs/history.md#w15-the-vertical-slice-through-the-console--2026-11-20)).

Manifest's W04 (2026-11-20), the regional economy, needed no engine edit, on the same pin: each
region's employer works its batches in a Manifest system ordered between the expiry sweep and the
brokers by the scheduler's declared edges, and on each turn pays wages, sells to its broker and its
households buy and consume through the book; health, happiness, the batches and the pantries are
Manifest's components, saved and hashed by the world like any
([Manifest's history](../../manifest/docs/history.md#w04-the-regional-economy--2026-11-20)).

Manifest's W06 (2026-11-20), station life support and resupply, needed no engine edit, on the same
pin: a Manifest system ordered after its travel system connects a ship to its venue's life support
as it docks, from the `Ship::dockedAt` travel sets on observing a `Dock` docking, and lets go as it
leaves its pad, and off it draws the ship's own oxygen and cartridges each fixed step; refills and
cartridges come from the ship's hold, Manifest's components, saved and hashed like any
([Manifest's history](../../manifest/docs/history.md#w06-station-life-support-and-resupply--2026-11-20)).

Manifest's W07 (2026-11-20), live and stale market observations, needed no engine edit, on the same
pin: a Manifest system ordered after its travel system refreshes each player's reports of the venues
their docked ships are at as the step ends, from its own book and its record of fills, and the
reports, the fills and the starting grant are Manifest's components, saved and hashed like any. A
player's snapshot is filtered by Manifest before `net` sends it
([Manifest's history](../../manifest/docs/history.md#w07-live-and-stale-market-observations--2026-11-20)).

Manifest's W08 (2026-11-20), company-funded cargo upgrades, needed no engine edit, on the same pin: a
ship's purchased systems are a Manifest component, saved and hashed like any, and its hold is computed
from them by Manifest's effects engine. A hull's authored mass is the `Physics::Body` mass a ship is
spawned with, which Manifest's content holds to a float above 0 and to an inertia Jolt's float
arithmetic holds
([Manifest's history](../../manifest/docs/history.md#w08-company-funded-cargo-upgrades--2026-11-20)).

The rails placed once a body, provisioned in Manifest's L26 (2026-11-20), the console's long step:

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Rails placed once a body | L26 (4) | A cruising step of Manifest's trading fixture took 26 µs in a release build, about 45% of it evaluating orbits: physics placed every entity on rails by its body's orbit, chain and all, so a station's pads, decks and buggies evaluated the same station dozens of times a step. **Done** 2026-11-20: the physics step places each rails body once, at the step's end, for everything riding on it, with the same arithmetic, so a placement is the same to the bit; the cruising step is 13 µs, Ore to Brine about 4 min 50 s of a release build. What is left is spread: Jolt's update of each frame's world, the docks, the spatial index, the cruise's placing. | Met: the physics suite's hall carries three plates on one lift's rails and places the lift once a step; every pinned hash held. |

The fast-forward over quiet steps, provisioned for Manifest's L27 (the user's, 2026-11-20, after L26):

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| A fast-forward over quiet steps | L27 (4) | A cruising step of Manifest's is 13 µs, nearly all of it the engine's movement, so a 4.49-day cruise is 23 million steps a formula could place. **Done** 2026-11-20 ([architecture](architecture.md#quiet-steps), [decided](decisions.md)): `Sim::Quiet`, given with `SimWorld::RegisterSystem`, and `SimWorld::FastForward`; quiets for physics, flight, cruise, lanes, gates, docks, steering, the spatial index and behaviour trees (`Ai::TaskWake`), and the sample's markets, damage and traders' cruise. A kinematic body in a world with nothing dynamic is put where it is, asleep, its `Velocity` what the move implies. A spike first measured both worlds settling: Manifest's stations and the sample's, docked ships and all, asleep and Jolt's state the same bytes each step within 60 steps; a body's place far out, less its frame's, moving 2 to 14 times a day by up to 30 nm, which woke it; and a gate's ring, a sensor, turning every step. Manifest's own: `Breathe` draws every crewed ship's life support each step and `Observe` reports every docked fleet's venues each step, so its quiets are its work. | Met: the sim suite passes over a world's quiet steps to the bytes stepping gives at every stop, runs one step for a queued command and every step for a system with none; the physics suite passes over a lift's plate and a turning carousel's sensor for 19,997 steps in one call, the sensor still turning by its velocity, then senses a body dropped through it, the two worlds the same 240 steps on; the space suite cruises Highport to Pell past a ship parked on rails, and crosses a lane, each the same at every stop; the sample's stations, parked ships and all, an hour in a few calls; a behaviour tree's waits and a task's wake, ten rounds in 54 calls. Three pinned hashes moved, the dock's approach and deck and the sample's traders: half an hour of the traders' galaxy (every trader's place, runs and cargo, every market's stock and prices) is the same to the bit before and after. Manifest's `company` and `fixture` pins move with it; its step budget is unchanged (13.4 µs). |

Manifest's W09 (2026-11-20), the complete trading fixture, needed no engine edit, on the same pin:
one command stream from purchase to upgrade, through Jettison's departure, cruise and docking, saved in
flight and loaded, replayed from the engine's record of applied commands, from the directory and the
pack, reaches one world hash in Debug, Release, sanitised and the TUI off; a cruise the engine refuses,
for a ship with no `CruiseDrive`, fails its journey with the ship and cargo where they are
([Manifest's history](../../manifest/docs/history.md#w09-the-complete-trading-fixture--2026-11-20)).

Manifest's W10 (2026-11-21), standing trade policies, autopilot and NPC jobs, on pin `0f973b2`:
each ship's job trades through the players' own commands sent from a system, its quiet passing over
a blocked job's wait. Its zero-client run found a ship that circled the point above its pad until the
pad lapsed; the docks now stop a ship out along its pad's normal first, a stopgap
([carried forward](roadmap.md#then-robustness-tools-and-what-a-mod-can-reach)). Its L28 needed no
engine edit ([Manifest's history](../../manifest/docs/history.md#w10-standing-trade-policies-autopilot-and-npc-jobs--2026-11-21)).

Durable directories and a file lock, provisioned for Manifest's R02 (2026-11-24), and with them the
message longer than a packet that its Stage 4 review booked for the next engine change:

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Durable directories and a world lock | R02 (5) | Manifest's recovery writes a world's snapshots, journal and catalogue under one directory, by one process. `Core::WriteFileDurably` made a missing directory and flushed only the file's own, so a new hierarchy's entries could be lost in a power cut with the file in them; and nothing held a directory to one writer. **Done** 2026-11-24: `Core::CreateDirectoriesDurably` makes each missing directory outermost first and flushes its parent, and `WriteFileDurably` makes its directory with it, so every save, setting and mod list is covered; `Core::FileLock`, a file locked with `flock`, refuses a second holder in the same process or another, and is the kernel's to let go, so a crash leaves nothing stale. Not provisioned, since nothing asked: a write that refuses to replace a file (a world's one writer checks first), fault injection under the system calls (Manifest injects at its own seam), and a lock's holder named in its file. | Met: the `core` suite makes a hierarchy three deep once and refuses one through a file; holds a lock against a second holder in its process and across a move, and against another process, and takes it once that process is killed. Under `strace` each `mkdir` is followed by its parent's `fsync`, and a write's order is the partial file's `fsync`, the `rename`, the directory's `fsync`. |
| A message longer than a packet | Stage 4's review (4) | Manifest's snapshot of the trading scenario was 63,567 bytes of the 64 KiB `maxMessageBytes`, past it with ten resting orders, and the roadmap booked splitting and reassembly. **Done** 2026-11-24 with a test and no code: a message is an ENet packet, which ENet splits into datagrams and reassembles already, so the limit is a setting a game raises (`Limits::maxMessageBytes`). Measured on loopback: a reliable message of 1 MiB arrives; an unreliable one, its fragments sent at once into the receiver's 256 KiB socket buffer, arrived twenty times of twenty at 192 and 256 KiB and never at 384 KiB or more. Past that a view wants deltas, not provisioned ([architecture](architecture.md#networking)). | Met: the `net` suite sends a 192 KiB snapshot and a 1 MiB reliable message and compares each byte for byte, thirty runs of thirty. |

Manifest's R02 is done on the pin `ea66afe` (2026-11-24): its world's directory made durably and held
by one server on the two, its token file written durably, and its limit 192 KiB, a view of 69,829
bytes reaching its client ([Manifest's history](../../manifest/docs/history.md#r02-durable-io-directories-and-the-world-lock--2026-11-24)).

Manifest's R03 and R04 (2026-11-24), its journal and snapshots, needed no engine edit, on the same
pin: its batches, snapshots and catalogue are `Core::WriteFileDurably`'s files under
`Core::Fnv1a64`, a snapshot `SimWorld::Save`'s bytes, and its recovery `SimWorld::Load` and
`FastForward` with the requests queued again. Its user settled that a crash may lose up to 30 s of
play, so the journal is written every 30 s after the steps it records and not before each
([its journal](../../manifest/docs/stage-5-recovery-plan.md#the-journal-and-its-snapshots-r03-r04-2026-11-24)).
Its R05 (2026-11-24), a clean stop, backups and restore, needed none either: a backup is the
snapshot's bytes beside the world's identity, written with `Core::WriteFileDurably`, and a restore
copies one into a directory holding only its `Core::FileLock` and recovers it as a start does
([its backups](../../manifest/docs/stage-5-recovery-plan.md#shutdown-backups-and-limits-r05-2026-11-24)).

ImGui's backends out of `Devtools`, provisioned for Manifest's R07 (2026-11-24), the gap its R06 named:

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| ImGui's backends out of `Devtools` | R07 (5) | Manifest's bundle ships its TUI, which never draws, and a consumer of `Devtools` should need no window system or Vulkan ([its contract](../../manifest/docs/stage-5-recovery-plan.md#the-runtime-distribution-contract-r06-2026-11-24)); `imgui` compiled ImGui's GLFW and Vulkan backends into itself and linked `glfw` and `Vulkan::Vulkan` publicly, so the TUI had `libvulkan.so.1` among its `NEEDED`. **Done** 2026-11-24: `imgui` is ImGui's core and its FreeType rasteriser, and `imgui_backends` the GLFW and Vulkan backends on it, linked by `render` alone, the one caller of `ImGui_ImplGlfw_*` and `ImGui_ImplVulkan_*` (`ImGuiRenderer.cpp`). | Met: `devtools_alone` (`source/test`), linking `Devtools` alone and drawing a console in one ImGui frame with no renderer, needs only `libstdc++`, `libm`, `libgcc_s` and `libc`, and runs with no Vulkan loader or GLFW among its mappings; the `devtools` suite's binary needed `libvulkan.so.1` before. Manifest's `manifest_tui` on the local engine needs neither, where on the pin `ea66afe` it needed `libvulkan.so.1`, and ran on a terminal with neither mapped. |

Its R07 (2026-11-24), the distribution, needed no engine edit beyond that split: its release build
links its four executables to run outside the Nix toolchain on glibc 2.39 or newer, and finds its
content from `Platform::ExecutablePath` and its writable places from `Platform::ResolvePaths`, both
already the engine's ([its distribution](../../manifest/docs/stage-5-recovery-plan.md#the-distribution-r07-2026-11-24)).
Its R08 (2026-11-24), clients reconnecting to a server restarted from its journal and a world
recovered with none, needed none either: what it proves is its journal's and its request ledger's,
on `net` as N02 left it ([its recovery](../../manifest/docs/stage-5-recovery-plan.md#reconnect-and-recovery-r08-2026-11-24)).

A world's saved names, provisioned for Manifest's R09 (2026-11-25), the gap its R01 named:

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| A world lists what its save holds | R09 (5) | Manifest's close of its Stage 5 pins its state inventory, every component and service a hosted world saves, to the names the save lists ([its inventory](../../manifest/docs/stage-5-recovery-plan.md#the-state-inventory)); a world named its components only in its save's first bytes, and its services nowhere but inside it. **Done** 2026-11-25: `SimWorld::ComponentNames` and `ServiceNames`, each in the order a save holds them, the names a save lists them under. | Met: the `sim` suite's service case reads its four components and its service back by name, in order, and a second service after the first. Manifest's `host` suite pins a hosted world's 50 components and 3 services to its state inventory on the pin `13c1bec`. |

Its R09, the close of its Stage 5 (2026-11-25), needed that provision alone
([its close](../../manifest/docs/stage-5-recovery-plan.md#stage-5-closed-r09-2026-11-25)). Its saves are
to leave the thread that steps the world (its D-115, the user's); a writer for that, if reusable, is
the engine's.

Four gaps Manifest's Stage 5 cross-review found (2026-11-25), each provisioned here:

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| A world's saved names owned | Stage 5 review | `ComponentNames` and `ServiceNames` returned views into names a later registration could move. **Done** 2026-11-25: they return copies. | Met: the `sim` suite's service case compares copies. |
| A server's last messages delivered | Stage 5 review | Destroying a server closes every connection at once, so the replies of a clean stop's last step were dropped. **Done** 2026-11-25: `Server::Closing`, polled after closing each session until its client has acknowledged what it was sent. | Met: the `net` suite sends a session a message, closes it, polls until it is no longer closing, and its client has the message and the reason. |
| One XDG directory resolved alone | Stage 5 review | `ResolvePaths` resolves all four directories, so a program writing only configuration needed HOME or every XDG variable. **Done** 2026-11-25: `ResolvePath`, one directory, needing HOME only when its own variable is not absolute. | Met: the `platform` suite resolves configuration from its variable alone and refuses the cache with neither. |
| A directory made private from the start | Stage 5 review | `CreateDirectoriesDurably` made a directory with the default mode, so a caller changed it afterwards, and a flush that failed between the two left it as made, open to everyone, and taken as already there on the next run. **Done** 2026-11-25: it takes the permissions to make each directory with, under the umask, the default every permission as before. | Met: the `core` suite makes two directories 0700 under one left 0750, and through `new/..` makes `new` and `made` 0700 and leaves the one named twice as it was. |

Manifest's W18, W11 and W19 needed no engine edit, and later W12, W13 and W14 (2026-11-23), competitive
procurement, regional governance and the close of its Stage 4, on the pin `14fc23a`. Its R01
(2026-11-24), the save and compatibility contract, a planning task, needed none either and names no
gap ([its contract](../../manifest/docs/stage-5-recovery-plan.md#save-and-compatibility-contract-r01-2026-11-24)):
a world cannot list what it registers but through its save's first bytes, which Manifest's R04 may
ask for. Its R06 (2026-11-24), the runtime distribution contract, a planning task, selects a standalone
bundle for glibc 2.39 or newer, its TUI in it, and names one gap
([its contract](../../manifest/docs/stage-5-recovery-plan.md#the-runtime-distribution-contract-r06-2026-11-24)):
`imgui` carries ImGui's GLFW and Vulkan backends, so a terminal consumer of `Devtools` links
`libvulkan.so.1`, provisioned [above](#neutral-sdk-and-manifest-integration) before its R07. The cruise coast phase, provisioned for Manifest's
W21 (2026-11-22), fuel burn, its slow runs coasting:

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Cruise coast phase | W21 (4) | A profiled cruise may thrust for only part of its time and coast between, for a burn that saves fuel ([D-107](../../manifest/docs/decision-ledger.md#d-107), [W21](../../manifest/docs/stage-4-trading-plan.md#delivered-w21-2026-11-22)); the player chooses it as a percentage of the time, never in seconds (the user's, 2026-11-22). **Done** 2026-11-22: `Space::CruiseProfile`'s optional `thrustShare`, the share of the whole cruise's time the drive thrusts, above 0 and at most 1, half at each end with the boost first, the ship coasting between at the speed it reached: a share rather than seconds, so it means the same at any distance. Each end's thrust t solves 2(1 − share) t v(t) + 2 share x(t) = share × metres, which rises with t, in closed form on the boost and as a quadratic's non-cancelling root past it; `CruiseSeconds` and `CruiseCovered` take the coast, and `CruiseThrust` gives each end's seconds at the boost and at the sustained acceleration, which a game prices its fuel from. None, or a share of 1, takes the sums it took before, so its times are the same to the bit, and an empty optional is left out of a save. `CheckCruiseDrive` refuses a share not above 0, past 1 or not finite. | Met: in the space suite, a share f of 1 g throughout reaches √(f / (2 − f)) of the full speed and takes (1 + s²) / 2s as long, Ore to Brine 4.65 days at all of it and 5.37 at half, to 10⁻¹²; with the 2 g boost, a quarter of a long cruise thrusts past the boost and of a short one on it, each end's covered metres meeting the thrust's closed form; a share of 1 gives the times and covered metres of none exactly; Highport to Pell thrusting a quarter of the time on the test profile, 393,013 km in 723.2 s, hardest step 19,999.6 m/s² of a 20,000 boost, 0.014 m/s² at the midpoint, coasting at the speed reached slowed to its whole steps, ending at rest off Pell; a world saved mid-coast arrives at the hash the one saved does; each bad share is refused. In the sample suite, a mod patches the shuttle's drive (`defs/drives/shuttle.patch.yaml`) with a 1 g profile thrusting a quarter of the time, read, layered and checked as it loads, and a share of 1.5 is refused. Every pinned hash held. |

Manifest's W21 slice 4, its policy choosing the burn per hour, sent traders between the stations of
one orbit, and found one engine gap, provisioned (2026-11-22):

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| A station's reach where physics placed it | W21 (4) | A ship flown out of a station's reach by a game's system after physics is accepted for its cruise. Manifest's ships leaving Ore Vale for Highport or Steppe, on Ore's one orbit, reached `ClearOf`'s point, 3,063 m from the station as placed, and stayed there: physics places every `Transform` for the step's end and `ClearOf` measured them against the bodies at the clock's `Now`, a step before, when Ore was some 500 m back on its way round the star, so the ship was 2,543 m out of a 2,763 m reach and given the same flight again. **Done** 2026-11-22: `PhysicsWorld::PlacedAt`, the end of the last step or skip physics placed for, none after a start or a load, when it is `Now`; `ClearOf` and `Cruise`'s refusals near a station and already there measure at it. A cruise's course keeps the clock's `Now`, as its steps count from it. | Met: in the space suite, a system after physics puts a ship 10 m outside the reach of a station as placed, behind it as it goes, which moves over 100 m in the step; it is given no flight and its cruise is accepted, and without the change both fail. Every suite and every pinned hash held. |
| Space extraction | before W12 (the user's, 2026-11-22) | The engine keeps only what has a neutral name and serves more than one genre; the space setting moves into Manifest, which tests all space code; the space sample is retired; jump lanes and gates stay as portals for Manifest's stargates; neutral modules use none of the setting's words; every Manifest pin holds. **Done** 2026-11-23, in eight slices ([the plan](space-extraction-plan.md)): the vocabulary check (`neutral_words`); the sample, Jetsam, `spacescene` and the cargo deck retired, render_test the shell with no game over static scenes; the space-only reasons, the scene's star system and the save's migration in neutral words; `transit` (motion profiles, passages, portals, slots, gated portals) with consumers of no genre, the cruise, lanes and gates rebuilt on it with no pin moved; the docks' platforms, berths and vehicles; `celestial` and `space` moved into Manifest's `space`; the saved names renamed, pins moved once by the names alone; and the close-out: the last names and documents, Manifest's boundary assessment, the consumer matrix and a three-round cross-review, whose migration findings ended with formats changing in place until player testing ([decided](decisions.md)). Manifest pinned to `14fc23a`, its gate passed on the fetched pin. |

| Delivery | Manifest task (stage) | Exit criterion |
| --- | --- | --- |
| Public host | H02 (1) | A public host owns the VFS, the definition registry and one world, with ordered start-up, cleanup after a failed start and lifetime tests; a game's composition is passed in explicitly. **Done** 2026-11-18: `Jettison::Host` (`Session`, `Game`, `StartupError`), and the package description and engine check moved there from the shell's mounts; the host suite starts a session from a directory and its pack, fails each phase in turn with no later hook run, tears down world before game, and runs two sessions side by side and one after a failure. |
| CPU-only build | H03 (1) | The engine configures without ImGui/Vulkan/glslc/sample assets, its CPU modules and their suites build and pass, and the packer stays available. **Done** 2026-11-18: `JETTISON_PRESENTATION` (ON by default) gates the presentation modules, the assets, Flotsam, Jetsam, the render harness and the libraries only they use, replacing `PLUGIN_IMGUI`, which had returned before the engine was defined; the `clang-cpu` preset refuses `find_package(Vulkan)`, and its fresh configure fetched none of GLFW, FreeType, RmlUi, miniaudio, ImGui, stb, VMA or KTX, built in 720 steps and passed its 22 suites; the gate configures, builds and tests it. Splitting renderer resources from example content moved to H05, where the sample's content leaves the engine. |
| Independent CPU consumer | H04 (1) | A non-space CPU consumer defines, steps, saves, loads and tears down through the public host, with no galaxy, transform or star system. **Done** 2026-11-18: `source/test/consumer` fetches the engine with `JETTISON_PRESENTATION` off and Vulkan refused, and runs a race (a `Piece` definition read from its own content directory, a `Token` component, a `Place` command, a stepped system that rolls the world's generator) through `Host::Session`: a second session from the same seed and commands reaches the same state hash, a third loads the save and reaches it too, and teardown runs world before game. Its link line names no presentation library and no sample content; it needs an explicit `JETTISON_SOURCE_DIR` or `JETTISON_GIT_TAG`, the `main` fallback gone. It is a manual check, not a gate stage (a second fetch of the dependency set). |
| Sample extraction | H05 (1) | Sample trading/UI/assets reside in sample/test targets outside engine code; renderer resources and example content have separate targets/manifests; Jetsam selects the example explicitly and Flotsam does not by default; neutral start-up registers no traders, markets or fleet; existing sample checks still pass. **Done** 2026-11-18: `App::Game` (`app/Game.h`) is the composition seam `RunRenderApplication` takes, its hooks the shell's points from registration to the render test's game UI stage, reading the shell through `App::Shell`; Trading, Traders, the fleet's shipyard, the market screen and the parked ships are `Sample::SpaceGame` in `source/samples/space`, with their suite (`space` then, `sample-space` since W01), linked by Jetsam and the render test; Flotsam composes none, and `GameTests` holds a shell of no game to no market, trader or fleet type. The assets split: `assets/` is the engine's alone (shaders, fonts, the saves screen, the engine modules' strings), and the sample's content is `source/samples/space/assets` with its `mod.yaml`, each staged by `jettison_add_assets` and packed (`jettison_assets_pack`, `jettison_sample_space_pack`); the shell mounts the content as `base` over the engine's `engine`, refusing content for a newer engine, Jetsam and the render test taking the sample's by default (`jettison_use_content`) and Flotsam none (`--content` names some). Run with none, Flotsam started on an empty scene and drew it, with no warning. The app suite's `Game` cases hold the engine's assets to their four directories with none of the sample's paths in them, and the packed smoke test runs from both packs and checks the content's description. The traders' pinned hash is unchanged, and every suite, the smoke tests and the reference frames pass. |
| Frame and save metadata seams | A01 (3) | Neutral frame/partition identity and game save metadata sections, with migrations that preserve the existing fixtures. **Done** 2026-11-18 ([decided](decisions.md)): `Sim::InFrame`, `kNoFrame` and `Sim::FrameId` in place of `InSystem`, `kNoSystem` and `SystemId`, a frame the game's partition and none for a game of one, the snapshot's sample carrying it as `frame`; the world's version 4 renames the component, and `InFrame`'s own chain (version 2, which `RegisterComponent<T>()` now takes from a type's static `Migrations()`) its field. `SaveInfo::details`, the game's key and value strings, replace `galaxy` and `system`, the save game's version 2 moving a version 1 save's into them, and the shell writes, lists and checks its own as `galaxy` and `system`. `replay-layout3.save` (a world of version 3 of `Sim.InSystem`) and `save-v1.save` load unchanged, the sim suite turns this build's save back into version 3's names and loads it as `InFrame`, and the round trip of a save's details holds keys of no space game. The eight pinned hashes moved with the version and the names alone: with the world written as version 3 under the old names, every one passed at its old value. |
| Generic module boundaries | W01 (4) | Non-space physics and scene consumers require no celestial definitions; generic AI is independent of space transit; the docking/berth seams Manifest needs (pad classes as data, Manifest's D-044) are provisioned. **Done** 2026-11-18 ([decided](decisions.md)), in five commits: physics, AI, scene, the dock's pad classes, the dependency checks. Physics done 2026-11-18: a frame's rails, local frames and gravity are the game's `Physics::FrameModel` (`PhysicsWorld::DefineFrame`), physics depends on no celestial, and a star system is the new `space` module's model (`Space::DefineStarSystem`), with the flight and contact suites that fly in one; the physics suite's room stacks crates in its own gravity and carries one on a lift's rails, and saves and loads, with no star system. The sample's suite is `sample-space`, the module's `space`, which the sanitised run takes. The pinned hashes are unchanged. AI done 2026-11-18: cruise, gates, lanes, transit and galaxy routes are `space`'s (namespace `Space`), courses round spheres stay the AI's (`ai/Courses.h`, which the dock flies by), and `ai` depends on no celestial or space module; their types keep their `Ai.*` names, and `Ai::Reason` keeps the transit's reasons, whose `reason.ai.*` keys are authored text and saved in events. Steering's suite flies round Halcyon's station through a test-only link to `space`. Every pinned hash and the script bindings are unchanged. Scene done 2026-11-18: the system scene and the jump transition are the new `spacescene` module's (namespace `SpaceScene`), with their suite, and the scene's proxies of the simulation's entities stay the scene's (`scene/SimProxies.h`); the scene depends on no celestial module. Its file keeps its optional `system` and `celestial` fields, authored data a scene without them never reads. Pad classes done 2026-11-18: a pad class is a definition (`Dock::PadClassDefinition`, `defs/pads`: its largest hull, its pad's size and its rank, checked by `CheckPadClass`), loaded before the hulls and layouts that name it; a hull, a layout's pad and the `Dock.Pad` component name their class, and a hull lands on a pad of its class or one ranked above (`Docks::Fits`), the shell's dressing and its free pad asking the docks. The sample's S, M, L and XL are its data at their old sizes. `Dock.Pad`'s chain (version 2) turns version 1's enumerator into its name, with a test of it; the dock suite gives a game's own compact berth and bay by rank, a pod the berth and a second tug queued for the bay, and refuses a hull or a pad of a class not defined. Three pinned hashes moved, the dock's approach and deck and the sample's traders: every world the old build's dock and sample suites hashed, saved by it, loads through the chain into this build and saves to the same bytes as this build's own. The dependency checks done 2026-11-18: every configure, once every target is added (`jettison_check_optional`, `source/CMakeLists.txt`), refuses an engine module with a sample's target in its link closure, and holds each module but `celestial`, `space`, `spacescene` and their users, the dock and the shell, to having none of the three in its closure or its public headers, a new module held to it unless it is listed; the sample linked by the dock, a celestial dependency given to `spatial`, and a space header included in physics' public one each stopped the configure. Met: physics' room and the scene need no celestial definition, the AI no space transit, and Manifest authors its compact berth as a pad class. |
| Content pipeline: map fields | C04 (2) | A reflected field may be a map, read, written and layered as YAML and binary, so Manifest's hull stats (`std::map<EffectStat, double>`) are authored as a mapping. **Done** 2026-11-19: `std::map` and `std::unordered_map` keyed by a `std::string` or a registered enumeration, of any type an archive writes, are a mapping in YAML and a count of key and value pairs in binary, in the order of their keys' text in both, so an unordered map writes the same text and bytes whatever its own order; a patch merges a map key by key and a null removes a key, as for any mapping. A set, or a key of another type, is refused as `UnsupportedTypeError`; a key that names no enumerator, or one given twice, as `FormatError` with its path. `std::array`, which wrote but could not be read, reads in place from exactly as many elements; `std::deque` and `std::list` already read as sequences. | Met: the serialise suite round-trips maps by string and by enumerator, of leaves, classes and sequences, quoted and empty keys included, with `std::array` and `std::deque`, writing the same text and bytes twice, and the same for an unordered map filled in two orders; it refuses a map given a sequence, an unknown enumerator key, a duplicate key, a bad nested value and an array of the wrong length, each naming its path, and a set, an integer key and an unregistered enumerator key as defects. Its layer cases merge a map key by key, a null removing one. |
| Content pipeline: fit without a world | C09 (2) | Manifest's content validation asks whether a hull lands on a pad class with no world to hold a `Docks`. **Done** 2026-11-19: `Dock::Fits` (`dock/Definitions.h`) is the rule over a `PadClasses`, beside `CheckHull`, and `Docks::Fits` asks it over the classes defined, so the two cannot disagree. | Met: the dock suite asks it of the sample's classes with a game's skiff ranked below, a 9.8 x 6.4 x 1.8 m skiff fitting its own class and a corvette pad, a frigate transport refused both and given a destroyer's, and a class not given, as the pad or the hull's own, fitting nothing; every existing fit case is unchanged. |
| Content pipeline: package rules without a world | C06 (2) | Manifest's validator holds its sources to the host's package rules without starting a session, which builds a world. **Done** 2026-11-19: `Host::DescribePackages` (`host/Session.h`) describes and checks each source of a file system in mount order, a newer engine or a `requires` not mounted before it refused as the content phase's `StartupError` naming the source, and the session's content phase calls it, so the two cannot disagree. | Met: the host suite describes three sources, one requiring the first and one with no `mod.yaml`, and refuses the requiring one mounted alone, naming it; every session case is unchanged. |
| Maintenance: shared environment | M03 (0) | Done: `devenv.nix`, `devenv.yaml` and `devenv.lock` match Manifest's. `lazygit` joined the SDK shell (5ebfc9b) and left it on 2026-11-18: it is on the desktop, not in either project's shell. The pin-based parity check (T5, M04) is Manifest's. |
| Generic shell | G01 (engine; Manifest's graphical client, L15, stands on it) | **Done** 2026-11-18, in the five sub-phases below. The shell is generic and the gameplay a game's ([decided](decisions.md)). `app` and `dock` come off `jettison_check_optional`'s users, so no engine module but `celestial`, `space` and `spacescene` links one of them, held at every configure. The shell's galaxy and star systems, stations, docking commands, piloting, cruise and jump, traffic, chase and cockpit cameras and flight panel move to the space sample, composed through `App::Game` on the shell's seams (scene loading, the controlled entity, a camera rig, console commands, panels); a hull's cruise drive is the game's component, not `Dock::HullDefinition`'s. Physics' and the AI's `ship` fields and `Thrusters` take neutral names, their saved names kept or migrated so no save breaks. A non-space game composed into the shell, with its own scene, input and camera, runs in the smoke test; the space sample's smoke test, reference frames and pinned hashes are unchanged. Its [cross-review](build-and-gates.md#cross-review), one over G01 from `f3fbbbb` (the user's, 2026-11-18), found three: a ground save loaded with a key held kept its rover driving, a sequence's fade stayed on in a game with no jump to clear it, and the shell heard only an event whose field was `ship`. Each was fixed with a test that failed first: the ground game takes the loaded controls as those last sent, the shell clears a sequence's fade the frame it ends, and a game says which events the player hears (`Game::Hears`, in place of `Followed`). Its second round, which the user extended to a third, found two: the rover's proxy was the scene file's own entity, whose transform the sync moved and a saved scene kept, and the smoke test's speed and turn still hung on the frame rate. The game now builds its proxy as built (not authored) runtime state, and a piloting stage waits until the game says what it flew has moved (`SmokeActions::piloting`). Met: each sub-phase's criterion, below, G01f's the last. |

G01 was worked in five sub-phases (what was G01c is in G01b), each a commit that left the gate green and the space sample's run,
frames and pinned hashes as they were. The code that moves goes to the space sample
(`source/samples/space`), the game app that extends the shell; what is reusable across space games
stays in or goes to `space` and `spacescene`.

| Sub-phase | Work | Exit criterion |
| --- | --- | --- |
| G01a: the shell's seams | **Done** 2026-11-18. `App::Game` takes what the moving code needs, with the shell's own space code still where it is: its frame systems in the shell's phases, ordered by the shell's systems' names (`RegisterFrame`, on the `Ecs::Scheduler` the shell validates); the scene loaded and about to unload; its own save sections and save details, and a load's refusal of a save not its own; its dev-tools panels; the render test's stages it drives. `App::Shell` gives the scene, the snapshots and the time shown between them, the debug lines, the screen's fade, the asset tree and resources, the console and input, the active camera, and the entity followed in place of the ship. | The app suite composes a test game whose frame system runs in its phase after the one it names, whose save section round-trips and whose scene hooks are called, with no space module in it. Met: `GameTests`' seam game, its system after `sim.sync`, its section and detail round-tripped, a save it refuses leaving the world's state hash as it was, and one naming no system failing the shell's construction. |
| G01b: space out | **Done** 2026-11-18, with what was G01c, since the shell's ship and stations read the system shown wherever they act, and a split would have needed an interface to delete. The galaxy and its systems, the system scene, jumps, lanes, transit, cruises and routes, the jump streaks and fade, the galaxy panel and the orbits' debug lines; stations and their dressing, docking commands, the player's ship, the traffic, the fleet and the helm, piloting, its cameras and context, the flight panel, the thruster, buggy, pad-lamp and hum syncs and the cockpit mask; the render test's pilot and jump stages: all moved to the sample's space layer (`Sample::SpaceLayer`), which attaches lanes, gates, cruises, the spatial index, steering, the docks and the behaviour trees in the shell's old order, so their systems step as they did. `App::Game` lost its space hooks (`OpenGalaxy`, `StationOpened`, `StationsOpened`, `StarterShip`, `FitShip`, `DescribeShip`) and `ShellWorld` all but physics; it gained the proxies' sync, the entity followed, an entity's name, where a click picks, the load applied and the shutdown, and `App::Shell` the command line, a resync, the scripts, a game's input context, a target framed, a sequence played, the pick cleared and whether debug lines are shown. The shell's orbit camera frames any entity a game marks (`App::OrbitTarget`), and picking and its outline stay the shell's; each of the shell's frame systems names the one before it, so the sample's go where the shell's were. The save's `galaxy` and `system` details and its `Shell.State` section, under that name, are the sample's. | Met: `app` links and includes no `celestial`, `spacescene`, `space`, `dock` or `ai`; `save-v1.save` loads in the sim suite, and the sample suite loads `shell-before-g01.save`, written by render_test at `cb59478`, its shell section and world into the sample's; every reference frame matches to the pixel, and the pinned hashes and the smoke test hold. |
| G01d: docks without space | **Done** 2026-11-18. A hull's cruise drive is no field of `Dock::HullDefinition`: the sample's drives are their own definitions (`Ai.CruiseDrive`, `defs/drives/<hull>.yaml`), read once as the docks' hulls are (`Sample::ReadDrives`) and given to each ship it makes, and their check is `space`'s (`Space::CheckCruiseDrive`). Hull files are of version 2; one of version 1, which may give a drive, loads through `Dock::HullMigrations` with it left out. `dock` dropped `space` and `celestial`, which it did not use; its suite links `space` for the star system it flies in. | Met: `dock` links no `space` (or `celestial`); the dock suite loads a version-1 hull with a drive, and refuses a drive in a version-2 one; the space suite refuses each bad drive; the sample suite gives each parked ship its hull's drive, and every hull has one. |
| G01e: neutral names | **Done** 2026-11-18. Physics' and the AI's `ship` fields, on flight controls, the helm's and the destination's commands and events, are `vehicle`, and `Physics::Thrusters` is `Physics::Handling`. Their saved names are kept (`"ship"`, `Physics.Thrusters`), so a save, a recording, a script and a sound definition read them as before, with no migration. | Every pinned hash and save fixture holds. Met: the gate passes with no pin or fixture changed. |
| G01f: a non-space game | **Done** 2026-11-18. The ground sample (`Sample::GroundGame`, `source/samples/ground`, [sample game](sample-game.md#the-ground-sample)), run by its own `ground` executable: a rover on a slab under its own frame's gravity (`Physics::FrameModel`), at the player's helm and driven through the flight model by the game's input context, its proxy the scene's, a chase rig of the game's behind it, and its content's `scene/main.yaml` loaded as the game's default scene (`Game::DefaultScene`, which the space sample gives as `scene/orbit.yaml`). The smoke sequence's jump is a game's to drive, with no jump stage for a game that drives none. `jettison_check_optional` lost its users: `app` and `dock` are held to no space module. | The G01 row's exit criterion. Met: the ground sample links the shell alone and the configure holds `app` and `dock` to no `celestial`, `space` or `spacescene`; the smoke test runs it after the space sample's scenes, its rover driven (0.9 m in the packed run) and every stage's verdict passed, with the space sample's smoke test, reference frames and pinned hashes as they were; its suite (`sample-ground`) drives the rover ahead and turning on the ground, and the app suite's smoke sequence passes a game that drives no jump. |

## System catalogue, entries 1–31

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
*Filled in* (8a): a version mismatch runs the format's migrations when the reader is given them
(`serialise/Migration.h`), and `FormatVersionError` says why a version is refused (a later build's,
or no step from it) and carries both versions; see [entry 25](#25-persistence--phase-8--engineserialise-enginesim).
`BinaryWriter::WriteBytes` puts one archive inside another, as a save game holds the world's.
*Filled in* (11a): `YamlArchive::ReadLayered` reads a document in layers, the first whole and each
later one a patch merged over those below it, each migrated on its own first; see
[files, packs and mods](architecture.md#files-packs-and-mods). A YAML migration step edits only what
the data has, since a patch leaves out what it does not change.

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
*Filled in* (8a): `Write` is durable, through `Core::WriteFileDurably`: the partial file flushed
before the rename and the directory after it ([decided](decisions.md)). The shell mounts the
user's data directory writable over the base, and saves go there.
*Filled in* (11a): a mount is a directory or a pack file (`vfs/Pack.h`, zstd), behind the same
calls; `Find` (a file's mount, time and size) replaces `Resolve`, since a file in a pack has no host
path, and `ReadLayers` reads every mount's file at a path. The shell mounts the user's mods between
the base and the user's directory. See [files, packs and mods](architecture.md#files-packs-and-mods).

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
resolved path (its mount, since 11a), time and size from `Update()`, so a mod that shadows a base file counts as a
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
*Filled in* (8a): definitions migrate ([decided](decisions.md)): `Load` takes a type's YAML
migrations, and a file of an earlier version is brought up to the current one as it is read. The
world's save migrates from version 1 (before its services), and a save game (`sim/SaveGame.h`) holds
the world beside its info and the game's sections.
*Filled in* (11a): definitions layer finer than a file: a `.patch.yaml` beside a definition merges
over the layers below it, a whole file still replacing them, and each change a mod makes is logged
([decided](decisions.md)).

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
*Filled in* (7b): a joystick's raw axes and buttons are devices of their own, by GLFW's slot
(`Joystick1.Axis2`, hats among the buttons), so a HOTAS's stick and throttle bind apart; the wheel is
the shell's one callback, installed over the ImGui backend's and passing every scroll on to it, and
held back while ImGui has the mouse. The flight context's bindings are in
[entry 22](#22-physics--phase-7--enginephysics).
*Filled in* (8a): the shell loads the user's bindings from its settings file at start-up and writes
them whenever the player changes one (`bind` and `unbind` in the console). A binding's joystick
follows its device, named by GUID (`input/Joysticks.h`, [decided](decisions.md)), and
`SaveBindings` takes out of the settings an action reset to its defaults, which before stayed in them
and came back on the next load.

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
*Filled in* (7b): the chase and cockpit rigs are on a piloted ship through
`PilotCameraComponent`, which the game adds to a camera and the scene file never holds, both rigs
following the ship's interpolated proxy so switching between them is a transition, and over the
camera's orbit rig while it has one.

### 19. Space rendering · phase 5 · `engine/render`

Clustered forward shading, as [architecture](architecture.md#rendering-clustered-forward)
describes. PBR, image-based lighting, the sky and tonemapping were done in 5b, the clustered
lights in 5c, the sun's cascaded shadows in 5d, and culling, instancing, levels of detail and the
frame's budget in 5e. Point and spot lights have finite ranges with windowed inverse-square
falloff, and are assigned to a grid of view-space clusters by compute: 32 × 18 × 48, tuned in 5e
against a station of 265 lights, and covering the cluster range. The directional sun is separate,
in the frame constants, with cascaded shadows. The opaque depth prepass was measured in 5e, and is
kept and off ([decided](decisions.md)). Also in this phase: PBR (glTF metallic-roughness) with image-based lighting
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
grid by measurement (6c, [decided](decisions.md)). Jumps (6d): an entity's system is
`Sim::InSystem`, a system scene's traffic is its own system's, and `JumpTransition` replaces the
scene shown behind a fade, reading the destination's files on the I/O worker, checking them before
anything is touched, and holding the fade until the new system has streamed in. The simulation
steps through a jump as through any frame; the stepper is not resynced, since the world did not
change.
Left for later: the index kept from the simulation (9b), a jump in the simulation (9a), a jump's
particles (10b), a destination prefetched before it is shown, and a body's look as data, all under
[carried forward](roadmap.md#carried-forward).

### 22. Physics · phase 7 · `engine/physics`

Jolt in double precision. Rigid and static bodies; box, sphere, capsule, cylinder, convex hull
and mesh shapes; sensors; contact events to the simulation; ray and shape casts. It steps inside
the simulation's fixed step, and the presentation world interpolates. Jolt is built with
`CROSS_PLATFORM_DETERMINISTIC`, so Debug and Release builds step identically, and with RTTI and
exceptions on to match the engine. Its public instruction-set flags make x86-64 with AVX2 the
engine's CPU baseline from this phase on, as decided; the start-up CPU check (entry 6) that makes
it safe has been in place since phase 2.
*Library*: Jolt Physics.
*Filled in* (7a): Jolt left the planned set; `physics` links it privately, with assertions in Debug
trees and its profiler zones on Tracy's timeline. `PhysicsWorld` is a `Sim::WorldService`, a kind
of state the simulation world gained for it: owned by the world, saved after its components and
hashed with them, and loaded against the registry being loaded before both are committed. Bodies
are entities (`Physics::Body`, `Sim::Transform`, `Sim::InSystem`; `Physics::Velocity` for a dynamic
body), static, kinematic or dynamic, of shapes defined by name from `Physics::ShapeDefinition`s,
which the definition registry loads and a mod can replace. A Jolt world per star system with
bodies; one fixed step makes and removes bodies in entity order, applies what the game wrote, steps
each world and writes the dynamic bodies back ([architecture](architecture.md#physics)). Body
identifiers are the engine's, and a save holds Jolt's state with the contacts, so a loaded world
steps on to the same bytes.
*Filled in* (7b): physics runs in frames, a Jolt world per frame of each system, the system's own
or the local frame of a station or planet on rails, which carries its bodies with it
([architecture](architecture.md#physics), [decided](decisions.md)); star systems are definitions, for
rails and gravity; a `continuous` body is swept; the flight model (`physics/Flight.h`) turns a pilot's
demands into velocity before each step, with assist or without, relative to a reference. The shell
flies a ship beside Highport, from a `flight` input context, behind the scene's pilot rigs.
*Filled in* (7c): a sensor is a body marked `sensor`, kinematic to Jolt and never asleep, and an
entity on rails can be at a place on its body (`OnRails::offset`), so a docking zone rides before a
station's dock. Contacts are the simulation's: what the last step began and ended and what touches
now, from Jolt's contact cache after the step, saved with the world ([decided](decisions.md)).
`CastRay` and `CastShape` find the first body along a ray or a swept convex shape across a system's
frames, at the time a caller gives. Picking is the command `Physics::Pick`, a ray drawn by
presentation and cast at the time it showed, answered by the event `Physics::Picked`
([decided](decisions.md)). The shell docks its ship in a sensor before Highport's dock and
picks with the mouse.
Left for later: overlap queries and casts that return every hit, compound shapes and a shape's
offset in its body, collision filtering beyond static against moving, picking a paused world and
picking under the cursor every frame, and docking as the engine's rather than the shell's, all under
[carried forward](roadmap.md#carried-forward).

### 23. UI · phase 8 · `engine/ui`

RmlUi rendered through the Vulkan backend, with documents and stylesheets loaded through the
VFS so mods can restyle. Data models bound to simulation snapshots, never to simulation state
directly. Fonts through FreeType, shared with ImGui.
*Library*: RmlUi, FreeType.

*Filled in* (8b): `Ui::UiSystem` is RmlUi behind an engine interface, one at a time in a process,
with RmlUi's file interface over the VFS, its system interface over the presentation clock and the
log, and its render interface recording into `Render::Overlay`, which the renderer draws
([architecture](architecture.md#ui)). `Ui::DataModel` binds variables of any reflected type by
address and events by name; the snapshots it reads are the simulation's, carrying the components a
world shows. FreeType is linked by RmlUi and ImGui alike. The shell's market and saved-games screens
are its first documents (`assets/ui`), over the sample game's first slice
([sample game](sample-game.md)).

### 24. Audio · phase 8 · `engine/audio`

Buses (music, effects, UI, voice), 3D sources attached to presentation entities, streamed music,
and simulation events mapped to sounds.
*Library*: miniaudio.

*Filled in* (8c): `Audio::AudioSystem` is miniaudio behind an engine interface, with miniaudio's
file callbacks over the VFS, its log through the engine's under `audio`, and the buses as sound
groups under a master. Sources are generational handles: flat, positioned in the system frame and
placed relative to the listener each frame ([architecture](architecture.md#audio)), or music,
streamed. The output is a device, the null backend, or none, which the tests mix themselves.
`Audio::EventSounds` maps simulation events to sounds from `Audio::EventSound` definitions, by the
event's Reflect name, and `Audio::SoundSources` plays `SoundComponent`s on presentation entities.
The shell is the first user: its music, the station's hum, and the sample game's four event sounds
([sample game](sample-game.md)).

### 25. Persistence · phase 8 · `engine/serialise`, `engine/sim`

Save games of the simulation world and the galaxy, with format version and engine version.
Migrations are a chain of functions from version N to N+1. Settings and keybindings are stored
apart from saves.
*Filled in* (8a): a format's migrations are a chain of steps, one from each version to the next,
which the readers run when they are given them (`Serialise::Migrations`, `BinaryArchive::Read`,
`YamlArchive::Read`, `BinaryReader`, `DefinitionRegistry::Load`). A binary step edits a
`BinaryDocument`, the archive's table of names and its payload: a name renamed in the table is
renamed everywhere it is used, which is how a field, a component, a command or an enumerator is
renamed; a step that needs the old types reads the old version with them and writes the new. A YAML
step edits the parsed tree, which keeps the lines errors name. The world's save migrates from version
1, which had no services' state. `Sim::SaveGame` is the save file, `Jettison.Save` at version 1: a
`SaveInfo` first (the player's name for it, `Core::EngineVersion()`, when it was written by the wall
clock, which the caller sets, the world's step and time, the galaxy by its file, the system shown),
then the world's save and the game's named sections, each a binary archive of its own format,
version and migrations; the save game reads none of them. Writes are durable
(`Core::WriteFileDurably`, through which `Vfs::FileSystem::Write` and `Config::SaveUserLayer` go).
Settings and bindings are one file in the configuration directory, the config's user layer, with
the input system's overrides in it and the joysticks' devices beside them (`Input::JoystickMap`,
under `input.devices.joystick<N>`); a binding reset to its default is taken out of it
(`Config::Unset`), where before it stayed and came back on the next load. The shell is the first
user ([architecture](architecture.md#persistence)).

### 26. AI · phase 9 · `engine/ai`

Utility AI (considerations, response curves, scored actions), behaviour trees (data-defined,
ticked in the simulation step), A* over graphs (jump lanes, in-system waypoints), steering
(seek, arrive, obstacle avoidance, separation). Everything runs in the simulation and is
deterministic.
*Filled in* (9a, 9b, 9f): A* over a graph of indexed nodes, a consistent estimate or none, the same
route every run (`Pathfinding.h`); routes across the galaxy over its jump points, and courses across
a system round its bodies on rings of waypoints (`Routes.h`); the simulation's jump, an entity carried
along a lane and out of the far gate clear of what is there (`Lanes.h`); steering's seek, arrive,
avoidance and separation as velocities flight assist holds, over the spatial index kept from the
simulation (`Steering.h`); utility AI, decisions as data scored by considerations through response
curves (`UtilityAi.h`); and behaviour trees as data of the engine's composites and the game's tasks,
ticked first in the step, resuming their running child, an entity's place in its tree saved
(`BehaviourTree.h`), whose tasks act through commands applied at once (`Sim::SimWorld::Apply`)
([architecture](architecture.md#decisions)). The sample's traders are the first to use them all
([sample game](sample-game.md)).

### Shaders · phase 10

Phases 4 and 5 made programs found by name through the asset VFS and built by `PipelineCache` for
each render state a pass asks for, with shared GLSL in `shaders/include`, and added the shaded
set: the metallic-roughness BRDF under one lighting include, the sky and its bake, the cluster
compute programs, the shadow and prepass depth programs, and the instance buffer every mesh
program reads ([history](history.md#shaders--phase-5)). A material names its program and supplies
its parameters and texture slots, so adding a program is data plus GLSL, not a new code path.
The planet surface program came ahead of phase 10, with the first planet's maps (2026-11-12):
colour, normal, height and roughness by longitude and latitude, and a detail layer on the land.
Phase 10a added the atmosphere program and the planet's clouds, night lights, air and rings over its
surface, from shared includes (`body.glsl`, `atmosphere.glsl`, `rings.glsl`) the environment's
capture runs too. Phase 10b added a program's blend to its descriptor, opaque or transparent, the
glass program, and the particles' programs, one stepping them in compute and one drawing them, all
lit through the same include. Phase 10c added the post chain's programs, full-screen triangles over
shared arithmetic (`post.glsl`), and gave the depth prepass a fragment stage for its normals and
object IDs. Shader variants are compile-time defines
on one source (`jettison_compile_shader_variant`). Programs do not hot-reload yet; materials and
textures do.

### 27. Animation · phases 1 and 10 · `engine/animation`

Phase 1 moved the glTF clip player and the rotation animation out of `Model` and `Scene` into
this module as `Rig`, `Components.h` and `Playback.h`, unchanged in behaviour. Phase 10
generalises it to keyframed TRS tracks on any node and adds clip events (for example, "clamp
closed" triggering a sound).

Phase 10d built both ([animation](architecture.md#animation)). A clip is one thing whatever it
moves (`Clip.h`), sampled alike: a glTF rig's, which now plays on a model with no skin too, moving
where its mesh is drawn; and a clip file's, tracks on named nodes that a player binds to the
scene's own entities, so a rig of procedural parts built in code, or written in a scene file, moves
without a model. A clip file is a resource, its YAML parsed on the main thread. Events are a clip
file's alone, since glTF has no place for them, each crossed once in the order met whichever way
the clip plays, and a time set past a clip's end shows the rig there without them. The game gives
an event its meaning: the shell's are sounds, by data (`Audio::EventSound::clip`). The first rig is
a pad's docking clamps, closing on a docked ship and letting it go, which the dressing builds and
fits to the hull from the snapshots.

### 28. Presentation polish · phase 10 · `engine/render`

The atmosphere scattering shell, planet shading (clouds, night lights, rings), GPU particles
with a sorted transparent pass, glass on that transparent pass. The transparent pass is
forward-lit through the same clusters as the opaque one, which is one of the reasons the
renderer is clustered forward.

Phase 10a built the first part, and the frame written to a file the rest is judged by: air and
rings as maths over a sphere rather than a mesh shell or a baked table, drawn by the planet program
over its surface and by an atmosphere program over the sky, and the bodies in the environment's
capture ([atmospheres](architecture.md#atmospheres)); clouds and city lights from noise, since the
planet pack has no maps of them. Phase 10b built the transparent pass: a transparent program's draws
and the particle emitters sorted back to front and drawn last in the opaque pass's rendering, blended
by what they add and what they let through by colour, with glass as a thin tinted pane and particles
stepped on the GPU in a pool the CPU only tells of births
([transparency](architecture.md#transparency), [particles](architecture.md#particles)).

Phase 10c built the post chain ([post-processing](architecture.md#post-processing)): passes in the
pass list on the resolved HDR target between the opaque pass and the output pass's tonemapping,
bloom first (stars, engines and lit windows are far brighter than the hull), and the colour-grading
LUT of 11c later. Ambient occlusion reads the depth prepass (5e), which writes view-space normals and
runs whenever it is on; the occlusion attenuates the image-based light in `lighting.glsl`, not the
sun or the clusters' lights, so it is computed before the opaque pass shades rather than darkening
the frame after it. The prepass's multisampled depth and normals are read at sample zero, which a
resolve to sample zero would leave, without the copy. Masks come two ways ([decided](decisions.md)). The
depth target has a stencil aspect (`D32_SFLOAT_S8_UINT`, with the device's separate depth and
stencil layouts, and resolved to sample zero for the single-sampled post passes): bits 0 to 2 the
engine's (the cockpit, the outlined, and what post-processing leaves alone), 3 to 7 the game's (a
scanner's hologram, say). Every opaque draw writes its bits, the prepass's or not, so the post
passes test it and the hardware skips what a full-screen pass masks out before shading it. And the
depth prepass writes an object ID buffer beside its normals, which no material program has to know
of: from it a jump-flood pass draws wide, soft target glows that read at any distance, and the
selected's crisp one-pixel line takes its colour. Screen-space reflections stay out
([decided](decisions.md)).

### 29. Scripting · phase 11 · `engine/script`

A Wasm host: module loading from mod mounts, fuel/time limits, a capability-scoped host API
generated from reflection, and calls into scripts from AI considerations and events.
*Library*: WAMR (WebAssembly Micro Runtime) 2.4.5, its fast interpreter ([decided](decisions.md)).
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
*Filled in* (11c): the colour grade, a `.cube`'s table over the display's colours after the
tonemap (`render/ColourGrade.h`); the cinematic camera and sequencer, shots on rails in the frames of
roles the game binds, over the camera rigs (`scene/Sequence.h`). See
[colour grading](architecture.md#colour-grading) and [cinematics](architecture.md#cinematics).
*Filled in* (11d): crash handling, a report of the signal, the frames, named where a symboliser is,
and the log's tail, written on a fatal signal (`platform/Crash.h`, `Core::LogTail`); string tables
through the VFS, a language over its base and a mod's strings over the base's, documents' keys
replaced as RmlUi reads them (`ui/StringTable.h`). See [crashes](architecture.md#crashes) and
[localisation](architecture.md#localisation).

### 31. Docks · phase 9 · `engine/dock`

Stations' pads and the traffic on their decks, in the simulation and deterministic, so `dock` joins
the simulation modules of the [coding standards](coding-standards.md) as it is made. It depends on
`sim`, `physics` and `ai` (A* for the deck's roads, steering for the approach). Entries 22 and 26
left docking to a game and stood a sensor in for it; this is the engine's version, which a different
space game would want unchanged, with the game's rules (who may land where, what a pad costs, what
loitering on one means) left to the game.

**Scale.** A pad takes any hull that fits it ([decided](decisions.md)):

| Pad | Hulls | Largest hull (length × beam × height) | Pad |
|---|---|---|---|
| S | shuttle (the shell's ship, 10 m), fighter | 25 × 20 × 8 m | 40 × 35 m |
| M | transport | 60 × 30 × 15 m | 90 × 50 m |
| L | the largest transport | 140 × 50 × 30 m | 180 × 80 m |
| XL | liner, frigate: display only | 300 × 80 × 60 m | 360 × 120 m |

The smallest trading station has two M pads, the largest twelve L pads. Display pads are beyond
the game's scope: no ship is given one, and the station's layout puts a ship on it as dressing.

**Pads as the simulation's.** A station's pads are known from its layout before any ship comes, so a
server knows what every station can hold. Each pad is an entity of its station, saved, with its
class, its number and its state: free, reserved for a ship until a step, or holding one. A ship asks
by a command, and the step answers every command in the order the queue gives them: the smallest
free pad the hull fits, the lowest numbered of those; if none, a place in the station's queue, served
first come first served as pads free; a reservation lapses if the ship has not landed by its step. A
ship holds one reservation at most, and a pad docks only the ship it was given to. Every command
carries who sent it (a controller: a player's slot, or an AI), so a server can refuse a command for
a ship its sender does not control; nothing in the module knows of one player. Events for each
answer: given, queued, refused (and why), lapsed, landed, left.

**The approach.** Steering (9b) avoids a station by its sphere, which for a station a kilometre or
more across holds every pad. A ship with a pad flies to the point above it, past the station by its hull
(a shape cast along the way), then down the pad's normal at the speed it can stop from, turned to the
pad's heading, and is clamped: held in the station's frame, out of flight. Undocking lifts it to the
point above and lets it go.

**Deck traffic.** A road graph from the layout, between each pad's hatch and the cargo bays, and
buggies on it, routed by A* (9a), moving along its edges at their speed and giving way at its nodes
in a fixed order. They are kinematic, not Jolt bodies: nothing but the deck meets them. A trade's
crates move a buggy's load at a time, and the ship stays clamped until they are aboard, so a cargo's
time to load is the station's data (buggies a pad, crates a trip, speed, the distance from bay to
pad) and time compression shortens it as it does everything else.
*Filled in* (9c, 9d, 9e): `dock`, above `ai` and `physics`: hulls and station layouts as
definitions, checked as they are defined; stations built to them with an entity for each pad; pads
given in command order, the smallest a hull fits and the lowest numbered, queued first come first
served and lapsing, each command carrying its sender; the approach round the station's sphere and
down the pad's normal kept clear by the hull, and the clamp, a docked ship a kinematic body on its
station's rails; the station dressed from its layout in the shell (9d); and the deck's roads made
from the layout, with buggies carrying a docked ship's cargo across it in the simulation and holding
the ship until it has crossed (9e) ([architecture](architecture.md#docks)). On 2026-11-21, for
Manifest's W10, a ship flown in stops first at a point out along its pad's normal and flies straight
down it from there, a stopgap for a ship that circled the point above its pad until the pad lapsed
([carried forward](roadmap.md#then-robustness-tools-and-what-a-mod-can-reach)).
