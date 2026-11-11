# Scope

What the engine builds, what it does not, and why. This replaces the old `TODO.md` list of
typical engine building blocks: every item on that list has a verdict here.

## The game the engine serves

Jettison is the engine for a 3D space trading simulation: trucking logistics, trading and empire
building, with lightweight RPG progression (ship, marketplace and planet improvements, skill and
tech trees, an ask/bid marketplace). The game lives in its **own repository** and consumes the
engine as a CMake package. These decisions, taken in the scoping interview of 2026-11-10, settle
most of the list:

| Question | Decision |
|---|---|
| Ship movement | **Piloted and managed.** The player can fly a ship directly (cockpit and chase cameras, collisions with stations); the rest of the fleet is automated. |
| Multiplayer | **Single-player, split simulation.** A deterministic simulation runs as an in-process "local server", separate from presentation. No networking. |
| Modding | **Data packs plus Wasm scripts.** Content is data layered through a virtual file system; behaviour mods run as sandboxed Wasm modules. |
| Platforms | **Linux only.** |
| Game UI | **RmlUi** for the shipped game UI; Dear ImGui stays for the console, dev tools and the editor. |
| Skeletal animation | **Keep the glTF clip player, no ozz.** Mechanical rigs only: docking clamps, cargo arms, landing gear. |
| Planets | **Seen from orbit only.** No terrain, no water, no landing. |
| World scale | **Many star systems joined by jump lanes.** Each system is real-scale 3D space loaded on entry; the galaxy is a graph. |
| NPC decisions | **Utility AI for economic choices, behaviour trees for per-ship sequences, steering for local avoidance.** |
| Repository | **Separate repositories** for engine and game. |

There are no humanoid characters, and the player never walks around a ship or station.

## Engine and game boundary

The engine provides generic machinery; the game repository owns the rules. When in doubt, a
feature belongs to the engine if a different space game would want it unchanged.

| Engine | Game |
|---|---|
| Fixed-step deterministic simulation, time compression, calendar | The economy model, production chains, prices |
| Data definition registry with mod overrides | Goods, ship, station and module definitions |
| Generic directed graphs (route graph, prerequisite DAG) | Tech trees, skill trees and their effects |
| Utility AI, behaviour trees, route-graph pathfinding, steering | Trader personalities, faction strategy |
| Persistence of simulation state, save versioning | What a save contains beyond engine state |
| RmlUi integration, data binding, input contexts | Market order book screens, fleet screens |
| Wasm host and binding generator | The scripting API surface exposed to mods |

The ask/bid order book itself is game code: it is a data structure with game rules, not engine
infrastructure. The engine's obligation to it is determinism, persistence and UI binding.

## Verdicts

**In**: built, in the phase the [roadmap](roadmap.md) gives it. **Reframed**: the need is real
but the item as written is the wrong solution for this game. **Deferred**: possibly needed, but
only on measured evidence or close to shipping. **Out**: not built.

### Animation

| Item | Verdict | Reason |
|---|---|---|
| ozz-animation, tag-based selection | Out | Built for characters with large clip libraries. This engine has mechanical rigs and a handful of clips per model. |
| Clips | In | The glTF clip player exists (`Model::EvaluateSkin`); it moves into `engine/animation`. |
| Skeletons | In | Kept narrow: glTF skins for mechanical rigs, already supported by the shaders. |
| TRS | In | Transform animation exists (`RotationAnimationComponent`); generalised to keyframed TRS tracks. |
| Poses, FK | In | Not a public system: local pose to model pose is the hierarchy evaluation the sampler already does. |
| IK | Out | No feet, no hands, no aiming limbs. |
| Synchronisation | Out | Clip sync serves locomotion blending, which this engine does not have. |
| Cinematics | Deferred | Docking and jump cutscenes want camera rails and a sequencer. Late, with the cinematic camera. |

### Artificial intelligence

| Item | Verdict | Reason |
|---|---|---|
| Behaviour trees | In | Per-ship tactical sequences: undock, fly to waypoint, request docking, dock. |
| Utility AI | In (added) | Economic choices (which trade, which route, which upgrade) are scoring problems, not trees. |
| Navmesh | Out | Space has no walkable surface. |
| Pathfinding | Reframed | A* over the jump-lane graph and over in-system waypoint graphs (approach corridors, lanes), not over meshes or grids. |
| Collision avoidance | In | Steering near stations and in traffic lanes, so automated ships do not fly through each other or the player. |

