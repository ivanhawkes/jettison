# Roadmap

The engine's open work: what is being delivered now, then the numbered backlog. Finished work moves
to the [history](history.md); settled choices are in [decisions](decisions.md).

## How the order is set

Work is ordered by what is expensive to change later, then by what later work stands on, never by
what looks good in a screenshot. A choice that fixes what a [save](glossary.md#save) holds, or what
crosses into and out of the [step](glossary.md#step), comes first: each save a player keeps, and
each player who sends [commands](glossary.md#command), makes it dearer to change.

[Manifest](game-integration.md), the engine's first production game, sets what is needed next; its
task queue is [its delivery order](../../manifest/docs/delivery-order.md). A capability a Manifest
task needs and the engine lacks is provisioned here as a delivery, in Manifest's order, by the
workflow in [game integration](game-integration.md#provisioning-a-capability). Game rules, prices
and content stay in Manifest. Work is done when the [gate](glossary.md#gate-ci) covers it and the
history records the commit that did it.

Two tracks are planned apart from the backlog and scheduled on their own:
[dense geometry and ray tracing](#a-separate-track-dense-geometry-and-ray-tracing), J36 to J50, and
[characters](#a-separate-track-characters), J51 to J60.

The backlog's order is the order of its list, not of its IDs. An ID is given once, when an item is
added, and kept: an item added later takes the next number (J30 onwards) wherever its tier places
it, and an item done or dropped leaves its number unused.

## Current deliveries

Manifest's Stage 5 closed on the engine's commit `13c1bec` (2026-11-25). Manifest now builds against
`70cd4de`, the docs review's head; the four gaps its Stage 5 cross-review found were provisioned
by `459af99` and the commit before it ([history](history.md)). Its later work waits until the user
has played its first loop for a day or two. No delivery is open: encryption, J01, a script's
sender, J02, and dock control a game decides, J35, are done ([history](history.md)), and of the
backlog behaviour trees, J03, scripts, J04, packs and mods, J05, and the shell's log file, J10. Manifest's adoption of encryption, its server's key in
a file and the public half in its test builds' client configuration, is Manifest's.

## Waiting on you

What needs the user's answer or action and is not a backlog item. Each line goes when it is settled.
Nothing waits on the user now.

## The backlog

Each item has a fixed ID, so other documents can link it. Items are grouped in five tiers, in the
order above, the most pressing first within a tier. Each says what remains, why it
matters, what already exists, and which document describes the feature.

## Tier 1: what saves and the step's interface hold

No item is open in this tier: the last, a script's sender (J02), is done ([history](history.md)).

## Tier 2: what a game's next work stands on

What a game reaches for as its AI, content and tools grow. None changes a save already written, but
each is cheaper before a game builds a workaround.

<a id="j06"></a>
### J06. Picking a paused world, and under the cursor every frame

**What remains.** A pick answered while the world is paused, and a pick under the cursor every
frame, for a hover highlight. Both want a query that presentation makes itself, against the bodies'
shapes in a snapshot.

**Why.** A pick is a command the next step answers with an event ([decided](decisions.md#physics)),
so a paused world answers only when it resumes, and a hover would send a command every frame.

**What exists.** `Physics::Pick` and `Physics::Picked`
(`source/engine/physics/include/jettison/physics/PhysicsWorld.h`); the shell asks the game where a
click picks (`App::Game::PickIn`).

**Feature doc:** [physics](physics.md).

<a id="j07"></a>
### J07. Physics

**What remains.**

| Feature | Why |
| --- | --- |
| A shape offset in its body, and compound shapes | Most objects are not one primitive centred on their body. |
| Collision filtering beyond static against moving | A game's own layers and groups: what may touch what. |
| Jolt's debug renderer drawn through debug draw | Seeing the shapes Jolt actually has. |
| Jolt's job system on more than one thread | Once a profile asks for it. The result is deterministic either way. |
| Capacities tuned against real traffic | The defaults are guesses. |
| Overlap queries, and casts that return every hit | "Every body in this shape" and "everything along this ray", for triggers and AI. Steering did without. |

**What exists.** Shapes are Box, Sphere, Capsule, Cylinder, ConvexHull and Mesh; a mesh is for
static and kinematic bodies only (`source/engine/physics/include/jettison/physics/Shape.h`).
`PhysicsWorld::CastRay` and `CastShape` return the first hit. Jolt runs on
`JPH::JobSystemSingleThreaded` (`physics/src/PhysicsWorld.cpp`). The capacities are `maxBodies`
8192, `maxBodyPairs` 32768 and `maxContactConstraints` 16384 (`PhysicsWorld.h`).

**Feature doc:** [physics](physics.md).

<a id="j08"></a>
### J08. Steering

**What remains.** A course round the inside of a [platform](glossary.md#platform), such as from one
[berth](glossary.md#berth) to another under a pier. And steering measured for a lane busier than one
platform's traffic, where every vehicle queries the spatial index each step.

**Why.** A vehicle reaches a berth only by coming straight down from outside the platform's sphere,
so a layout must leave that column clear. Steering has been measured for sixteen vehicles, one
platform's traffic; a busier lane's cost is unknown.

**What exists.** An obstacle is the sphere about its body that holds its shape
(`source/engine/ai/include/jettison/ai/Steering.h`). A destination flown `byShape` sweeps the
vehicle's own shape along its way (`PhysicsWorld::CastShape`), which slows it but does not turn it.

**Feature doc:** [AI](ai.md).

<a id="j09"></a>
### J09. The way in to a berth, done properly

In Tier 2 rather than Tier 3 because every NPC and autopilot in Manifest docks through it.

**What remains.** The approach, designed with the user on 2026-11-21:

- an approach corridor flown without stopping, at a speed that falls with the distance left;
- the platform not an obstacle on the corridor's last leg, as it is not on the way down (`byShape`);
- Arrive damping the velocity across its line, rather than pursuing the point;
- a reservation that lapses when a vehicle stops making progress, rather than after fixed seconds.

It is measured by vehicles flown in from every side and speed, one queued behind another on the next
berth, and no arrival slower than the stopgap's.

**Why.** The stopgap cures a vehicle arriving fast and sideways that overshot and circled, but every
vehicle still stops out along the normal before it comes in.

**What exists.** The stopgap, in [the way in](docks.md#the-way-in) (`Dock::Docks::FlyInbound`,
`FlyAbove`, `source/engine/dock/src/Docks.cpp`). A reservation lapses after the layout's
`reservationSeconds`, 300 by default (`dock/Definitions.h`).

**Feature doc:** [docks](docks.md).

<a id="j30"></a>
### J30. The shell started through a session

**What remains.** The rendered shell starts its world through `Host::Session`, so one set of
start-up hooks serves a game's server, its tests and its client: the session's phases, its checks
on the packages and its `Host::StartupError` naming the phase, then the shell's presentation over
the world it made. It is done when the shell runs a game composed as a `Host::Game` beside its
`App::Game`, and the ground sample and the render test start that way.

**Why.** A game drawn on screen writes its start-up twice: `Host::Game`'s `RegisterTypes`,
`LoadContent`, `RegisterWorld` and `CreateInitialState` for the session, and `App::Game`'s
`Register`, `LoadDefinitions` and `Define` for the shell, which can drift apart. Manifest's
server, console and tests start through a session; its graphical client (its L15) is the first
program to need both.

**What exists.** The shell holds its own `Sim::SimWorld`, with a fixed step of 1/60 s and its own
seed (`source/engine/app/src/Application.h`), and reads each package's `mod.yaml` as a
`Host::PackageDescription` (`app/src/Mounts.cpp`), as a session does
([the host session](simulation.md#the-host-session)).

**Feature doc:** [the app shell and tools](app-shell.md).

## Tier 3: robustness and tools

Cheap to add when wanted, or waiting on a profile, and costing nothing meanwhile.

<a id="j11"></a>
### J11. Crash handling

**What remains.**

- A report from a thread other than the main one whose stack overflowed, which wants an alternate
  signal stack on every thread the engine starts (the I/O worker, the scripts' watchdog) and on
  Jolt's once it has threads ([J07](#j07)).
- Each module's build ID in the report, a release's debug files kept by it, and the shell saying on
  its next start where a new report is (decided 2026-11-13).
- A report sent somewhere, which waits on players outside development.
- A minidump, should a report's frames and log tail not be enough.

**What exists.** `Platform::InstallCrashHandler` sets one alternate stack, for the thread that
installs it, and writes a report with its backtrace, named by a symboliser when one is found
(`source/engine/platform/src/Crash.cpp`).

**Feature doc:** [runtime](runtime.md).

<a id="j12"></a>
### J12. A glTF model's collision mesh

**What remains.** A model's triangles read from its glTF file on the I/O worker, as its rig is, or a
collision mesh in a file beside it.

**Why.** A vehicle drawn from a model collides as the box of its definition's size.

**What exists.** `Physics::ShapeKind::Mesh` and `ConvexHull` take vertices given in data or code
(`source/engine/physics/include/jettison/physics/Shape.h`). A platform built to a layout collides as
its decks, a mesh the docks make from the layout's boxes (`dock/src/Definitions.cpp`). A
`Dock::VehicleDefinition` has a length, beam and height, and an optional model drawn in place of its
proxy (`dock/Definitions.h`).

**Feature doc:** [physics](physics.md).

<a id="j13"></a>
### J13. A scene's models prefetched before it is shown

**What remains.** A scene's models asked for before the scene is shown, so its first frames draw
complete.

**Why.** The renderer asks for a model the first frame a scene draws it, and draws nothing for it
until the file has loaded, so after a scene switch its models appear over its first frames (0.2 to
0.4 s on lavapipe for the retired space sample's scenes). Manifest's jump between systems is one
user.

**What exists.** `Render`'s model cache: a model file is a resource, decoded on the I/O worker and
reloaded when it changes (`source/engine/render/src/vulkan/ModelCache.h`).

**Feature doc:** [rendering](rendering.md).

<a id="j14"></a>
### J14. UI

**What remains.**

- A stylesheet or document reloaded when its file changes. RmlUi keeps a stylesheet for the run.
- A document's images decoded on the I/O worker. They are decoded on the main thread as the document
  lays out, which is fine for a screen's icons.
- RmlUi's debugger in the dev tools.

**Why.** A UI author iterates with reload and the debugger; decoding off the main thread matters
once a screen shows large images.

**Feature doc:** [UI, input and localisation](ui.md).

<a id="j15"></a>
### J15. Localisation

**What remains.**

- Numbers with fractions, and dates and times, in the language's own form; only a whole number's
  thousands are.
- Text a document holds in an attribute, which RmlUi does not hand to the string table.
- Right-to-left and shaped scripts. RmlUi can lay them out with HarfBuzz; the fonts shipped cannot
  draw them.
- The player's language chosen at the first start from the platform's (Steam's, the environment's).
  The setting stands in for it.

**What exists.** `Ui::StringTable` with `Format` and `Plural`, and plural categories per language
(`source/engine/ui/include/jettison/ui/StringTable.h`).

**Feature doc:** [UI, input and localisation](ui.md).

<a id="j16"></a>
### J16. A test of an acquire out of date twice

**What remains.** A test of the renderer's recreate loop when the swapchain is out of date, and of
its failure after `kMaxRecreates` (8) tries (`source/engine/render/src/vulkan/Renderer.cpp`).

**Why.** That branch is not covered: it needs fault injection under the Vulkan layer, which the
engine lacks. Taken up when the branch is needed, if ever.

**Feature doc:** [rendering](rendering.md).

<a id="j17"></a>
### J17. Comments better as names

**What remains.** Found by the comment review (2026-11-18):

- `kFullTurn` (`source/engine/animation/src/Playback.cpp`) as `glm::two_pi<float>()`.
- `kBelowOne` (`source/engine/time/src/FixedStep.cpp`) as `kLargestBelowOne`. `std::nextafter` is
  constexpr only from C++23, so the constant stays.
- The trailing comments that `scripts/comment-only.sh` cannot tell from code, so left: the glosses
  on the console lines' tokens in `source/engine/ui/imgui/src/Theme.cpp`, and the comment
  `source/engine/render/src/vulkan/imgui/FontFaces.cpp` wraps over three lines.

**Why.** A name that says what a value is needs no comment to explain it ([coding
standards](coding-standards.md#comments)).

<a id="j18"></a>
### J18. A prebuilt SDK

**What remains.** `JETTISON_INSTALL=ON` made a supported path, so a game can
`find_package(Jettison)` rather than build the engine from source.

**Why.** Only when a game needs a prebuilt engine; Manifest builds it from source, pinned to a
commit.

**What exists.** The option and why CMake refuses to export it:
[installing](build-and-gates.md#installing-not-yet-supported).

**Feature doc:** [build and gates](build-and-gates.md#consuming-the-engine).

<a id="j19"></a>
### J19. A pack signed as a whole

**What remains.** A signature over a whole pack, checked as it mounts.

**Why.** A player installs mods made by others, and nothing shows that a pack is the one its author
released.

**What exists.** Per-file checks that catch damage, not tampering
([packs](files-and-mods.md#packs)).

**Feature doc:** [files, packs and mods](files-and-mods.md).

<a id="j31"></a>
### J31. A parallel scheduler

**What remains.** Systems in one phase whose declared reads and writes do not conflict run on
several threads, and those that conflict keep the order the scheduler fixes, so a step ends in the
same state, to the hash, on any number of threads. It is done when a profiled phase runs in
parallel and its [state hash](glossary.md#state-hash) matches the one-thread run's.

**Why.** Every system runs on the main thread, one after another. It is deferred until a profile
shows a phase the main thread cannot run in its frame ([scope](scope.md#runtime-core-and-platform)).

**What exists.** `Ecs::Scheduler` runs a phase's systems in order on the calling thread
(`source/engine/ecs/include/jettison/ecs/Scheduler.h`). Each system declares its access, and
`Ambiguities(phase)` lists the pairs whose access conflicts and which no constraint orders: what a
parallel scheduler would have to keep apart ([the scheduler](simulation.md#the-scheduler)). The
engine's threads are in [architecture](architecture.md#threading).

**Feature doc:** [simulation](simulation.md).

<a id="j32"></a>
### J32. A durable write that refuses to replace a file

**What remains.** A durable write that fails when the file is already there, deciding that in the
same rename that puts the file in place (`renameat2` with `RENAME_NOREPLACE`), with the partial
file removed. It is done when two writers making the same new file leave one file whole and the
other told it was refused.

**Why.** `WriteFileDurably` replaces whatever file is there. A writer that must not (a new save's
name, a world's first file) can only check first, and another writer can come between the check and
the write. Nothing has asked for it yet.

**What exists.** `Core::WriteFileDurably`: the partial file, `fsync`, the rename over the file and
the directory's `fsync` (`source/engine/core/include/jettison/core/File.h`).

**Feature doc:** [persistence](persistence.md).

<a id="j33"></a>
### J33. Fault injection under the file system calls

**What remains.** A test that makes each system call under `WriteFileDurably` and
`CreateDirectoriesDurably` fail in turn (a short or failed write, the file's `fsync`, the rename,
the directory's `fsync`) and checks that each throws naming the path, with no partial file left
and any old file as it was.

**Why.** That is what `File.h` promises, and a save's safety rests on it, but a working disk does
not fail on demand: the `core` suite reaches only a failed open and a file in the way of a
directory (`source/engine/core/tests/FileTests.cpp`). Nothing has asked for it yet.

**What exists.** The calls are made directly in `core/src/File.cpp`, with no seam to fail them.
[J16](#j16) wants the same under the Vulkan layer.

**Feature doc:** [persistence](persistence.md).

<a id="j34"></a>
### J34. A lock's holder named

**What remains.** `FileLock` writing who holds it (the process's ID and program name) into its
file, and a refused `TryAcquire` naming that holder, so "the world is already open" says by
whom. The kernel's lock still decides who holds it: a name left by a crash is read, never trusted.

**Why.** A refused `TryAcquire` returns nothing but `nullopt`, and the file holds nothing, so a
player or an operator told a directory is in use cannot tell which process to stop. Nothing has
asked for it yet.

**What exists.** `Core::FileLock` on the kernel's `flock`, freed with the process however it
ends (`source/engine/core/include/jettison/core/FileLock.h`), tested against a second process and a
killed holder.

**Feature doc:** [persistence](persistence.md).

## Tier 4: presentation, when a scene asks

Each is placed by the first scene or screen that needs it, and changes no data or save on the way.

<a id="j20"></a>
### J20. Animation

**What remains.**

- A berth's clamps fitted to a vehicle's shape rather than its box, so on a vehicle drawn by its
  model they grip where its sides are.
- Rigs of a vehicle's own (landing gear, a hatch), which the docks' vehicle proxies lack.

A glTF clip's events and cross-fades between clips moved to the characters track ([J54](#j54),
[J53](#j53)).

**What exists.** Rigs and their clips from glTF
(`source/engine/animation/include/jettison/animation/Rig.h`).

**Feature doc:** [animation and cinematics](animation.md).

<a id="j21"></a>
### J21. Audio

**What remains.**

- Music that crossfades between pieces, and a playlist to choose them.
- Doppler, once something reports velocities to presentation.
- A vehicle's own sounds, and occlusion by what is in the way.
- A sound file streamed from its mount rather than read whole. That waits on a streaming read in the
  VFS: a pack's stored files could be read in ranges, its compressed ones would want zstd's
  streaming decoder.

**What exists.** One piece of music at a time, read whole and decoded as it plays, and a voice bus
with nothing on it ([audio](audio.md), `source/engine/audio/include/jettison/audio/AudioSystem.h`).

**Feature doc:** [audio](audio.md).

<a id="j22"></a>
### J22. Cinematics and grading

**What remains.**

- A sequence's events heard by the shell, so data alone can tie a sound to one.
- A shot's roll, and depth of field.
- A camera blend that steers round an object rather than along a line; a rail is drawn to avoid it.
- A grade blended between two tables, for a sequence's own look or a scene's change of one.

**What exists.** Sequences of shots, fades, letterbox bars and named events
(`source/engine/scene/include/jettison/scene/Sequence.h`); a colour grade as a 3D lookup table
(`render/ColourGrade.h`).

**Feature doc:** [animation and cinematics](animation.md); grading in [transparency, particles and
post-processing](rendering-effects.md).

<a id="j23"></a>
### J23. The UI's drawing

**What remains.**

- RmlUi's clip masks: an element with `overflow` clipped by a rounded border, or clipped while
  transformed. They want a stencil in the output pass, which draws the UI into the swapchain without
  the depth target's stencil.
- RmlUi's layers, filters and shaders (`filter`, `backdrop-filter`, `mask-image`, gradients drawn as
  shaders), which want render targets of their own, as the post chain's are.

Until then RmlUi's defaults draw nothing for them, and no screen uses them.

**Feature doc:** [UI, input and localisation](ui.md).

<a id="j24"></a>
### J24. Transparent draws and particles

**What remains.**

- Particles sorted within an emitter, on the GPU, for a smoky emitter seen through itself.
- Soft particles where they meet a surface, which want the depth the opaque pass is still writing,
  so a render-graph node of their own after it. The transparent draws are recorded inside `opaque`.
- Refraction through glass, from a copy of the frame behind it, which the opaque pass's resolved
  target would give were the transparent draws a node of their own.
- A glTF primitive's `BLEND` alpha mode, which today draws opaque.
- A transparent surface's tinted shadow, which the sun's depth-only cascades cannot hold.
- An emitter started warm, as if it had been running; a capture of a smoke column waits three
  seconds instead.

**Feature doc:** [transparency, particles and post-processing](rendering-effects.md).

<a id="j25"></a>
### J25. The post chain

**What remains.**

- A transparent part of an outlined shape (a canopy) in the outline. It is not in the depth prepass,
  so the outline runs round the opaque parts alone.
- A pick read from the object IDs under the mouse; the physics ray answers it.
- The game UI and the dev tools under a mask. The output pass draws them without the stencil.
- Ambient occlusion at half the frame's size, or kept from frame to frame, should a measured frame
  want it cheaper.

**Feature doc:** [transparency, particles and post-processing](rendering-effects.md).

<a id="j26"></a>
### J26. The dev tools' theme leftovers

**What remains.**

- The game UI's own stylesheet in the same palette, so a game's screens and the tools read as one
  design: an RmlUi stylesheet over the VFS, no engine work.
- The palette as a player setting on a game's own settings screen.
- A light-attachment swap noticed mid-run. `ApplyThemeIfChanged` answers only when a setting
  changes.
- The darkest tones exact on the palette, which wants the sRGB encoding done in the UI's own shader
  over a UNORM attachment: ImGui packs a vertex colour into 8-bit linear bytes, so `#1e1e2e` lands
  as `#1c1c2e`. This is the further work of the theme review's finding F5
  ([the record](history/devtools-theme.md#f5-in-detail--the-colour-contract)); finding F6 carries
  none, its corrected explanation committed and its stock start tested.

**What exists.** The dev tools are themed on Catppuccin's four varieties, switched live by the
`dev.palette` and `dev.ui_scale` settings
(`source/engine/ui/imgui/include/jettison/devtools/Theme.h`).

**Feature doc:** [UI, input and localisation](ui.md).

<a id="j27"></a>
### J27. Rendering not yet placed

**What remains.**

| Item | Note |
| --- | --- |
| Shadow tuning | The sun's shadow distance and split blend tuned by measurement. |
| Spot and point light shadows | Only the sun casts shadows. Ray-traced ones come with [J45](#j45) on devices with ray queries; this row is the fallback's. |
| Planets' own shadows | Past the shadow distance (an object in eclipse), by an analytic test against the bodies. |
| Programs that hot-reload | Shaders reloaded as they change. |
| Cube maps and texture arrays in files | |
| Normal mapping in `mesh` | Waits for tangent-space vertex data, which `math/Tangents.h` makes. A planet builds its frame from the sphere and does not need it. |
| A glTF model's own metallic-roughness factors and textures | |
| A model file read once | Today it is read twice, for its rig and for its geometry. |
| An emissive scale per draw | A mesh's own, or a material parameter set per draw, so a glow can change smoothly without a model per colour. The renderer keeps one procedural model per shape and colour (`render/src/vulkan/ModelCache.h`). |
| Air over a mesh | The haze is the planet's own surface's and the sky's, not that of a mesh within the air, nor of a transparent draw. Two bodies' air and rings are not sorted where they overlap on the screen. |
| The environment capture's planets | The capture draws a body as a sphere of its mesh's colour, without its maps or clouds. |
| A gas giant's bands | The test content's Corva is one colour under its air. |

**Feature doc:** [rendering](rendering.md).

## Tier 5: the engine's own samples

Loose ends of the [samples](sample-game.md), for when a sample is next extended; none is an engine
requirement.

<a id="j28"></a>
### J28. The ground sample's frames held

**What remains.** `scripts/capture.sh` taking the executable to run, and a ground view in
`source/test/frames/views.txt`, so a [reference frame](glossary.md#reference-frame-image) holds the
ground sample's look.

**Why.** `capture.sh` runs `render_test` alone, so the ground sample's scene and chase camera were
checked once, by eye. It matters before Manifest's graphical client, when a second game's look
counts.

**Feature doc:** [samples](sample-game.md); [reference frames](build-and-gates.md#reference-frames).

<a id="j29"></a>
### J29. The ground sample's loose ends

**What remains.**

- The ground has no name, so a pick logs it as `Entity 0x0`: `GroundGame::Describe` names only the
  rover.
- The rover is a box sliding on the ground's friction, which needed 8 rad/s² to turn and drifts
  sideways, not a vehicle that grips (`source/samples/ground/src/Ground.cpp`).

Nothing stands on them.

**Feature doc:** [samples](sample-game.md#the-ground-sample).

## A separate track: dense geometry and ray tracing

Meshlets drawn by mesh shaders, an asset cook, streaming, and ray-traced shadows, ambient occlusion
and reflections, with RTX Mega Geometry on NVIDIA: the plan agreed on 2026-11-25, for Manifest's
station kits, instance fields and asteroids. It is scheduled apart from the backlog's tiers until
the user places it, and none of it is started. Its design, decisions, budgets, risks and each
stage's exit criterion are in [dense geometry and ray tracing](dense-geometry-and-ray-tracing.md#stages);
the rows below say what each stage is, what it waits on, and which backlog items it touches.

| Stage | What it is | Needs | Touches |
| --- | --- | --- | --- |
| <a id="j36"></a>J36. Vulkan 1.4 and the device's capabilities | `kApiVersion` at 1.4, and one table of the optional device features (mesh shaders, ray queries, acceleration structures, the cluster extension and their limits), logged, each path overridable by a setting and a flag. | — | — |
| <a id="j37"></a>J37. The geometry module, the cook, and levels of detail for model files | meshoptimizer added; the CPU module `geometry`; `jettison_cook` cooking models and textures once, with layered settings files and a cook record over each source's references; the cooked GLB's reader for models, rigs and KTX 2 textures; the engine's content cooked at build time, dense test meshes generated; levels of detail and hysteresis for model files; `SetLodThreshold` on `IRenderer`. | J36 | Takes [J27](#j27)'s level-of-detail row; leaves [J12](#j12) small |
| <a id="j38"></a>J38. The cook's watch mode | `jettison_cook --watch`: the artists' daemon, cooking a source tree outside the game's repository into its asset folder as files change. | J37 | — |
| <a id="j39"></a>J39. The meshlet hierarchy, cooked, and its page file | Each static mesh cooked into a meshlet hierarchy (`clusterlod.h`) and a page file beside its GLB; the meshlet size chosen by measurement. | J37 | — |
| <a id="j40"></a>J40. GPU culling and mesh-shader drawing | Instances and meshlets culled on the GPU, drawn by task and mesh shaders in the prepass, opaque pass and cascades, in a deterministic order; the indexed path the fallback. | J39 | [J28](#j28)'s frames recorded again |
| <a id="j41"></a>J41. Occlusion culling | Two passes against a depth pyramid; scope says In. | J40 | — |
| <a id="j42"></a>J42. Ray-tracing foundation | Bottom-level structures per model from one cooked level, the TLAS each frame in render space, the build pass, refits from [J51](#j51)'s deform pass, the bindless material table. | J37, J51 | — |
| <a id="j43"></a>J43. Streaming | Meshlet pages read as the view needs them, through ranged VFS reads, into a pool of fixed-size pages; the coarsest levels always loaded; one memory budget over the GPU's geometry, with an order for giving way when it cannot hold. | J40 | Changes the model cache: [J13](#j13) first, or within it |
| <a id="j44"></a>J44. Ray-traced sun shadows | One ray towards the sun in the lit programs' ray-query variant; the cascades the fallback. | J42 | — |
| <a id="j45"></a>J45. Ray-traced local shadows | `castsShadows` opts a point or spot light in; a bounded number per pixel, nearest first. | J44 | The ray-traced half of [J27](#j27)'s spot and point shadows |
| <a id="j46"></a>J46. Ray-traced ambient occlusion | Rays in the existing ambient-occlusion pass, with its inputs and blur. | J42 | — |
| <a id="j47"></a>J47. Sharp ray-traced reflections | One ray a pixel below a roughness cut-off, hits shaded by the sun, the environment and emission; the environment cube otherwise. | J44 | — |
| <a id="j48"></a>J48. Mega Geometry | `VK_NV_cluster_acceleration_structure` where present: a traversal pass writing the raster's and the rays' cuts, CLAS per loaded meshlet and per-instance structures from the rays' cut, which streams like the raster's; checked on Lythir by `scripts/mega-geometry-check.sh`, outside the gate. | J42, J43 | — |
| <a id="j49"></a>J49. Temporal foundation and glossy reflections | Motion vectors, a frame history and FidelityFX v1.1.4's denoisers (MIT); glossy reflections and soft shadows on them; temporal AA replacing MSAA. | J47 | — |
| <a id="j50"></a>J50. Visibility buffer and software raster | A 64-bit visibility buffer where the device has 64-bit atomics, its material resolve, and a compute raster for pixel-sized triangles. | J41, J49 | — |

Tracks A (J37 to J43, geometry) and B (J42, J44 to J47, ray tracing) can alternate once J37 is done.
**Feature doc:** [dense geometry and ray tracing](dense-geometry-and-ray-tracing.md).

## A separate track: characters

Faces that talk and emote, people who walk, and the skin, eyes and hair to shade them: the plan
agreed on 2026-11-25, for Manifest's portraits, interiors and pilots. It is scheduled apart from the
backlog's tiers until the user places it, after [J37](#j37) and before or beside [J42](#j42), and
none of it is started. Its design, decisions, risks and each stage's exit criterion are in
[characters](characters.md#stages).

| Stage | What it is | Needs | Touches |
| --- | --- | --- | --- |
| <a id="j51"></a>J51. The deform pass and rigs with several meshes | One compute pass skins each animated instance into its own vertex buffer, which every pass reads; several mesh nodes per model. | J37 | [J42](#j42) refits from it |
| <a id="j52"></a>J52. Blend shapes | glTF morph targets read, cooked and applied in the deform pass; weight tracks; weights set by name. | J51 | — |
| <a id="j53"></a>J53. Poses, cross-fades and layers | Clips sampled to poses and blended; a changed clip fades from the current pose; layers by joint mask. | J51 | Takes [J20](#j20)'s cross-fade row |
| <a id="j54"></a>J54. Clip events from the cook, sync markers and clip speed | Events in cooked glTF clips; foot contacts and each clip's speed found by the cook. | J37, J53 | Takes [J20](#j20)'s glTF-clip-events row |
| <a id="j55"></a>J55. Blend spaces | 1D and 2D blend spaces in files, kept in step on sync markers, at a rate matched to speed; the ground sample's walker. | J54 | — |
| <a id="j56"></a>J56. The character controller | Jolt's `CharacterVirtual` behind the physics interface: walking, steps, slopes, moving decks. | — | — |
| <a id="j57"></a>J57. Lip sync at cook time | Rhubarb Lip Sync run by the cook on dialogue audio, mapped to ARKit-named shapes, played with the voice. | J52, J53 | A voice's play position from [J21](#j21)'s audio |
| <a id="j58"></a>J58. Skin and eyes | Pre-integrated skin and an eye material. | J37 | — |
| <a id="j59"></a>J59. Hair cards and alpha-tested materials | The first alpha-tested materials, in the raster passes and as non-opaque geometry for ray queries. | J51 | [J44](#j44)'s shadow rays; [J49](#j49)'s dither |
| <a id="j60"></a>J60. Two-bone IK and look-at, later | Feet on steps, hands on controls, heads and eyes that follow a target. | J53, J56 | — |

**Feature doc:** [characters](characters.md).
