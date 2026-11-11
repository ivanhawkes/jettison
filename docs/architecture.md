# Architecture

How the engine is laid out, how its parts depend on each other, and the handful of
architectural rules that are expensive to change later. The [scope](scope.md) says what is
built; the [roadmap](roadmap.md) says in what order.

## Repository layout

The layout as built. Phase 1 of the roadmap moved the existing code into it (`core`,
`animation`, `render`, `app`, the executables under `tools/` and `samples/`) and stubbed every
other in-scope module: each has its directory, target, public headers and a passing test, and
every function that needs an algorithm throws `JETTISON_NOT_IMPLEMENTED()` until its phase.

```
jettison/
├── CMakeLists.txt              project, warnings target, shader helper, package export
├── CMakePresets.json
├── cmake/                      jettison_add_module(), JettisonConfig.cmake.in
├── assets/                     sample and test content (Git LFS), staged beside executables
├── docs/                       this documentation
├── scripts/                    gates: ci.sh, format-check.sh, smoke-test.sh, tidy.sh
└── source/
    ├── external/               every third-party dependency, pinned, FetchContent
    ├── engine/
    │   ├── core/               assert, log, config, string IDs, handles, hashing
    │   ├── math/               GLM additions: bounds, rays, splines, easing, RNG, noise, orbits
    │   ├── platform/           OS layer: paths, crash handling, dynamic queries
    │   ├── profile/            Tracy wrapper (compiled out when disabled)
    │   ├── time/               clocks, fixed-step accumulator, time compression, calendar
    │   ├── jobs/               I/O worker now; job system later
    │   ├── reflect/            EnTT meta registration helpers
    │   ├── serialise/          YAML and binary archives over reflection
    │   ├── vfs/                virtual file system, mounts, packs
    │   ├── resource/           handles, async load, reference counts, hot reload
    │   ├── ecs/                scheduler, tick phases, events, FSM utility
    │   ├── animation/          glTF rigs and clip player, TRS tracks, mechanical skins
    │   ├── sim/                simulation world, command queue, snapshots, definition registry
    │   ├── celestial/          celestial clock, on-rails orbits, system and galaxy frames
    │   ├── scene/              presentation scene, its file, update systems, camera rigs; layers
    │   ├── spatial/            per-system spatial index, queries
    │   ├── input/              actions, contexts, bindings, devices
    │   ├── render/             renderer interface, frame, cameras, materials, debug draw
    │   │   └── src/vulkan/     the Vulkan backend, private, behind IRenderer.h
    │   ├── physics/            Jolt integration
    │   ├── audio/              miniaudio integration
    │   ├── ui/                 RmlUi integration and data binding
    │   │   └── imgui/          ImGui backend, console, dev tools
    │   ├── ai/                 utility AI, behaviour trees, graph pathfinding, steering
    │   ├── script/             Wasm host
    │   └── app/                Application shell, argument parsing, smoke sequence
    ├── tools/
    │   └── flotsam/            editor
    ├── samples/
    │   └── jetsam/             runtime viewer: the engine with no game
    └── test/                   the render smoke harness (test executable)
```

Each engine module has the same shape:

```
engine/<module>/
├── CMakeLists.txt              jettison_add_module(<module> DEPENDS ...)
├── include/jettison/<module>/  public headers: #include <jettison/<module>/Thing.h>
├── src/                        implementation and private headers
└── tests/                      doctest unit tests, registered with CTest
```

Each module is a static library `jettison_<module>`, aliased `Jettison::<Module>`, in namespace
`Jettison::<Module>`, made by `jettison_add_module` (`cmake/JettisonModule.cmake`). The helper
refuses a `DEPENDS` edge on a module that has not been added yet, so the order of
`add_subdirectory` calls in `source/engine/CMakeLists.txt` is the dependency graph's topological
order, and an upward or sideways edge fails the configure rather than linking quietly.

### Parked code

Nothing is parked today. Phase 1 parked the scene graph in `render`, where it loaded models through
the device and handed the renderer `Model` pointers while the renderer read its camera and lights
back out of its registry, a cycle. Phase 6a took it out: the scene is `engine/scene`, names its
models by virtual path, and loads each model's rig through the resource manager; the renderer
resolves the same paths to device models through its own cache, behind `IRenderer.h`. The rig is
read from the glTF file by `animation`, which moved to the World tier, below `scene`, so the clip
player the scene drives is no longer a sibling edge of `render`'s.