### Audio

| Item | Verdict | Reason |
|---|---|---|
| Audio | In | miniaudio: positional 3D sound for cockpit and station proximity, UI sounds, streamed music, mixer buses. |

### C++ weaknesses

| Item | Verdict | Reason |
|---|---|---|
| Assertions | In | First thing built: a debug-break assert, a release check, and an always-on verify. |
| Reflection | In | EnTT's `meta`. Drives serialisation, the editor inspector, console variables and the mod data schema. |
| Strings | In | Hashed string IDs for assets, definitions and tags; UTF-8 `std::string` everywhere else. |
| Containers | Deferred | Standard containers until a profile names one; a flat hash map is the likely first addition. |
| Allocation | Deferred | See Memory. |

### Cameras

| Item | Verdict | Reason |
|---|---|---|
| Third person orbit | In | Orbiting stations, planets and ships. |
| First person | In | Cockpit view when piloting. |
| Chase | In (added) | Behind the piloted ship. |
| Map | In (added) | System map and galaxy map: top-down orbit with zoom across many orders of magnitude. |
| Cinematic | Deferred | With cinematics. |

### Engine runtime

| Item | Verdict | Reason |
|---|---|---|
| Components register for tick | In | A scheduler with named phases (input, fixed-step simulation, presentation, render) that systems register into, with declared ordering. |
| Console | In | ImGui console with commands and console variables, backed by reflection. |
| Core / common | In | Types, handles, hashing, assertions, logging, configuration. |
| Entity component system, queries | In | EnTT, already used. Upgraded from v3.6.0 (2020) to a current release in phase 1. Queries are EnTT views and groups; the engine adds no wrapper. |
| Finite state machines | In | A small utility for ship states and application flow (menu, loading, in system, jumping). |
| Time | In | Real clock, fixed-step simulation clock with pause and time compression, game calendar. |
| Time of day / seasons | Reframed | Becomes the celestial clock: planet rotation and on-rails orbits of planets, moons and stations driven by simulation time. Seasons are out. |
| Platform: Linux | In | Exists (Wayland desktop, X11 headless). |
| Platform: Windows | Out | Dropped from the plan on 2026-11-10: the toolchain cost more than it returned. |
| Platform: CPU | In | x86-64 with AVX2 minimum (Intel Haswell 2013, AMD Excavator 2015, and later), checked at start-up. Jolt's instruction-set flags set it. |
| Profiling | In | Tracy, early, so later decisions are measured. |
| Logging | In | Early. Severity, categories, sinks to the console and a file. |

### Culling and spatial

| Item | Verdict | Reason |
|---|---|---|
| Frustum culling | In | Needed as soon as there is a station with modules and traffic. |
| Planet horizon culling | In (added) | A planet hides everything behind it; a sphere test is cheap. |
| Occlusion culling | Out | Space scenes are sparse. Not worth the work for scenes without walls. |
| Spatial queries | In | A per-system index for simulation queries (nearest station, ships in sensor range), separate from Jolt's broadphase, which serves physics. |
| Raycasting | In | Mouse picking of stations, ships and UI targets, and Jolt ray queries. |

### Content, files and extension

