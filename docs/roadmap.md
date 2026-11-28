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

Three tracks are planned apart from the backlog and scheduled on their own:
[dense geometry and ray tracing](#a-separate-track-dense-geometry-and-ray-tracing), J36 to J50 and J68,
[characters](#a-separate-track-characters), J51 to J60, and [the art standards'
support](#a-separate-track-the-art-standards-support), J64 to J67.

The backlog's order is the order of its list, not of its IDs. An ID is given once, when an item is
added, and kept: an item added later takes the next number (J30 onwards) wherever its tier places
it, and an item done or dropped leaves its number unused.

## Current deliveries

Manifest's Stage 5 closed on the engine's commit `13c1bec` (2026-11-25). Manifest now builds against
`70cd4de`, the docs review's head; the four gaps its Stage 5 cross-review found were provisioned
by `459af99` and the commit before it ([history](history.md)). The user plays its first loop in its
graphical client ([its L15](../../manifest/docs/delivery-order.md#l15)), not in the terminal UI,
which is mainly for administration (decided 2026-11-27); the engine work L15 stands on, the shell
started through a session, J30, is done. Encryption, J01, a script's sender,
J02, and dock control a game decides, J35, are done ([history](history.md)), and of the backlog
behaviour trees, J03, scripts, J04, packs and mods, J05, picking against the snapshots, J06,
overlap queries and all-hit casts of J07, steering, J08, the way in to a berth, J09, and the shell's
log file, J10. Manifest's adoption of encryption, its server's key in
a file and the public half in its test builds' client configuration, is Manifest's.

Deliveries placed by the user on 2026-11-27, each in its own lane: the server's pack, client-side
mods and the content check, J63, the device's capabilities, J36, and bindless textures, J68, from
the dense-geometry track, are done ([history](history.md)). J63 serves Manifest's graphical client
on a server ([its graphical client](../../manifest/docs/roadmap.md#the-graphical-client)).

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

<a id="j07"></a>
### J07. Physics

**What remains.**

| Feature | Why |
| --- | --- |
| Jolt's debug renderer drawn through debug draw | Seeing the shapes Jolt actually has. |
| Jolt's job system on more than one thread | Once a profile asks for it. The result is deterministic either way. |
| Capacities tuned against real traffic | The defaults are guesses. |

**What exists.** Shapes are Box, Sphere, Capsule, Cylinder, ConvexHull, Mesh and Compound, each
placed in its body by an offset and a turn; a mesh, or a compound with one, is for static and
kinematic bodies only (`source/engine/physics/include/jettison/physics/Shape.h`).
`PhysicsWorld::CastRay` and `CastShape` return the first hit, `CastRayAll` and `CastShapeAll` every
body, and `Overlap` and `OverlapSphere` every body a shape overlaps; a cast's hit names the part of
a compound it met (`CastHit::part`). A game's collision layers are `Physics::LayerDefinition`s,
loaded as definitions and defined with `PhysicsWorld::DefineLayers`; a body is put in one, and in a
group whose bodies never meet, by its `Body` (`layer`, `group`), and a cast or overlap is cast as a
layer (`CastOptions::layer`), a snapshot pick too (`PickOptions::layer`); steering keeps clear only
of what its vehicle's layer and group would meet. Jolt runs on
`JPH::JobSystemSingleThreaded` (`physics/src/PhysicsWorld.cpp`). The capacities are `maxBodies`
8192, `maxBodyPairs` 32768 and `maxContactConstraints` 16384 (`PhysicsWorld.h`).

**Feature doc:** [physics](physics.md).

## Tier 3: robustness and tools

Cheap to add when wanted, or waiting on a profile, and costing nothing meanwhile.

<a id="j11"></a>
### J11. Crash handling

**What remains.**

- A crash stack on Jolt's threads once it has them ([J07](#j07)).
- A report sent somewhere, which waits on players outside development.
- A minidump, should a report's frames and log tail not be enough.

**What exists.** `Platform::InstallCrashHandler` writes a report with its backtrace and each
module's build ID, named by a symboliser when one is found, on a stack of its own on the thread that
installs it and on each holding a `Platform::ThreadCrashStack` (the I/O worker, the scripts'
watchdog) (`source/engine/platform/src/Crash.cpp`); `scripts/symbolise-crash.sh` finds a stripped
release's debug files by build ID.

**Feature doc:** [runtime](runtime.md).

<a id="j12"></a>
### J12. A glTF model's collision mesh

**What remains.** A model's collision shape baked into a file of its own beside the model: the
model's own collision mesh where its author made one (its `collision-` nodes, [J66](#j66)), or else
convex hulls simplified from the drawn mesh. The file is world content, kept in the server's pack,
which holds no models, and read by physics as it is, so the client and the server collide with the
same bytes. The cook ([J37](#j37)) bakes it once it exists; until then a small tool does, so this
waits on none of the dense-geometry track.

**Why.** A vehicle drawn from a model collides as the box of its definition's size. Physics is the
server's, and the server reads no model, so the shape cannot be read from the glTF at run time
(decided 2026-11-27).

**What exists.** `Physics::ShapeKind::Mesh` and `ConvexHull` take vertices given in data or code
(`source/engine/physics/include/jettison/physics/Shape.h`). A platform built to a layout collides as
its decks, a mesh the docks make from the layout's boxes (`dock/src/Definitions.cpp`). A
`Dock::VehicleDefinition` has a length, beam and height, and an optional model drawn in place of its
proxy (`dock/Definitions.h`).

**Feature doc:** [physics](physics.md).

<a id="j14"></a>
### J14. UI

**What remains.**

- A document's images decoded on the I/O worker. They are decoded on the main thread as the document
  lays out, which is fine for a screen's icons.

**Why.** Decoding off the main thread matters once a screen shows large images.

**Feature doc:** [UI, input and localisation](ui.md).

<a id="j15"></a>
### J15. Localisation

**What remains.**

- Right-to-left and shaped scripts. RmlUi can lay them out with HarfBuzz; the fonts shipped cannot
  draw them.
- The player's language chosen at the first start from Steam's, once the shell has Steam; the
  environment's locale is read today.

**What exists.** `Ui::StringTable` with `Format`, `Plural`, `Number`, `Decimal`, `Date` and
`Time`, plural categories per language, and attribute text translated as element text is
(`source/engine/ui/include/jettison/ui/StringTable.h`).

**Feature doc:** [UI, input and localisation](ui.md).

<a id="j16"></a>
### J16. A test of an acquire out of date twice

**What remains.** A test of the renderer's recreate loop when the swapchain is out of date, and of
its failure after `kMaxRecreates` (8) tries (`source/engine/render/src/vulkan/Renderer.cpp`).

**Why.** That branch is not covered: it needs fault injection under the Vulkan layer, which the
engine lacks. Taken up when the branch is needed, if ever.

**Feature doc:** [rendering](rendering.md).

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

<a id="j61"></a>
### J61. Flown in under a low cover from any side

**What remains.** A vehicle flown in to a covered berth with the least headroom its class allows
lands from any start touching nothing but its berth. The user finds the cause and its fix by flying
the approaches in the GUI; it is done when they have.

**Why.** The J02 to J09 cross-review (2026-11-27) flew the `dock` suite's long, low vehicle under a
low roof (`source/engine/dock/tests/ApproachTests.cpp`) from other starts, in the platform's axes.
From 400 m up over the +X, +Z corner, the test's start, it lands touching nothing. From 400 m up
over the two −Z corners, and 150 m out along +X, it touches the deck beside its berth. From over
the middle it meets the cover's underside, and from 20 m up 300 m out along −Z it holds until its
reservation lapses. Highport's 48 landings touch nothing, so no Manifest task meets it yet.

**What exists.** The course among the decks and piers, and the shape sweep that slows a vehicle
short of what it meets and holds it between a deck and a roof
(`source/engine/ai/src/Steering.cpp`, [steering](ai.md#steering)).

**Feature doc:** [docks](docks.md#a-covered-berth).

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

- The rover is a box sliding on the ground's friction, which needed 8 rad/s² to turn and drifts
  sideways, not a vehicle that grips (`source/samples/ground/src/Ground.cpp`).

Nothing stands on it.

**Feature doc:** [samples](sample-game.md#the-ground-sample).

## A separate track: dense geometry and ray tracing

Meshlets drawn by mesh shaders, an asset cook, streaming, and ray-traced shadows, ambient occlusion
and reflections, with RTX Mega Geometry on NVIDIA: the plan agreed on 2026-11-25, for Manifest's
station kits, instance fields and asteroids. It is scheduled apart from the backlog's tiers until
the user places it; J36 and J68 were placed on 2026-11-27 ([current deliveries](#current-deliveries)), and both are built. Its design, decisions, budgets, risks and each
stage's exit criterion are in [dense geometry and ray tracing](dense-geometry-and-ray-tracing.md#stages);
the rows below say what each stage is, what it waits on, and which backlog items it touches.

| Stage | What it is | Needs | Touches |
| --- | --- | --- | --- |
| <a id="j37"></a>J37. The geometry module, the cook, and levels of detail for model files | meshoptimizer added; the CPU module `geometry`; `jettison_cook` cooking models and textures once, with layered settings files and a cook record over each source's references; the cooked GLB's reader for models, rigs and KTX 2 textures; the engine's content cooked at build time, dense test meshes generated; levels of detail and hysteresis for model files; `SetLodThreshold` on `IRenderer`. | J36 | Takes [J27](#j27)'s level-of-detail row; leaves [J12](#j12) small |
| <a id="j38"></a>J38. The cook's watch mode | `jettison_cook --watch`: the artists' daemon, cooking a source tree outside the game's repository into its asset folder as files change. | J37 | — |
| <a id="j39"></a>J39. The meshlet hierarchy, cooked, and its page file | Each static mesh cooked into a meshlet hierarchy (`clusterlod.h`) and a page file beside its GLB; the meshlet size chosen by measurement. | J37 | — |
| <a id="j40"></a>J40. GPU culling and mesh-shader drawing | Instances and meshlets culled on the GPU, drawn by task and mesh shaders in the prepass, opaque pass and cascades, in a deterministic order; the indexed path the fallback. | J39 | [J28](#j28)'s frames recorded again |
| <a id="j41"></a>J41. Occlusion culling | Two passes against a depth pyramid; scope says In. | J40 | — |
| <a id="j42"></a>J42. Ray-tracing foundation | Bottom-level structures per model from one cooked level, the TLAS each frame in render space, the build pass, refits from [J51](#j51)'s deform pass, hits' materials read through [J68](history.md#j68)'s texture table. | J37, J51, J68 | — |
| <a id="j43"></a>J43. Streaming | Meshlet pages read as the view needs them, through ranged VFS reads, into a pool of fixed-size pages; the coarsest levels always loaded; one memory budget over the GPU's geometry, with an order for giving way when it cannot hold. | J40 | Changes the model cache, whose prefetch a scene's load holds for ([J13](history.md#j13)) |
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

## A separate track: the art standards' support

What Jettison builds so Manifest's assets can follow its [art
standards](../../manifest/docs/art-standards.md), which Manifest defines and the engine's test
content follows too (decided 2026-11-27). Each stands on the cook, [J37](#j37), and is scheduled
with the dense-geometry track until the user places it; none is started.

<a id="j64"></a>
### J64. Texture coordinate sets by role, fetched by vertex pulling

**What remains.** The shaders fetch vertex attributes from per-mesh streams by buffer address
(vertex pulling), not through a fixed vertex layout: one stream a texture-coordinate set, up to
eight a mesh, each quantised by the cook to 16 bits over its own range. An asset's cook settings
name its sets by role in their order (`uvSets: [unique, trim, decal]`), and a material's texture
slot names the role it reads (`uv: trim`, `unique` when unset). The material cache resolves roles to
set indices when a material binds to a mesh, so a shader fetches only the sets its slots read. The
cook refuses a mesh with more than one set and no `uvSets`, a mesh with more than eight sets, and a
material reading a role its mesh lacks, each error naming the files. Vertex colour sets (`COLOR_n`,
`colorSets`) go the same way. The indexed path, the mesh shaders ([J40](#j40)), the deform pass
([J51](#j51)) and ray-traced hit shading ([J47](#j47)) read the same streams.

**Why.** Some of Manifest's assets need six sets, and a library material is shared by assets whose
sets differ in number and order ([texture coordinate
sets](../../manifest/docs/art-standards.md#texture-coordinate-sets)). A fixed vertex holding eight
sets pays them on every vertex, in memory the meshlets, ray-tracing levels and deformed instances
all hold; a shader variant per count of sets multiplies the pipelines and splits a shared material
in two. Mesh shaders and hit shading pull vertices by address anyway, so one path serves all three
([decisions](decisions.md#rendering)). A material's sixteen slots ([J68](history.md#j68)) can read all
eight.

**What exists.** `Render::Vertex` has one `texCoord` and one `color`
(`source/engine/render/include/jettison/render/Mesh.h`), bound as fixed attributes
(`render/src/vulkan/Model.cpp`); the glTF loader reads `TEXCOORD_0` alone
(`render/src/vulkan/ModelGltf.cpp`). Triplanar programs need no set.

**Feature doc:** [rendering](rendering.md#models-and-textures).

<a id="j65"></a>
### J65. The material library

**What remains.** A glTF material's name binds the library material of that name: the cook finds
`<name>.material.yaml` under `materials/`, refuses a name found twice or not at all (naming the
model and the name), and writes the material's virtual path into the cooked GLB's material extras,
which the model loader reads. A mesh component's `material` still overrides it. The glTF's own
material values are not read. An uncooked GLB keeps today's behaviour, its base-colour image
through `mesh`, with a warning naming the cook.

**Why.** A shared material, a trim sheet's, is changed in one file for every asset that uses it,
and a model's look is data a mod can shadow ([decisions](decisions.md#rendering)). It takes
[J27](#j27)'s row for a glTF model's own factors and textures, which the library replaces.

**What exists.** `MeshComponent` names a material file; a model without one draws its own
base-colour texture through `mesh` ([materials and programs](rendering.md#materials-and-programs)).

**Feature doc:** [rendering](rendering.md#materials-and-programs).

<a id="j66"></a>
### J66. The cook reads the standard's names

**What remains.**

- **Map words.** A texture's colour space from its map word (`color` and `emissive` sRGB, the rest
  linear), not from a `.linear.png` name; a map word the standard lacks is refused, naming the file.
  16-bit PNG sources for `normal` and `height`.
- **`orm`.** The `orm` slot's red channel read as occlusion in `mesh`, beneath ray-traced and
  screen-space occlusion (the slot was renamed from `metallicRoughness` on 2026-11-27). A `mask`
  slot for programs that take one.
- **Material extensions.** A glTF source using a material extension is refused, naming it.
- **Role nodes.** `collision-` nodes kept out of the drawn mesh and handed to [J12](#j12)'s
  collision file; `socket-` nodes kept in the cooked GLB, their transforms found by name on the
  model.

**Why.** The [art standards](../../manifest/docs/art-standards.md#names) name maps and nodes by
role, so the source tree carries no engine-specific suffixes and the cook checks what an artist
could get wrong.

**What exists.** The [content-file rule](coding-standards.md#content-files)'s map words; the
development PNG loader's `.linear.png` ([models and textures](rendering.md#models-and-textures)).

**Feature doc:** [rendering](rendering.md#models-and-textures).

<a id="j67"></a>
### J67. The test content in the standard's layout

**What remains.** `source/test/content` laid out and named as the [art
standards](../../manifest/docs/art-standards.md#files) have it: materials in class folders
(`materials/<class>/<name>.material.yaml`), maps by the standard's words. The [content-file
rule](coding-standards.md#content-files) becomes a pointer to the art standards, keeping the
engine's exceptions (shader programs, fonts). Needs [J66](#j66) for `orm`.

**Why.** One standard serves Manifest and the engine's own consumers, so the test content is the
standard's first example.

**What exists.** The test content's eleven materials, flat under `materials/`, and its textures
under `textures/<kind>/<asset>/`.

**Feature doc:** [samples and test consumers](sample-game.md).
