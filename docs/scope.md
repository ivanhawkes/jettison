# Scope

What the engine builds, what it leaves out, and why, area by area: each row a verdict with one line
of reason. How a capability works is in the feature document linked at the head of each area.

## The test

A capability belongs in the engine when its contract is useful without any particular
[game](glossary.md#game); the game's policy (its rules, prices, content and screens) stays with the
game. So a different genre can use the simulation, physics, scenes and AI without inventing a
galaxy. Manifest, the first production [consumer](glossary.md#consumer), drives what is built next
([game integration](game-integration.md)); the boundary itself is in
[architecture](architecture.md#responsibility-boundary). Neutrality is a boundary, not a promise to
build every genre's features now: a capability marked Out is reassessed when a consumer needs it.

## Verdicts

| Verdict | Meaning | Example |
| --- | --- | --- |
| In | Built. Open work on it is a backlog item, linked | Behaviour trees, with reactive nodes still open ([J03](roadmap.md#j03)) |
| Open | Accepted but not built yet: a backlog item, linked | A script reloaded when its module changes ([J04](roadmap.md#j04)) |
| Reframed | The need is real, but the usual solution is the wrong one here; the row says what serves it instead | Pathfinding: A* over graphs, not over navigation meshes |
| Deferred | Possibly needed, but built only on measured evidence or when a consumer asks | A general job system, until a profile shows a system needs it |
| Out | Not built | VR |
| Manifest | A game's, not the engine's: Manifest builds it on the engine's neutral parts | Orbital mechanics, on the engine's [rails](glossary.md#rails) |

## Simulation

How: [simulation](simulation.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Deterministic world, fixed step | In | The same seed and commands give the same state, so saves, replays and networking agree ([coding standards](coding-standards.md#simulation-code)) |
| Scheduler | In | Six phases a frame, systems ordered by name, with declared reads and writes |
| Entity component system, queries | In | EnTT 4; queries are EnTT views and groups, with no engine wrapper |
| Commands and events | In | [Commands](glossary.md#command) carry their sender into the step; [events](glossary.md#event) flow one way, in one ordered queue, to presentation |
| Snapshots for presentation | In | Presentation reads copies of simulation state, never the world itself |
| Definition registry | In | Typed, reflected [definitions](glossary.md#definition) loaded from data, which mods change in layers |
| Finite state machines | Deferred | A game keeps its own; the unused `Ecs::StateMachine` was deleted on 2026-11-26, to be written again when one is shared |
| Time | In | A wall clock for presentation; a simulation clock with pause and compression; a calendar whose shape the game supplies |
| Quiet steps and fast-forward | In | A world with nothing due skips steps it can prove do nothing |
| Time of day, seasons | Reframed | Bodies turn and move on rails driven by simulation time; seasons are a game's |
| Host and session | In | A [session](glossary.md#session) runs a game's start-up phases over its [packages](glossary.md#package), and fails naming the phase and the source |
| Rollback | Deferred | No game asks for it; commands already name their sender and apply in queue order, which it would need |

## Scenes, cameras and spatial queries

How: [scenes](scene.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Scene management | In | One scene loaded at a time, replaced by the game; the simulation persists across scenes |
| Scene layers | In | Environment, traffic and effects layers, shown and hidden as a whole |
| Proxies of simulation entities | In | A scene shows the simulation's entities through [proxies](glossary.md#proxy) the game makes |
| Spatial queries | In | An octree per [frame](glossary.md#frame-coordinates) for nearest, in-range and in-volume queries, apart from Jolt's broadphase |
| Frustum culling | In | Draws and lights outside the view are skipped |
| Horizon culling | In | A large sphere hides what is behind it, and the test is cheap |
| Occlusion culling | Open | Two passes against a depth pyramid, for station interiors and dense fields of instances ([J41](roadmap.md#j41)) |
| Orbit, chase and cockpit cameras | In | Camera rigs that follow their target exactly, in double precision |
| Map camera | In | A logarithmic zoom over a reference plane, from close by to a whole system; the map screen is the game's |
| Cinematic camera | In | A [sequence](animation.md) takes the camera from the rigs and hands it back |

## Coordinates, large worlds and mathematics

How: [coordinate frames and units](coordinates.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Double-precision positions in frames | In | Metres in a [frame](glossary.md#frame-coordinates), in double precision: dangerous to retrofit |
| Camera-relative rendering, reversed-Z | In | Float precision is spent near the camera, and an infinite far plane holds a cockpit and a planet in one frame |
| Vectors, matrices, quaternions | In | GLM, configured on its target, with `dvec3` and `dmat4` for simulation positions |
| Bounds and intersection | In | AABBs, OBBs, spheres, rays and planes, for culling, picking and queries |
| Splines and easing | In | Camera rails, approach paths and UI transitions |
| Noise | In | FastNoiseLite on the CPU, deterministic, for procedural generation |
| Random numbers | In | The engine's seeded PCG32 and its own distributions, because the standard ones differ between libraries |
| Tangents | In | MikkTSpace, which glTF specifies, when a model omits them |
| Orbital mechanics | Manifest | Kepler orbits and transfer timing are Manifest's, on the engine's rails |

## Physics

How: [physics](physics.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Rigid and static bodies | In | Jolt, in double precision and deterministic across builds |
| Box, sphere, capsule, cylinder, convex hull, mesh | In | Shapes are definitions that bodies name, so a save holds no geometry; compound shapes are [J07](roadmap.md#j07) |
| Contacts | In | Began and ended events, in entity order, for the simulation to read |
| Sensors | In | Bodies that report overlaps and push nothing: zones, doors, gates |
| Flight model | In | Thrust as accelerations, with an assist that holds a demanded velocity |
| Rails and moving frames | In | The game's `FrameModel` moves a frame's bodies and gives its gravity |
| Ray queries and picking | In | Jolt ray casts; picking a paused world is [J06](roadmap.md#j06) |
| Character controller | Open | Walking, steps, slopes and moving decks over Jolt's `CharacterVirtual`, in the simulation ([J56](roadmap.md#j56)) |
| Ragdoll | Out | No consumer asks for characters that fall limp |
| Water, wind | Out | No consumer has them; a game can apply its own forces |
| Physical particles | Out | Particles are a render effect |

## Transit

How: [transit](transit.md).

| Item | Verdict | Why |
| --- | --- | --- |
| [Passages](glossary.md#passage) | In | An entity leaves physics and is carried along a way the game gives: a lift, a train, a fast crossing |
| [Portals](glossary.md#portal) | In | An entity leaves its frame for a time and comes out at a named exit: a door, a teleporter |
| [Slots](glossary.md#slot) | In | One holder at a time and a first-come queue: a bay, a lane, a gated portal |
| [Motion profiles](glossary.md#motion-profile) | In | Acceleration-limited travel times, so a game can price a journey |
| Jump lanes, cruise, stargates | Manifest | Manifest's space travel, built on passages, portals and slots |

## Docks

How: [docks](docks.md).

| Item | Verdict | Why |
| --- | --- | --- |
| [Platforms](glossary.md#platform), [berths](glossary.md#berth) and [berth classes](glossary.md#berth-class) | In | Who holds a berth is authoritative state, given by commands in queue order; the layout is data |
| Docking and undocking | In | A vehicle is reserved a berth, flies to it and is held there |
| The way in to a berth | In | A proper approach is open ([J09](roadmap.md#j09)) |
| Berth prices, landing rights | Manifest | A game's rules |

## AI

How: [AI](ai.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Behaviour trees | In | Sequences such as undock, fly, dock, trade, as data, saved where each entity is; more nodes are [J03](roadmap.md#j03) |
| Utility AI | In | Choices that are scoring problems (which trade, which route), as data |
| Pathfinding | Reframed | A* over graphs, and courses round spheres; not over navigation meshes or grids |
| Steering | In | Seek, arrive, avoidance and separation, so automated vehicles keep clear; more is [J08](roadmap.md#j08) |
| [Helm](glossary.md#helm) | In | A player takes an entity's controls and its behaviour stands down |
| Navigation meshes | Out | No consumer walks on meshes |
| Trader policies, faction strategy | Manifest | A game's rules |

## Persistence

How: [persistence](persistence.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Reflection | In | EnTT's meta, with attributes that drive saves, definitions, the console and scripts |
| Authored content | In | YAML archives, written by reflection |
| [Saves](glossary.md#save) | In | A versioned binary format with its own [migrations](glossary.md#migration), written durably |
| Settings and key bindings | In | Layered configuration in the user's settings file, apart from saves |
| What a save holds beyond the world | Manifest | The game's state, in a format of the game's, which the save carries unread |

## Files, packs and mods

How: [files, packs and mods](files-and-mods.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Virtual file system | In | Ordered mounts (the engine's assets, the game's content, mods, the user's directory), each a directory or a pack |
| [Packs](glossary.md#pack) | In | One file per package for shipping, each file its own zstd frame, so one is read alone |
| [Mods](glossary.md#mod) | In | Mounts over the base that replace files by path and [patch](glossary.md#patch) definitions field by field; more is [J05](roadmap.md#j05) |
| Resource manager | In | Typed handles, loads on the I/O worker, reference counts, hot reload in development builds |
| Streaming | Reframed | Assets load when a scene is entered, behind a transition; not open-world streaming ([J13](roadmap.md#j13)) |
| Geometry streaming | Open | A model's meshlet pages read as the view needs them, within a scene ([J43](roadmap.md#j43)) |
| Zip archives | Reframed | Packs instead: a mod is a directory or a pack |
| Signed packs | Open | A pack signed as a whole ([J19](roadmap.md#j19)) |
| Storefront integration | Out | A game may integrate a storefront's SDK; the mounts stay storefront-agnostic so it can |

## Scripts

How: [scripts](scripts.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Wasm scripts | In | Sandboxed mod behaviour on WAMR's interpreter, in C or Rust, against bindings generated from reflection; more is [J04](roadmap.md#j04) |
| Native C++ plugins | Reframed | Game logic is native C++ in the game's repository; mods get none, being unsandboxed and ABI-fragile |
| What a script may do | Manifest | The game grants each script its commands and events |

## Networking

How: [networking](networking.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Transport, connections, sessions | In | ENet over UDP, so the same stack serves an idle game and real-time combat |
| Authentication handshake | In | Bearer tokens on libsodium; the server keeps digests only |
| Snapshot replication | In | The server encodes each session's view through a visibility hook the game supplies |
| Encryption | In | Every message sealed to keys made with the server's pinned key, on libsodium; TLS does not run over UDP |
| A script's sender under networked play | In | The game names a handler's sender per event, and every controller in its command is stamped ([scripts](scripts.md#who-a-scripts-commands-are-from)) |
| Local server | Reframed | The in-process split of simulation and presentation, with commands through a queue, serves |
| Dedicated server, protocol, accounts, authorisation | Manifest | A game's |

## Rendering

How: [rendering](rendering.md), and
[transparency, particles and post-processing](rendering-effects.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Clustered forward on Vulkan | In | Material variety, lit transparency and MSAA without a G-buffer, on dynamic rendering and a declared pass list |
| Deferred shading | Out | Clustered forward serves the same lights without a G-buffer |
| Physically based materials | In | glTF metallic-roughness, lit by image-based lighting from the sky's capture |
| Materials as data | In | Parameters and texture sets in YAML, some animated over time (pulses, blinks) |
| Point and spot lights | In | Finite ranges assigned to clusters, so a scene can carry hundreds |
| Directional sun and its shadows | In | Cascaded shadow maps, stable under a moving camera |
| Ray-traced shadows and ambient occlusion | Open | The sun's and opted-in local lights' shadows and ambient occlusion by ray queries where the device has them; cascades and screen-space occlusion stay the fallback ([J44](roadmap.md#j44) to [J46](roadmap.md#j46)) |
| Atmospheres and rings | In | Scattering shells and rings seen from orbit |
| Sky | In | A procedural sky of stars, a band and nebula, per scene |
| Reflections | In | Image-based lighting; ray-traced sharp reflections are open ([J47](roadmap.md#j47)); screen-space reflections are out, needing most of a G-buffer |
| Anti-aliasing | In | MSAA on the HDR target today; temporal AA replaces it in [J49](roadmap.md#j49) ([decisions](decisions.md#rendering)) |
| Colour space | In | Linear workflow, HDR targets and an sRGB swapchain; expensive to retrofit |
| Triplanar mapping | In | Surfaces without authored texture coordinates |
| Textures | In | KTX 2 with Basis Universal or BC7 for shipping; stb_image for development; cube maps and arrays are [J27](roadmap.md#j27) |
| Instancing and levels of detail | In | Draws that share a model, level and material become one instanced draw |
| Meshlets and GPU-driven drawing | Open | Models cooked into a meshlet hierarchy, culled on the GPU and drawn by mesh shaders, the indexed path the fallback ([J39](roadmap.md#j39), [J40](roadmap.md#j40)) |
| Visibility buffer, software raster | Open | Temporal AA makes one sample a pixel enough ([J50](roadmap.md#j50)) |
| Virtual shadow maps | Out | Ray-traced sun shadows serve, with the cascades as the fallback |
| Procedural geometry | In | Spheres, cubes and cube-spheres at levels of detail |
| Debug draw | In | Lines, boxes and spheres in world space, for any system |
| Fonts | In | TrueType through FreeType, for the game UI and the dev tools |
| Collision-only meshes | Open | With a glTF model's collision mesh ([J12](roadmap.md#j12)) |
| Multiple texture coordinate sets | Deferred | When an asset needs a detail map on a second set |
| Surface types (sounds, decals) | Out | No consumer needs footsteps or decals |
| Terrain, water | Out | No consumer has a walkable surface |
| Skin and eyes | Open | Pre-integrated skin and an eye material, for portraits ([J58](roadmap.md#j58)) |
| Hair | Open | Alpha-tested cards, the engine's first alpha-tested materials ([J59](roadmap.md#j59)); strands out |
| Cloth | Out | No consumer asks for it |
| Global illumination | Out | Image-based lighting and a sun serve the consumers' scenes |
| Ray tracing | Open | Ray queries on plain KHR acceleration structures, which every vendor and lavapipe run; RTX Mega Geometry on NVIDIA where present ([J42](roadmap.md#j42), [J48](roadmap.md#j48), [the plan](dense-geometry-and-ray-tracing.md)) |
| Video playback | Out | A screen in the world, if wanted, is a UI document drawn to a texture |
| VR | Out | No consumer asks for it |

### Effects

| Item | Verdict | Why |
| --- | --- | --- |
| Transparency | In | Glass as thin tinted panes, sorted back to front; refraction waits for the frame behind ([J24](roadmap.md#j24)) |
| Particles | In | Simulated and drawn on the GPU, with the transparent draws in the opaque pass |
| Post-processing | In | HDR, tonemapping and a chain of passes, bloom first; more is [J25](roadmap.md#j25) |
| Ambient occlusion | In | From the depth prepass, darkening the image-based light only |
| Stencil masks, object IDs, outlines | In | Crisp outlines, and post-processing kept off masked areas |
| Colour grading | In | A 3D lookup table from a `.cube`, after the tonemap, so a graded screenshot is the graded frame ([J22](roadmap.md#j22)) |

## UI, input and localisation

How: [UI, input and localisation](ui.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Game UI | In | RmlUi with data binding to presentation's snapshots, read through the VFS so a mod restyles it; its drawing is [J23](roadmap.md#j23) |
| Dev tools and console | In | ImGui, with commands and console variables bound through reflection |
| Input | In | Actions, stacked contexts, rebinding saved in settings, gamepads and joysticks named by device |
| Localisation | In | String tables per language through the VFS, with formatting and plurals; more is [J15](roadmap.md#j15) |
| Right-to-left and shaped scripts | Open | With the rest of localisation's open work ([J15](roadmap.md#j15)) |
| Screens, flows, the mod menu | Manifest | A game's |

## Audio

How: [audio](audio.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Positional sound, buses, music decoded as it plays | In | miniaudio behind an engine interface, listener-relative in double precision; more is [J21](roadmap.md#j21) |
| Sounds for simulation events | In | A game maps events to sounds as data |

## Animation and cinematics

How: [animation and cinematics](animation.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Clips | In | glTF clips and clip files that keyframe scene entities, with events a game gives meaning to |
| Skeletons | In | glTF skins for mechanical rigs |
| Transform animation | In | Keyframed position, rotation and scale tracks on any scene entity |
| Poses, forward kinematics | In | The hierarchy evaluation the sampler already does; not a public system |
| Cinematics | In | Shots on camera rails, cut or blended, with a fade, letterbox and events, as data a mod replaces ([J22](roadmap.md#j22)) |
| ozz-animation, tag-based selection | Out | The engine's own sampler, extended to blends and layers, serves its characters; ozz's IK solvers are a reference for [J60](roadmap.md#j60) |
| Blend shapes | Open | glTF morph targets, driven by clips, by the game's code by name and by lip sync ([J52](roadmap.md#j52), [characters](characters.md)) |
| Blend spaces, cross-fades, layers | Open | 1D and 2D blend spaces kept in step on sync markers, cross-fades and layers by joint mask ([J53](roadmap.md#j53) to [J55](roadmap.md#j55)) |
| Root motion | Out | Animation never moves the simulation; locomotion plays in place, its rate matched to the controller's speed |
| Animation state machines and graphs | Out | The game's code chooses what each layer plays |
| Lip sync | Open | Made by the cook from dialogue audio with Rhubarb Lip Sync ([J57](roadmap.md#j57)) |
| Two-bone IK, look-at | Open | Later, after blend spaces ([J60](roadmap.md#j60)) |
| Crowds of animated characters | Out | Each animated instance has its own vertices: dozens, not hundreds |

More animation work is [J20](roadmap.md#j20).

## Runtime: core and platform

How: [runtime: core and platform](runtime.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Assertions | In | `JETTISON_ASSERT` (debug builds), `JETTISON_VERIFY` (every build), `JETTISON_NOT_IMPLEMENTED` (throws) |
| Logging | In | Severity, categories, and sinks to stderr, a file and the console |
| Core types | In | Generational handles, hashed string IDs, durable file writes, file locks, the engine's version |
| Configuration | In | Layers of defaults, the user's file and the command line, by dotted key |
| Paths | In | Saves, settings, logs and caches where the XDG specification puts them |
| Crash handling | In | A report of the stack and the log's tail on a fatal signal; more is [J11](roadmap.md#j11) |
| Linux | In | Wayland on the desktop, X11 for headless runs |
| Windows | Deferred | Built near the end of this development phase (the user's, 2026-11-25); until then code is written for Linux with no design compromise made for Windows, and dependencies stay portable ([decisions](decisions.md#build-and-platform)) |
| CPU baseline | In | x86-64 with AVX2, checked before any static initialiser runs ([decisions](decisions.md#build-and-platform)) |
| Profiling | In | Tracy, compiled in on demand, with GPU zones per pass |
| I/O worker | In | One thread reads and decodes assets, so loading does not stall the frame |
| General job system | Deferred | Until a profile shows a system needs it; systems already declare their reads and writes for it |
| Containers | Deferred | Standard containers until a profile names one |
| Allocators | Deferred | Until a profile shows allocation in a hot path |
| GPU memory | In | Vulkan Memory Allocator |

## The app shell and tools

How: [the app shell and tools](app-shell.md).

| Item | Verdict | Why |
| --- | --- | --- |
| The [shell](glossary.md#shell) | In | The `app` library, which the ground sample, the editor and the render test link: a game's hooks composed over the engine, with console commands and arguments |
| The packer | In | `jettison_pack` writes a directory as a pack |
| The asset cook | Open | `jettison_cook` cooks models and textures from a source tree into a game's asset folder, once or watching ([J37](roadmap.md#j37), [J38](roadmap.md#j38)) |
| Editor (Flotsam) | In, minimal | The shell with no game composed; an inspector and scene editing are not built. How much a game needs Flotsam is to be looked at later (the user, 2026-11-25); until then an editor is assumed needed, though each game may better ship its own over common functionality the engine provides |
| Logging in the shell | In | The run's log in the state directory ([runtime](runtime.md#logging)) |

## Testing and packaging

How: [build and gates](build-and-gates.md).

| Item | Verdict | Why |
| --- | --- | --- |
| Unit tests | In | doctest with CTest, one suite per module |
| Smoke test and reference frames | In | The render test on a GPU or lavapipe, judged on validation output and exit status; reference frames on lavapipe |
| Engine package | In | A game builds the engine from source with `FetchContent` and links its CMake targets |
| Prebuilt SDK | Open | An installed engine for `find_package` ([J18](roadmap.md#j18)) |