| Item | Verdict | Reason |
|---|---|---|
| Extensions / mods / addons | In | Data packs mounted over the base content, plus Wasm script modules. |
| Scripting: Wasm | In | Sandboxed mod behaviour (missions, events, AI considerations). WAMR as the runtime, to be confirmed when the phase starts. Late phase. |
| Scripting: native C++ | Reframed | Game logic is native C++ in the game repository. There is no native plugin loading for mods: it is unsandboxed and ABI-fragile. |
| File system | In | A virtual file system with ordered mounts (base, DLC, mods, user). Mods override by path. |
| Streaming | Reframed | Asynchronous asset loading when a star system is entered, behind a loading transition. Not open-world streaming. |
| Compression, zipping | Deferred | Pack files for shipping, and whether mods may be zip archives, are decided with packaging. Development reads loose files. zstd is constrained by the copy libktx carries (see the [roadmap](roadmap.md#open-decisions)). |
| Resource manager | In | Typed handles, asynchronous load, reference counting, hot reload in development builds. |
| Serialisation: load, save | In | YAML for authored content (the scene format exists); a versioned binary format for saves. Both reflection-driven. |
| Persistence | In | Save games of simulation state, versioned with migrations. Settings and keybindings saved separately. |
| Scene: management | In | One loaded scene per star system, entered and left through jumps; the galaxy-level simulation persists across them. |
| Scene: layers | In | Content layers within a system (static environment, traffic, effects) for loading and editor visibility. |

### Graphics

| Item | Verdict | Reason |
|---|---|---|
| Render pipeline | In | Exists. Becomes clustered forward ([architecture](architecture.md#rendering-clustered-forward)), on dynamic rendering and a declared pass list. |
| Deferred shading | Out | Clustered forward instead: material variety, lit transparency and MSAA without a G-buffer. |
| PBR | In | glTF metallic-roughness, which is the same model Unreal uses. Image-based lighting from environment cubemaps. |
| Lights: point, projected (spot) | In | Station floodlights, docking bay lights, navigation beacons. Finite ranges, assigned to clusters, so a station can carry hundreds. |
| Light: directional sun | In (added) | The star is the key light of every scene. |
| Shadows | In | Sun shadows on stations and ships; cascaded for close-ups. |
| Atmosphere halo | In | An orbit-view scattering shell around planets. |
| Sky | In | Starfield and nebula skybox, per system. |
| Reflections | In | Through image-based lighting, with the planets added to the environment's capture (phase 10a). Screen-space reflections are out, reconsidered on 2026-11-11: they need most of a G-buffer, and where a ray leaves the screen they fall back to the environment at a seam black space makes plain ([decided](roadmap.md#decided)). |
| Particles | In | GPU billboards: engine exhaust, debris, docking sparks, jump effects. |
| Post processing | In | HDR, tonemapping, and a chain of post passes on the HDR target: bloom first (stars and engines are far brighter than the hull). Phase 10c. |
| Screen-space ambient occlusion | In (added 2026-11-11) | Contact shadow in docking bays, between hull modules and under racked cargo, where the sky's light reaches in full today. From the depth prepass, which writes normals for it, attenuating the image-based light alone. Phase 10c. |
| Stencil | In (added 2026-11-11) | A stencil aspect on the depth target, for the effects that mask by it. Phase 10c. |
| Anti-aliasing | In | MSAA on the HDR target, which forward rendering keeps cheap. Thin struts and antennae against black space alias worst. |
| Colour space | In, early | Linear workflow, sRGB swapchain, HDR render targets. Expensive to retrofit. |
| Colour grading | Deferred | A LUT pass late, when there is art to grade. |
| Triplanar / tri-blend | In | Asteroids and procedural planet surfaces without authored UVs. |
| Glass | Deferred | Cockpit canopy and station windows. The transparent pass it needs comes earlier, with particles. |
| Multiple UV maps | Deferred | glTF `TEXCOORD_1` for detail maps, when an asset needs it. |
| NoRender | In | A trivial flag for collision-only and trigger meshes. |
| Terrain, water | Out | Planets are seen from orbit. |
| Hair, skin, cloth | Out | No characters. |
| Global illumination | Out | A sun and image-based lighting from the system's sky are enough for space. |
| Ray tracing | Out | Not worth the cost for this art style. |
| Materials: shader | In | A material system over PBR parameters and texture sets, authored in data. |
| Materials: effects and animation | In | Material parameters driven over time: emissive pulses, scrolling UVs, blinking beacons. |
| Materials: surface type (sounds, hardness, decals) | Out | Footsteps and bullet decals do not exist here. Impact sound class lives on the physics material. |
| Textures | In | KTX2 with BasisU or BC formats and mip chains for shipping; stb_image in development. |
| Fonts | In | One font asset feeding RmlUi (FreeType) and ImGui. |
| Geometry | In | Procedural meshes (spheres, rings, cylinders), mesh utilities, tangent generation, debug draw. |
| Video playback, cinematics, diegetic video | Out | Diegetic screens, if wanted, are RmlUi documents rendered to a texture. |
| VR | Out | |

### Large worlds

| Item | Verdict | Reason |
|---|---|---|
| Floating origin | In | Architectural and dangerous to retrofit, so early. The simulation holds double-precision positions per star system; rendering is camera-relative in float; reversed-Z depth with an infinite far plane covers a cockpit and a planet in the same frame. |

### Mathematics

GLM (1.0.1) supplies vectors, matrices and quaternions; its configuration lives on the `glm`
target. The engine adds:

| Item | Verdict | Reason |
|---|---|---|
| Vectors, matrices, quaternions, points | In | GLM, plus `glm::dvec3` / `dmat4` for simulation positions. |
| AABB, OBB | In | Culling, picking and spatial queries. |
| Lines, rays, planes, spheres | In | Picking and culling. |
| Bezier and splines | In | Camera rails, route display, docking approach paths. |
| Easing functions | In | UI and camera transitions ([easings.net](https://easings.net/en)). |
| Noise | In | CPU noise (FastNoiseLite) for procedural generation; GPU noise in shaders. |
| Random numbers | In | The engine's own seeded generator (PCG32) and distributions. `std::uniform_*_distribution` is implementation-defined: its streams differ between standard library implementations and can change between their versions, which would break saves and replays. |
| Tangents | In | Normal mapping; MikkTSpace when glTF omits them. |
| Orbital mechanics | In (added) | Keplerian on-rails orbits, transfer timing for route estimates. |

### Memory and threading

| Item | Verdict | Reason |
|---|---|---|
| GPU allocation | In (added) | Vulkan Memory Allocator replaces the raw `vkAllocateMemory` calls. |
| Allocators: bucket, fixed, pool, small | Deferred | Built when Tracy shows allocation in a hot path. A per-frame linear allocator is the likely first one. |
| Atomics | In | As needed, with the threading they serve. |
| Job system | Deferred | One asset I/O worker comes first. A general job system (enkiTS is the candidate) arrives when the economy simulation or culling measurably needs it. Systems are written with declared reads and writes so they can be parallelised later. |

### Network

| Item | Verdict | Reason |
|---|---|---|
| State replication, rollback | Out | Single-player. |
| Local server | Reframed | The in-process simulation/presentation split. Commands flow into the simulation through a queue, which is also the seam for replays and any later networking. |

### Physics (Jolt)

Jolt is built with `JPH_DOUBLE_PRECISION`, which matches the simulation's double-precision
positions.

| Item | Verdict | Reason |
|---|---|---|
| Rigid body, static body | In | The piloted ship, debris, cargo pods; stations. |
| Box, sphere, capsule, cylinder | In | Plus convex hull and mesh shapes for stations. |
| Collision | In | Contact events feed damage and impact sound. |
| Area | In | Sensors: docking zones, station no-fire zones, jump points. |
| Ragdoll | Out | No characters. |
| Water, wind | Out | Space. |
| Particles (physics) | Out | Particles are a render system. |

### Input and UI

| Item | Verdict | Reason |
|---|---|---|
| Input system | In | Action mapping, input contexts (flight, map, UI), rebinding, gamepad and HOTAS through GLFW joysticks. |
| UI | In | RmlUi with data binding for the game; ImGui for tools. |

### Previously out of scope

These stay out: blend shapes, blend spaces, human hair and facial animation. Steam and Epic
integration are out of the **engine**. The game repository may integrate a storefront SDK
(Steam Workshop is a natural home for data mods); the engine keeps its file system mount
model storefront-agnostic so that is possible.

## Added: needs the list did not name

| Item | Phase | Reason |
|---|---|---|
| Unit test framework | 1 | Today there is one hand-rolled test executable. doctest with CTest. |
| Engine packaging | 1 | The game is a separate repository, so the engine exports CMake targets. |
| Configuration and settings | 2 | Engine settings, user settings, console variables. |
| Events and messaging | 3 | EnTT signals and dispatcher between systems; simulation events to presentation. |
| Data definition registry | 3 | Typed, reflected definitions loaded from data packs, overridable by mods. |
| Debug draw | 4 | Lines, boxes and spheres in world space, for every later system. |
| Instancing and LOD | 5 | Cargo containers, asteroid fields, traffic. |
| Editor (Flotsam) | ongoing | Grows with reflection: scene editing, inspector, definition editing. |
| Crash handling | 11 | A backtrace and the log tail, written on a fatal signal. |
| Localisation | 11 | String tables through the VFS. RmlUi handles text shaping. |
