# Architecture

How the engine is laid out, how its parts depend on each other, and the handful of
architectural rules that are expensive to change later. The [scope](scope.md) says what is
built; the [roadmap](roadmap.md) says in what order.

## Repository layout

The target layout. Phase 1 of the roadmap moved the existing code into it (`core`, `animation`,
`render`, `app`, the executables under `tools/` and `samples/`) and stubs every other in-scope
module.

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
    │   ├── sim/                simulation world, command queue, snapshots, definition registry
    │   ├── celestial/          celestial clock, on-rails orbits, system and galaxy frames
    │   ├── scene/              scene format, layers, system load and unload
    │   ├── spatial/            per-system spatial index, queries
    │   ├── input/              actions, contexts, bindings, devices
    │   ├── render/             renderer interface, frame, cameras, materials, debug draw
    │   │   └── vulkan/         the Vulkan backend (today's source/renderer/vulkan)
    │   ├── animation/          glTF clip player, TRS tracks, mechanical skins
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

Phase 1 moved code without changing what it does, and two things landed somewhere other than
their final home:

- **The scene graph is in `render`**, not `scene`. `Scene` loads models through the device,
  waits on it before destroying them, and hands `Model` pointers to the renderer, while the
  renderer reads the camera and light components back out of the scene registry. That is a
  cycle, and putting the scene below `render` means inventing the seams (an injected model
  loader, a forward-declared model) that phase 3's resource manager replaces anyway. The scene
  format and its round-trip test stay in `render` until phase 3 gives the scene handles instead
  of pointers; phase 6 then builds the per-system scene in `engine/scene`.
- **`render` depends on `animation`**, a sibling in its tier. The glTF loader fills an
  `Animation::Rig` (nodes, skin joints, clips), and the scene update system drives the rig's
  clip player. The edge points the right way: animation knows nothing about the device.

The executables keep the names they already have: **Flotsam** is the editor, **Jetsam** is the
runtime, now a sample viewer because the game has its own repository, and **render_test** is
the render smoke harness that the gate runs. It was `test` until CTest reserved that name (see
[build and gates](build-and-gates.md#unit-tests)).

### Consumption by the game repository

The engine installs and exports `JettisonTargets.cmake` and a `JettisonConfig.cmake`, so the
game uses either `find_package(Jettison)` or `FetchContent` with a pinned commit and links
`Jettison::Engine`, an interface target over every module. Public headers are the
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
        sim; celestial; spatial; scene; input
    end
    subgraph T4[Presentation and simulation services]
        render --> animation
        physics; audio; ui; ai; script
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
| Local | `double` | a station or planet | docking approaches, station-relative traffic |
| Render | `float` | the camera | everything submitted to the GPU |

Rendering is **camera-relative**: each frame subtracts the camera's double-precision position
from every object's before converting to float, so precision is always best where the player is
looking. This is the floating origin. It is never visible to simulation code. Depth is
**reversed-Z with an infinite far plane**, so a cockpit at half a metre and a planet at
forty thousand kilometres share a depth buffer. The existing `GLM_FORCE_DEPTH_ZERO_TO_ONE`
setting is the prerequisite for that.

## Frame and tick structure

`Application::Tick` today runs events, hot keys, UI and render. With the scheduler
(`engine/ecs`), it becomes a sequence of named phases that systems register into, each
declaring the components it reads and writes:

1. **Input**: poll devices and map them to actions in the active contexts.
2. **Commands**: turn actions and UI into simulation commands.
3. **Simulation**: zero or more fixed steps (commands, AI, economy, physics, orbits), as the
   accumulator allows.
4. **Sync**: copy simulation snapshots into presentation and interpolate.
5. **Presentation**: animation, cameras, audio, particles, UI layout.
6. **Render**: cull, collect, record, submit, present.

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
fence. The scene generation counter (`Scene::Generation`) is how the renderer learns that cached
resources keyed on model addresses are stale. The resource manager replaces address keys with
handles carrying a generation, which retires that hazard class.

## The engine today

The engine runs today. It renders a glTF scene through Vulkan with validation, and the gate
proves it headlessly. What exists:

- **App shell** (`engine/app`). `RunRenderApplication` is the entry point Flotsam,
  Jetsam and render_test call: it parses argv (`AppArguments.cpp`: `--smoke-test`, `--assets`,
  `--scene`, `--save-scene`), builds `Application`, and prints any exception the run threw.
  `Application::Init` stages are window, device and swapchain, scene, pipeline, UI, renderer.
  `RunFrameLoop` runs `Tick` (events, hot keys, UI, render) until close.
- **Smoke sequence**. `SmokeSequence` is the render test's six stages (resize, resize,
  minimize, restore, scene reload, settle) as a state machine driven once per frame through
  five callbacks. It names no GLFW, Vulkan or scene type, and its counters (`Passed`,
  `Summary`) are the gate.
- **Scene** (`engine/render`, see [parked code](#parked-code)). An EnTT registry loaded from
  and saved to YAML (`assets/scene/main.yaml`). It has transform, parent, world-transform,
  camera, light and mesh components, and the animation module's rotation-animation, skeleton
  and animation-player components. Authored state and runtime phase are kept apart:
  `RotationAnimationComponent::angle` and `AnimationPlayerComponent::time` are never saved, and
  the render suite's round-trip test holds the writer and the loader to that.
- **Animation** (`engine/animation`). `Rig` is a model's node hierarchy, skin joints and clips,
  with the clip player (`EvaluateSkin`) that used to be inside `Model`; `Playback.h` advances
  the rotation and clip-player components. Nothing in it touches the device, so it is tested
  without one.
- **Vulkan backend** (`engine/render/.../vulkan`). `DeviceContext` holds the instance, device,
  debug-utils messenger and object names. Then `Swapchain`, `Pipeline` (one graphics pipeline,
  descriptor pools sized from what is allocated), `Renderer` (acquire, record, submit, then
  `Present`), and `Model` (fastgltf loading of vertices, primitives and the rig). There is one
  vertex/fragment shader pair.
- **ImGui** for a demo window, toggled with F1.