The executables keep the names they already have: **Flotsam** is the editor, **Jetsam** is the
runtime, now a sample viewer because the game has its own repository, and **render_test** is
the render smoke harness that the gate runs. It was `test` until CTest reserved that name (see
[build and gates](build-and-gates.md#unit-tests)).

### Consumption by the game repository

The game uses `FetchContent` with a pinned commit and links `Jettison::Engine`, an interface
target over every module, or the `Jettison::<Module>` targets it needs. The install export
(`JettisonTargets.cmake`, `JettisonConfig.cmake`, behind `JETTISON_INSTALL`) exists for a later
`find_package(Jettison)` consumer; [build and gates](build-and-gates.md#consuming-the-engine) says
what it still needs. Public headers are the
`include/jettison/` trees only; anything under `src/` is private. Third-party headers that
appear in public engine headers (GLM, EnTT) are public dependencies; the rest (Vulkan, Jolt,
RmlUi, miniaudio) stay private where the interface allows it, so the game does not compile
against the backends.

## Module dependencies

Dependencies point downward only. A module depends on modules in lower tiers, never on its own
tier's siblings unless the edge is listed, and never upward. A cycle is a build failure, not a
style question.

```mermaid
flowchart TB
    subgraph T0[Foundation]
        core --> math
    end
    subgraph T1[Services]
        platform; profile; time; jobs; reflect; vfs
    end
    subgraph T2[Data]
        serialise; resource; ecs
    end
    subgraph T3[World]
        scene --> animation
        scene --> celestial
        sim; spatial; input
    end
    subgraph T4[Presentation and simulation services]
        render; physics; audio; ui; ai; script
    end
    subgraph T5[Shell]
        app
    end
    T1 --> T0
    T2 --> T1
    T3 --> T2
    T4 --> T3
    T5 --> T4
```

The World tier's listed sibling edges are `scene`'s: on `animation`, whose clip players its update
drives, and on `celestial`, whose star systems its system scenes are built from. `render` depends on
`scene` from the tier above, and reads it; the scene names no device.

The rule that matters most: **`sim`, `celestial`, `spatial`, `physics` and `ai` never include
`render`, `ui`, `audio` or `input`**. Simulation code that can see the renderer eventually reads
from it, and then it is no longer deterministic or testable without a GPU.

## Simulation and presentation

The game is single-player, but the simulation runs as if it were a local server.

- **The simulation world** owns authoritative state in its own `entt::registry`: ships, cargo,
  stations, markets, orders, positions in double precision. It advances in fixed steps on
  simulation time, which is real time multiplied by the time-compression factor. It is
  deterministic: the same save and the same command stream produce the same state in every
  build of the engine, Debug and Release alike. It is everything a save contains.
- **The presentation world** owns what is drawn and heard, in its own registry: render proxies,
  interpolated float transforms, cameras, particles, UI state. It runs once per frame at the
  display rate and may be rebuilt from the simulation at any time.
- **Commands** flow from presentation to simulation through a queue: "buy 40 units at
  1,200", "plot route", "set throttle". The simulation applies them at the start of a step.
  The queue is the seam for replays, automated tests and any later networking.
- **Snapshots and events** flow back: transforms for interpolation, and events such as
  "docked", "trade filled" or "contact" that presentation turns into sound, effects and UI.

The piloted ship crosses this boundary every frame: input becomes flight commands, Jolt
advances in simulation steps, and presentation interpolates between the last two physics
states. When time compression is above 1×, piloting is suspended, as it is in every game of the
genre. Compression is refused near stations and other ships for the same reason.

## Coordinate frames and units

SI units: metres, seconds, kilograms, radians. Right-handed, Y-up, as the scene format already
is (`CameraComponent` documents it).

| Frame | Precision | Origin | Used by |
|---|---|---|---|
| Galaxy | graph | none: systems are nodes, jump lanes are edges | route planning, the galaxy map |
| System | `double` | the system's barycentre or primary star | simulation positions, orbits, Jolt (`JPH_DOUBLE_PRECISION`) |
| Local | `double` | a station or planet | docking approaches, station-relative traffic, the sun's shadow grid (`CameraView::frameOrigin`) |
| Render | `float` | the camera | everything submitted to the GPU |

Rendering is **camera-relative**: each frame subtracts the camera's double-precision position
from every object's before converting to float, so precision is always best where the player is
looking. This is the floating origin. It is never visible to simulation code. Depth is
**reversed-Z with an infinite far plane**, so a cockpit at half a metre and a planet at
forty thousand kilometres share a depth buffer. The existing `GLM_FORCE_DEPTH_ZERO_TO_ONE`
setting is the prerequisite for that.

## Rendering: clustered forward

The renderer is a **clustered forward** renderer (decided 2026-11-10, see the
[roadmap](roadmap.md#decided)). Lights are assigned to a 3D grid of view-space clusters by a
compute pass. Each surface is then shaded once, in its own material program, from the lights
listed for the cluster it falls in. There is no G-buffer. It was chosen over deferred shading
for what this game draws:

- **Material variety.** Hulls, planets, atmosphere shells, glass, emissive panels and engine
  plumes are each their own program. Forward shading puts no common G-buffer layout between them.
- **Transparency.** Canopies, station windows, particles and exhaust are lit by the same lights
  through the same code. A deferred renderer would still need a forward path for them.
- **Anti-aliasing.** MSAA works directly on the HDR target. Thin struts, antennae and distant
  ships against black space are where aliasing shows most.
- **Bandwidth.** No fat render targets, which matters on lavapipe, where the gate runs, and on
  laptops.
- **Many small lights.** Floodlights, docking bay lights, beacons and navigation lights add up to
  hundreds per station, but few of them reach any one pixel. Clustering keeps the cost per
  fragment at the lights that actually reach it.

### The frame

Each step is a node in the declared pass list (phase 4), and compute nodes sit beside graphics
nodes in it:

1. **Collect and cull** (CPU). Every mesh's bounds are taken into render space in double
   precision, before anything becomes a float (`render/Culling.h`). Its level of detail is the
   coarsest whose geometric error, projected, covers under a pixel. The view keeps what is inside
   the frustum and not wholly behind a planet's horizon (a celestial body drawn as a sphere, shrunk
   by its level's error so the sphere is inside the mesh); each cascade keeps the casters whose
   sphere reaches the prism its map shadows. What is kept is batched (`render/DrawBatches.h`): draws
   sharing a model, a level and a material are one instanced draw, the batches front to back, and
   every instance's transform goes into this frame's instance buffer. Active lights are converted
   to camera-relative float and written to a storage buffer for this frame in flight (5c); one
   whose reach ends short of the cluster range's far end, is outside the frustum, or is wholly
   behind a planet is left out (5e), since every surface it could light is too. A skinned draw's
   pose can leave its model's bounds, so it is never culled.
2. **Shadows** (graphics, `sun-shadows`, 5d). The sun's four cascades, each a depth-only rendering
   of its casters into one layer of the shadow map; see [sun shadows](#sun-shadows). A scene with no
   sun clears them and draws nothing, so the plan is the same either way. Spot and point shadows,
   when they arrive, render into an atlas, and each light's record carries its atlas slot.
3. **Depth prepass** (graphics, `depth-prepass`, 5e). The view's batches, depth only, laid down for
   the opaque pass to test against with greater-or-equal, so each pixel is shaded once. Measured in
   5e it cost more than it saved, on hardware and on lavapipe, because the batches are front to
   back already; it is kept, and off (`Renderer::SetDepthPrepass`), as the place screen-space
   ambient occlusion will take its depth and normals from (phase 10c). Off, the node records
   nothing and the opaque pass clears the depth, under the same plan.
4. **Cluster bounds** (compute, `cluster_bounds`, 5c). The view-space bounds of every cluster.
   They are rebuilt only in a frame whose projection, viewport or cluster range differs from the
   one they were built for; the pass's barriers are recorded either way, and the buffer keeps
   its contents between frames.
5. **Light assignment** (compute, `cluster_lights`, 5c). Each light's sphere, and a spot's cone,
   is tested against each cluster's bounds, one invocation per cluster. The output is an offset
   and a count per cluster into one global light-index list, whose first word is the allocator:
   a transfer pass zeroes it at the start of the frame, and each cluster reserves its share with
   one atomic add. The list holds 32 entries per cluster on average, a budget for the frame
   rather than a cap on any one cluster.
6. **Opaque** (graphics, into the MSAA HDR target). A fragment finds its cluster from its screen
   position and view distance, loops over that cluster's lights, then adds the sun, shadowed through
   its cascade, and image-based lighting.
7. **Sky.** Drawn last in the opaque pass's rendering rather than as a node of its own (5b): one
   full-screen triangle at depth 0, the far end of the reversed-Z range, tested against the depth
   buffer so it shades only what no surface covered. A node of its own would have to store the
   multisampled target and load it again, which the opaque pass keeps on chip.
8. **Transparent**, sorted back to front, using the same cluster lookup (phase 10).
9. **Resolve, bloom, tonemap** into the sRGB swapchain, then the UI. The tonemapper is Khronos PBR
   Neutral (5b), applied after exposure in the output pass: a base colour under white light of
   intensity one shows as authored, and highlights compress towards white without changing hue.
10. **Cluster readback** (transfer, 5c). The grid and the index list copied out for the cluster
    check, which compares them with a CPU reference once the frame's fence is waited on. It is in
    every frame's plan and records its copies only when the check is on, so the plan the gate's
    synchronization validation runs is the plan every run has.

### The environment

A star system's sky is drawn behind the scene and is also what the scene is lit by
(`render/src/vulkan/Environment.h`). Its starfield, galactic band and nebula are procedural, from the
scene's `SkyComponent` and a seed, and `sky.glsl` is the one function that draws them, both in the
frame and into a cube map. When a scene loads or its sky changes, that capture is baked into the
image-based lighting: GGX-prefiltered levels of the specular cube, one roughness each, and a
cosine-weighted irradiance cube, both by filtered importance sampling from the capture's mip
chain. The split sum's lookup table depends only on the BRDF and is made once. The bake is a run of
fragment passes on the graphics queue, recorded outside the frame's pass list as an upload is, and
it waits for the queue; the cubes it replaces are freed when it returns, and the frame sets are
rewritten to name the new ones. The sun is not in the capture: it lights the scene as the
directional light, and a disc in the environment would light it twice. Nor are the planets, so a
planet's reflected light is missing until a later phase adds it.

### The grid and the infinite far plane

The grid is screen tiles times depth slices, exponential in view distance: 32 × 18 × 48, tuned in
5e against a station of 265 lights (`scripts/make-light-budget-scene.py`), where it shaded the
opaque pass a fifth faster than the 16 × 9 × 24 it started at, on hardware and on lavapipe alike
([decided](roadmap.md#decided)). Depth is reversed-Z with an infinite far plane,
so the slices are computed from view-space distance, never from the depth buffer. They span the
near plane to a finite **cluster range**. Past it, no local light is assigned, and a surface is lit
by the sun, image-based lighting and its own emission. A station's lights seen from tens of
kilometres away are emissive sprites, not lights. The cluster range is a setting
(`Renderer::SetClusterRange`), chosen with the light budget: 2 km, which 5e kept, its lights all
being within 200 m of the camera.

The grid's arithmetic is one file, `shaders/include/clusters.glsl`: the slices, the bounds, the
light-against-cluster tests and the windowed falloff. Like `brdf.glsl` it is written in the
subset of GLSL that GLM also compiles, and `render/Clusters.h` is that source compiled as C++.
The CPU reference built on it checks the GPU's assignment: in the headless render test every
frame's grid is read back and compared with it, cluster by cluster, as sets with a rounding
tolerance of a hundred-thousandth of the distances involved. The lights-per-cluster heat map (F3)
shows the same grid on screen, in place of the shaded surfaces.

### Sun shadows

The sun is shadowed by four cascaded shadow maps (5d), one layer each of a 2048 × 2048
reversed-depth array image shared by the frames in flight. The view is cut into four slices by
the practical split scheme, blending even and logarithmic splits (0.8 towards logarithmic), from
the near plane to the **shadow distance** (`Renderer::SetShadowDistance`, 400 m to start with,
tuned in 5e). Past it the sun casts no shadow. `render/Shadows.h` fits the cascades on the CPU each
frame, and `shaders/include/shadows.glsl`, which it compiles as C++ as `Clusters.h` does
`clusters.glsl`, is what the lighting include selects and looks up through.

- **Stable by construction.** Each cascade's map covers the bounding sphere of its slice of the
  view, plus a border of four texels, so its texel size depends only on the projection and turning
  the camera never changes it. The map's centre is snapped to whole texels in the sun's frame,
  whose axes come from the system's, never the camera's. The snap is measured from the camera's
  **frame origin** (`CameraView::frameOrigin`): the station or body an orbit camera circles, or the
  system's origin. A station moves at kilometres a second through the system, so a grid fixed to
  the system would slide under it by a fraction of a texel every frame and its shadows' edges would
  crawl; fixed to the station, a still caster lands on the same texels whatever the camera does.
  The snap is taken in double precision, and only the matrix from render space to the map becomes
  a float.
- **Casters between the sun and a map still shadow it.** The shadow pass clamps depth rather than
  clipping it (`depthClamp`, a required device feature), so whatever lies sunwards of a cascade's
  sphere, up to and including a planet, is drawn at the map's sun side.
- **Acne** is kept off by a slope-scaled depth bias in the shadow pass and by moving each receiver
  along its normal before the lookup, by one and a half texels of its cascade at a surface edge-on
  to the sun.
- **Lookup.** A fragment selects its cascade by view depth, takes nine bilinear comparison taps a
  texel apart (a 4 × 4 texel neighbourhood), and across the last tenth of each cascade blends into
  the next, or out of shadow past the last. Each cascade is fitted from the start of the previous
  one's band, so the blend's second lookup lands inside its map too.
- **Debug draw shows the cascades** (F4): each surface in its cascade's colour, lit by the sun
  alone and shadowed.

### The budget

The frame's GPU time is measured, not guessed: `GpuProfiler` writes a timestamp either side of
every declared pass, in a query pool per frame in flight, and reads them back once the frame's fence
is waited on; with Tracy compiled in, the same spans are GPU zones named after the passes. The dev
tools' Renderer panel shows the last and mean time of each pass and the frame's counts, and
`render_test --benchmark <frames>` prints them after a warm-up ([build and
gates](build-and-gates.md#benchmark)). The budgets, for what phase 5 draws:

| Device | Frame | Budget | `orbit.yaml` | With 265 lights |
|---|---|---|---|---|
| RTX 5060 Ti | 1248 × 1358 (the compositor's tile) | 4 ms, a quarter of a 60 Hz frame | 0.30 ms | 0.47 ms |
| lavapipe, 24 threads | 640 × 480 | 50 ms | 33 ms | 46 ms |

A quarter on hardware leaves the rest of the frame for phase 10's atmosphere, transparency and
post-processing and the UI. Lavapipe's budget is the gate's: it keeps a smoke run in the tens of
seconds. Neither is enforced by the gate, whose runners' times vary too much to fail on; a change
that matters to them is measured with the benchmark and recorded. Two measurements in 5e shaped the
frame. Lavapipe decodes BC7 in software at every texel fetch, and the orbit scene's opaque pass took
469 ms in BC7 and 23 ms in RGBA8, so a CPU device is given RGBA8. And the planet, drawn into two
cascades whole, was most of the shadow pass until casters were culled exactly against each map's
prism: 15.5 ms to 4.2 ms on lavapipe, most of what is left being the four 2048² clears.

### Rules that follow

- **Every local light has a finite range.** Falloff is inverse-square, windowed to reach zero at
  that range ((1 - (d/r)^4)^2 / d^2), so culling a light against a cluster is exact.
  `LightComponent` is a point or a spot with a range and, for a spot, inner and outer cone
  angles as glTF's `KHR_lights_punctual` has them (5c); a scene file that leaves out a light's
  range is refused.
- **The sun is not a cluster light.** There is one directional light per star system, held in the
  frame constants and shadowed with cascades.
- **Light data lives in storage buffers**, one set per frame in flight, never in the frame UBO.
  The UBO carries only the light count and the grid's parameters.
- **One lighting implementation.** Every lit program includes the shared GLSL for the cluster
  lookup and the BRDF (`lighting.glsl`, over `brdf.glsl`). No material lights itself its own way.
  The BRDF and the tonemapper are written in the subset of GLSL that GLM also compiles, so the
  render suite compiles the shaders' own source as C++ and checks it against reference values.
- **No G-buffer.** Effects that need one stay out, and screen-space reflections are one: they
  would need each pixel's roughness, metalness and a scene colour to march through, and fall back
  to the environment cube at a seam black space makes plain (reconsidered 2026-11-11,
  [decided](roadmap.md#decided)). Screen-space ambient occlusion is in (10c): the depth prepass
  writes view-space normals for it, and it attenuates the image-based light alone. The prepass
  writes object IDs too, for outlines, beside a stencil on the depth target (10c): masks that no
  material program writes.
- **Debug draw shows the grid**, as a lights-per-cluster heat map, so a light budget problem is
  visible rather than inferred.

The Vulkan features this needs are compute shaders and storage buffers, both core since 1.0, so
the headless render test on lavapipe covers every pass.

## Frame and tick structure

`Application::Tick` is a sequence of named phases (`engine/ecs`, `Ecs::kTickPhases`) that
systems register into, each declaring the components it reads and writes:

1. **Input**: poll devices and map them to actions in the active contexts.
2. **Commands**: turn actions and UI into simulation commands.
3. **Simulation**: zero or more fixed steps (commands, AI, economy, physics, orbits), as the
   accumulator allows.
4. **Sync**: copy simulation snapshots into presentation and interpolate.
5. **Presentation**: animation, cameras, audio, particles, UI layout.
6. **Render**: cull, collect lights, record the pass list (shadows, cluster light assignment,
   forward passes, post), submit, present. See [rendering](#rendering-clustered-forward).

Declared reads and writes are what let the job system parallelise later without a rewrite.

## Threading

To begin with: the main thread runs everything above, and one I/O worker thread reads files and
decodes assets for `resource`. Vulkan submission stays on the main thread. The job system is
deferred until a profile shows the need (see [scope](scope.md)). When it arrives, it runs whole
systems whose declared access does not conflict, not fine-grained tasks inside them.

## Lifetimes

The existing shell's rule stays: `Application::Init` builds subsystems in dependency order and
`Shutdown` destroys them in reverse. `Shutdown` is idempotent and `noexcept`, and
`~Application` calls it, so a frame that throws still releases the device. GPU objects are
released by explicit `Destroy()` calls in that order, because their lifetimes are bound to the
device's and to frames in flight rather than to C++ scope. Non-GPU types written from now on use
RAII. See [coding standards](coding-standards.md#ownership-and-lifetime).

GPU resources referenced by in-flight command buffers are destroyed only after the frame's
fence. Nothing the renderer caches is keyed on an address the scene owns: models and named
materials are keyed on virtual paths (or a procedural shape's description) and held as resource
handles, and what a reload or a release replaces waits out the frames in flight in a queue
rather than the device idling. `Scene::Generation` says only that the scene's contents were
replaced, so the renderer rebakes the environment and releases what the new contents no longer
draw.

## The engine today

The engine runs today. It renders a glTF scene through Vulkan with validation, and the gate
proves it headlessly. What exists:

- **App shell** (`engine/app`). `RunRenderApplication` is the entry point Flotsam,
  Jetsam and render_test call: it parses argv (`AppArguments.cpp`: `--smoke-test`, `--assets`,
  `--scene`, `--save-scene`), builds `Application`, and prints any exception the run threw.
  `Application::Init` stages are resources (the asset VFS, the I/O worker, the resource manager
  and the rig loader), scene (and the star system it names, read through the VFS), renderer, and
  window (its title and size). The shell holds the renderer as `Render::IRenderer` alone and
  includes no Vulkan header. The resource manager updates once a frame, before the render.
  `RunFrameLoop` runs `Tick` (the scheduler's phases, from input to render) until close.
- **Smoke sequence**. `SmokeSequence` is the render test's six stages (resize, resize,
  minimize, restore, scene reload, settle) as a state machine driven once per frame through
  five callbacks. It names no GLFW, Vulkan or scene type, and its counters (`Passed`,
  `Summary`) are the gate.
- **Scene** (`engine/scene`). An EnTT registry loaded from and saved to YAML
  (`assets/scene/main.yaml`, and `orbit.yaml` at real scale) through `serialise`'s archives, at
  file version 2: a field the file does not know is refused, naming its path and line, and the
  loader and the writer share one check of what a file may say. It has transform, parent,
  world-transform, camera, light (a point or a spot, with a range), sun, sky and mesh components
  (a glTF model by virtual path or a procedural shape - UV, ico or cube sphere, box, ring, cylinder
  - and optionally a material file, by virtual path), celestial bodies placed by the star system
  the scene names (a virtual path too), an orbit camera rig, and the animation module's
  rotation-animation, skeleton and animation-player components. It names no device: a model mesh
  holds its rig as a resource handle (`RigComponent`), and `SceneUpdateSystem` poses the model once
  the rig is in, reporting a clip the rig lacks once and posing it at rest. Translations and world
  transforms are doubles. Authored state and runtime state are kept apart: a rotation's angle, a
  clip player's time, a body's `PlacementComponent`, a rig's `CameraPoseComponent` and the rig
  handle are never saved, and the scene suite's round-trip tests hold the writer and the loader
  to that.
- **Animation** (`engine/animation`). `Rig` is a model's node hierarchy, skin joints and clips,
  with the clip player (`EvaluateSkin`) that used to be inside `Model`; `Playback.h` advances
  the rotation and clip-player components. `RigLoader.h` reads a rig from a glTF file through
  fastgltf, as a resource decoded on the I/O worker. The renderer reads the same file's geometry
  for its `Model`, so a model file is parsed twice, once for each. Nothing in it touches the
  device, so it is tested without one.
- **Vulkan backend** (`engine/render/src/vulkan`, private), on Vulkan 1.3, behind
  `render/IRenderer.h`: `CreateRenderer` makes the window, the device, the swapchain, the frame
  and the UI, and the interface is what the shell calls. `DeviceContext` holds the
  instance, device, debug-utils messenger (with synchronization validation in Debug), object
  names, and the VMA allocator every buffer and image comes from. `Swapchain` prefers an sRGB
  format and owns its image views. `Renderer` runs the frame as a declared pass list
  (`render/PassList.h`): the sun's [shadow cascades](#sun-shadows), the depth prepass (off), the
  cluster passes (the index
  list's allocator zeroed by a transfer, then the cluster bounds and the light assignment in
  compute), an opaque pass into a 4× multisampled
  RGBA16F target, which draws the sky after the surfaces and is resolved into a single-sampled
  target as it ends, an output pass that applies exposure and tonemaps on its way to the swapchain
  and draws the UI over it, and the cluster check's copy. The barriers between passes are derived
  from each pass's declared uses once, at start-up, and recorded with dynamic rendering and
  synchronization2. Draws are culled, given a level of detail and batched into instanced draws on
  the CPU before anything is recorded (["The frame"](#the-frame)), and each pass is timed on the GPU
  ([the budget](#the-budget)). The frame constants, skin palette, light list, instance buffer and
  command buffer exist once per frame in flight; the shadow map and the cluster grid's buffers are one set each, shared as the
  render targets are. The device has to offer depth clamp, which the shadow pass uses.
  `PipelineCache` makes graphics pipelines by program name and render state, and compute
  pipelines by program name, with the programs found through the asset VFS. Materials are data (`render/Material.h`): a material file
  names a program, whose descriptor (`shaders/<program>.program.yaml`) lists its parameters and
  texture slots, and sets them, some driven by presentation time. `MaterialCache` loads
  materials and their KTX 2 or stb textures through the resource manager (`Texture.h` decodes
  on the I/O worker; the loader's `finish` uploads), rebuilds a material's descriptor set when
  it or a texture reloads, and frees what a reload replaced once no frame in flight reads it; a
  model without a material draws with its own textures through the default `mesh` program.
  `Model` loads glTF through fastgltf, as one level of detail, and a procedural shape as five, in
  one vertex and index buffer, with a bounding sphere either way. `ModelCache` resolves what a
  mesh names: a model file is a resource, decoded on the I/O worker, uploaded by its loader's
  `finish` and reloaded when its file changes; a procedural shape is made on first use. A model
  file's mesh is not drawn until its rig has posed it, since the rig places the mesh within its
  file. The `mesh` program is glTF's metallic-roughness model, lit
  by the sun, shadowed through its cascades, the [environment](#the-environment) and the local
  lights of the
  [cluster](#rendering-clustered-forward) the fragment falls in. `unlit` is for what is a light
  rather than lit by one.
- **ImGui** for a demo window, toggled with F1, and the Renderer panel: the frame's counts and
  each pass's GPU time.
