# Glossary

The one home of every short definition in Jettison's documents. Each term says what the word means
in the code and links the feature document that explains it. Other documents link a term's first use
here, as `[step](glossary.md#step)`, rather than define it again.

Terms are in alphabetical order. A word with several meanings (frame, gate, layer, platform,
session) has an entry per meaning, or one entry that tells them apart. The [documentation
index](README.md) lists every document.

### Acceleration structure

What a ray query traverses to find what a ray hits. A bottom-level structure (BLAS) holds a model's
triangles; the top-level structure (TLAS) holds the instances that place them, rebuilt each frame. A
CLAS, NVIDIA's cluster acceleration structure, holds one [meshlet](#meshlet), and a bottom-level
structure can be built from CLAS references. Planned: see [dense geometry and ray tracing](dense-geometry-and-ray-tracing.md).

### Action

An input the game asks about by name, such as "throttle" or "select", rather than a key or a button.
Bindings map device inputs to actions, and the player can rebind them, only the overrides saved
(`Input::InputSystem`, `input/Actions.h`). See [UI, input and localisation](ui.md) and [input
context](#input-context).

### Animation layer

One of the animations an entity plays at once, applied in order: a clip or a blend space, a weight
and a joint mask, so a face or an upper body plays over what is below. Not a scene's
[layer](#layer). Planned: see [characters](characters.md).

### Behaviour tree

A tree of nodes, defined as data (`Ai::BehaviourDefinition`), that runs an entity through a sequence
of tasks the game defines by name: undock, fly to a door, ask for a berth, dock. It is ticked once a
[step](#step), and each entity's place in it is saved. See [AI](ai.md#behaviour-trees).

### Berth

A place on a [platform](#platform) where one [vehicle](#vehicle) lands (`Dock::Berth`): an entity on
the platform's [rails](#rails), free, reserved or holding a vehicle, given by the `RequestBerth`
command. See [Docks](docks.md).

### Berth class

A definition of a size of berth (`Dock::BerthClassDefinition`, in `defs/berths/<name>.yaml`): the
largest vehicle that lands on one, the berth's width and length, and a rank. A vehicle names its own
class, and lands on a berth of the same rank or above whose largest vehicle holds it, the
lowest-ranked first. The sizes are the game's data. See
[Docks](docks.md#which-berth-a-vehicle-fits).

### Blend shape

A morph target of glTF: a mesh's alternative shape as deltas from its vertices, mixed in by a
weight, named (`jawOpen`) in the mesh's `targetNames`. Planned: see [characters](characters.md).

### Blend space

Clips placed at points on one or two axes (speed, or a velocity on the ground plane), blended by
where a parameter falls among them and kept in step on [sync markers](#sync-marker). Planned: see [characters](characters.md).

### Body

An entity in the physics world: one with a `Physics::Body`, a `Sim::Transform` and a `Sim::InFrame`.
A body is dynamic, kinematic or static, and may be a sensor, which meets nothing and reports what
enters it. Jolt holds the body itself; the component is what the save keeps. See
[Physics](physics.md).

### Catalogue entry

One of the 31 numbered briefs for the systems the engine's first phases built, such as "entry 22"
for physics, which code comments and older documents cite by number. Each, with what it became, is
in [history: catalogue](history/catalogue.md).

### Character controller

The physics body that walks a character: a capsule moved by a wanted velocity, up steps, along
slopes and on moving decks, over Jolt's `CharacterVirtual`. Not a [controller](#controller), who
sends commands. Planned: see [characters](characters.md).

### Clip

Keyframed animation of a rig's nodes or a scene's nodes (`Animation::Clip`): tracks of translation,
rotation or scale, and named events that presentation hears as playback crosses them. A glTF model's
clips move its rig; a clip file moves scene nodes. See [Animation and cinematics](animation.md).

### Colour grade

A 3D lookup table from a `.cube` file (`Render::ColourLut`), applied by the output pass after the
tonemap, and named by a scene's `Scene::ColourGradeComponent`. See [Transparency, particles and
post-processing](rendering-effects.md#colour-grading).

### Command

A request from [presentation](#presentation) to the [simulation](#simulation), such as "request a
berth": a reflected struct pushed on the `Sim::CommandQueue` and applied at the start of the next
[step](#step), in order. Being reflected, it can be saved, recorded for a [replay](#replay) and sent
over a network. See [Simulation](simulation.md#commands-controllers-and-the-helm).

### Component

Plain data attached to an [entity](#entity), stored by EnTT. A simulation component is registered
with Reflect and the world (`SimWorld::RegisterComponent`), and its `Saved` fields are all a
[save](#save) keeps of it. Presentation components (`scene/Components.h`) live in the
[scene](#scene)'s registry. See [Simulation](simulation.md).

### Console

The developer console (`DevTools::Console`): the log, commands and console variables bound through
reflection, opened in the running app. Its lines also drive tests and captures, as in
`--run "orbit 0.8 -0.1 2.0e7 ostra"`. Its text is English and never goes through the [string
tables](#string-table). See [UI, input and localisation](ui.md) and [The app shell and
tools](app-shell.md).

### Consumer

A program built on the engine through its public targets alone: `Jettison::Engine`, `Jettison::App`
or a module's. Manifest is the production consumer; the [ground sample](#ground-sample) (on
`Jettison::App`), the [test content](#test-content) and `source/test/consumer` (a CPU-only race
served over loopback, on `Jettison::Engine`) are the engine's own. See [Samples](sample-game.md) and
[game integration](game-integration.md#enforcement).

### Controller

Who sends a command (`Sim::Controller`): no one in particular (the game's own rules or a test), a
player by slot, or the simulation's own agents. An entity with `Sim::ControlledBy` takes commands
only from its controller or from no one in particular (`Sim::MayCommand`). See
[Simulation](simulation.md#commands-controllers-and-the-helm).

### Cook

What `jettison_cook` does to an artist's source file: a glTF or an image in the asset source tree,
outside the game's repository, made into what the engine reads (a GLB and its [meshlet](#meshlet)
page file, a KTX 2 texture) in the game's asset folder. A **cook settings file** beside a source
(`big-ship.gltf.yaml`) overrides the game's `cook.yaml` and the engine's defaults for that file.
Planned: see [dense geometry and ray tracing](dense-geometry-and-ray-tracing.md).

### Cross-review

An adversarial review by a second model (Codex) of a branch's commits, after the gate, run by
`scripts/cross-review.sh` when a stage of work closes. Each finding is answered with a fix or with
evidence, for at most three rounds, then the user settles what is open. See [build and
gates](build-and-gates.md#cross-review).

### Data model

A UI document's view of the game's data (`Ui::DataModel`): named variables the document reads, as in
`{{market.goods[0].ask}}`, and named events it raises. The variables are presentation's copies of
what [snapshots](#snapshot) carry, never the simulation's own state; an event usually becomes a
[command](#command). See [UI, input and localisation](ui.md).

### Definition

A typed record of game data loaded from YAML and overridable by mods: a vehicle, a berth class, a
script, a behaviour tree. Each is one file, named by its path under the type's directory without
`.yaml`: `defs/vehicles/military/corvette.yaml` is `military/corvette`. A [save](#save) refers to
definitions by name and holds none. See [Simulation](simulation.md#definitions).

### Definition registry

The store of every loaded [definition](#definition), by type and name (`Sim::DefinitionRegistry`),
read through the [mounts](#mount) with [patches](#patch) [layered](#layering) and
[migrations](#migration) run. It is filled at start-up and kept in ordered maps, so a walk over it
is the same every run. See [Simulation](simulation.md#definitions).

### Deform pass

The compute pass that applies blend shapes and then skinning to each animated instance, once a
frame, into vertices every later pass and the ray-tracing refits read. Planned: see [characters](characters.md).

### Depth pyramid

A chain of ever smaller copies of the depth buffer, each texel the farthest depth of the texels
under it, against which a whole meshlet or instance is tested for occlusion with a few reads; also
called a hierarchical Z-buffer (HZB). Planned: see [dense geometry and ray tracing](dense-geometry-and-ray-tracing.md).

### Destination

Where [steering](#steering) flies a vehicle (`Ai::Destination`): a point offset from an entity, the
waypoints on the way, and when it has arrived; set and cleared by commands. See
[AI](ai.md#steering).

### Dev tools

The developer's ImGui views (`source/engine/ui/imgui`, namespace `DevTools`): the
[console](#console) and the panels F1 opens. They are English-only, separate from the game's RmlUi
screens. See [UI, input and localisation](ui.md).

### Entity

An identifier in an EnTT registry, to which [components](#component) attach. The simulation's
entities live in `Sim::SimWorld`'s registry and the presentation's in the [scene](#scene)'s; the two
never share one. An identifier carries a version, so an index reused after a destroy is a different
entity. See [Simulation](simulation.md).

### Event

A plain struct the simulation raises for presentation (`Sim::SimEvent`), such as "docked" or
"contact", with the [step](#step) it was raised in. Events flow one way: presentation takes them all
once a [frame](#frame-rendering), in the order they were raised, and turns them into sound, effects
and UI. The event queue is not part of the saved state. See [Simulation](simulation.md).

### Fast-forward

Advancing the world many [steps](#step) at once (`SimWorld::FastForward`), to the bit the state the
steps would have left, when no command is queued and every system is [quiet](#quiet-step). See
[Simulation](simulation.md#quiet-steps-and-fast-forward).

### Fixed step

The simulation's constant time step, given to `Sim::SimWorld` as it is made (1/60 s in the shell).
Each frame runs a whole number of them, at most eight by default, which makes a run the same on
every machine. See [Simulation](simulation.md#time).

### Flight controls

What a pilot demands of a vehicle (`Physics::FlightControls`): a linear and an angular demand, and
whether flight assist is on. With assist on, the thrusters hold a velocity relative to a reference
entity; with it off, a demand is a share of the thrusters' acceleration. Only the one at the
vehicle's [helm](#helm), or the game's own rules, may set them. See [Physics](physics.md).

### Flotsam

The editor's entry point (`source/tools/flotsam`): the [shell](#shell) with no game composed into
it, on the shared renderer and assets. See [The app shell and tools](app-shell.md).

### Format version

The version number every file format carries (`Serialise::FormatVersion`). A reader that meets
another version runs the format's [migrations](#migration), or throws `FormatVersionError` naming
both; until player testing, formats change in place ([decided](decisions.md#persistence)). See
[Persistence](persistence.md#format-versions-and-migrations).

### Frame

An overloaded word. It means a [coordinate frame](#frame-coordinates), one pass of the [frame
loop](#frame-rendering), or a captured image compared against a [reference
frame](#reference-frame-image). [Frames in flight](#frame-in-flight) are GPU frames still being
drawn. The context decides which.

### Frame (coordinates)

A partition of the world with its own origin, in which positions are comparable: a level, a room, or
in Manifest a star system. An entity names its frame with `Sim::InFrame`, whose value is the frame's
name hashed (`Sim::FrameId("ground")`); an entity between frames is in none (`Sim::kNoFrame`).
Positions are `double` metres from the frame's origin. See [Coordinate frames and
units](coordinates.md).

### Frame (rendering)

One pass of the app's main loop (`Application::Tick`), which runs the six tick phases in order:
Input, Commands, Simulation (zero or more [steps](#step)), Sync, Presentation and Render. A frame's
length is the display's, while a step's is fixed. The Render phase records the pass list and
presents one image. See [Rendering](rendering.md) and [architecture](architecture.md).

### Frame in flight

A frame the CPU has submitted and the GPU may still be drawing. The renderer keeps two
(`kFramesInFlight`), each with its own per-frame buffers, and destroys a GPU resource only once no
frame in flight uses it. See [Rendering](rendering.md).

### Frame model

What a game tells physics about a [frame](#frame-coordinates) beyond its bodies
(`Physics::FrameModel`): where each named body of the game's is at a given time, and the frame's
gravity. [Rails](#rails) and [local frames](#local-frame) name those bodies. It is a definition, not
state, so it is neither saved nor hashed. See [Physics](physics.md).

### Fuel

The number of interpreter instructions one call into a [script](#script) may run before it traps: a
script definition's `fuel`, 100,000 by default. The count is the same on every run, so a script out
of fuel stops at the same place in every build. See [Scripts](scripts.md).

### Game

The code that composes a game onto the engine, as hooks that do nothing by default: `App::Game`
(`app/Game.h`) into the windowed [shell](#shell), as the ground sample's `GroundGame` does, or
`Host::Game` into a windowless [session](#session), as `source/test/consumer` and Manifest's server
do. See [The app shell and tools](app-shell.md#the-game-a-program-composes) and [the host
session](simulation.md#the-host-session).

### Gate (CI)

`scripts/ci.sh`, the full check a chunk of work passes before a push: the Markdown links,
formatting, the Debug build and its suites, clang-tidy, the Release and CPU-only builds, the [smoke
test](#smoke-test), the [reference frames](#reference-frame-image) and, with `--asan`, the sanitised
smoke test and suites. A pass is remembered by a digest of its inputs, so the same tree is not
checked twice. See [build and gates](build-and-gates.md#the-gate).

### Gate (transit)

A gated portal: a [slot](#slot) with a sensor, which lets its holder through and lets any other body
that touches the sensor pass unsent. Manifest's stargates are built on it. See
[Transit](transit.md).

### Grant

A capability a [script](#script)'s definition lists in `capabilities`: `log`, `store` (keep bytes by
key in the save), or `send:<Command>` for each command it may send. A module that imports something
it is not granted does not load. Because grants are definition fields, a mod changes them with a
[patch](#patch). See [Scripts](scripts.md).

### Ground sample

The engine's sample game (`source/samples/ground`): a vehicle driven on the ground under a frame
model's gravity, with no space in it, and a small regression [consumer](#consumer) of the public
API. See [Samples](sample-game.md#the-ground-sample).

### Headless

Run with no desktop session: on a virtual X server (Xvfb) from the `clang-headless`
[preset](#preset), or [offscreen](#offscreen). The reference frames run on [lavapipe](#lavapipe)
under Xvfb; the gate's smoke test runs offscreen on the GPU, or on lavapipe under Xvfb with
`--llvmpipe` or no `/dev/dri`. See [build and gates](build-and-gates.md#headless-render-test).

### Helm

Who is flying a vehicle by its controls (`Sim::Helm`). Taking the helm (`Ai::TakeHelm`) makes the
vehicle's [flight controls](#flight-controls) that controller's, or no one in particular's, and
stands its behaviour tree down; leaving it (`Ai::LeaveHelm`) lets the controls go and restarts the
tree. A controller is at one helm at a time. See [AI](ai.md#the-helm).

### Host

The `host` module: a game's file system, definitions and world composed with no window, through a
[session](#session), a [game](#game) and its [packages](#package). It is what a server or a tool
runs. Not to be confused with the script host (`Script::ScriptHost`), which runs [scripts](#script).
See [Simulation](simulation.md).

### Hot reload

Replacing a loaded resource when its file changes, without restarting: in development builds the
resource manager polls the files it loaded and reloads one that changed, or that another mount now
shadows, keeping the handle and raising the resource's revision. See [Files, packs and
mods](files-and-mods.md).

### I/O worker

The one background thread that reads files and decodes assets for the resource manager
(`Jobs::IoWorker`). Its completions run on the main thread, once a frame, so nothing it finishes
touches engine state from another thread. The general job system is deferred. See [Runtime: core and
platform](runtime.md).

### Input context

A named set of [action](#action) bindings, such as flight, map or console, pushed on and popped off
a stack. A context higher on the stack claims the inputs it binds, so the ones below do not see
them; an opaque context, as the console's is, hides everything below. See [UI, input and
localisation](ui.md).

### Lavapipe

Mesa's software Vulkan driver, built on llvmpipe. The reference frames and the sanitised runs always
use it under Xvfb, and the smoke test does with `--llvmpipe` or no `/dev/dri`; it needs no GPU and
gives the same pixels every run. `scripts/lib/headless.sh` finds its driver manifest. See [build and
gates](build-and-gates.md#headless-render-test).

### Layer

In a [scene](#scene), one of three content layers an entity belongs to (`Scene::Layer`): Environment
(what the scene file places), Traffic (what the simulation places) and Effects (markers and
transient visuals). A hidden layer's meshes are not drawn and its lights light nothing, but the sun
and the sky ignore layers ([Scenes](scene.md#layers)). The [VFS](files-and-mods.md) also calls each
[mount](#mount) a layer; for that sense see [layering](#layering).

### Layering

How several [mounts](#mount) combine into one view of the files and definitions. For a plain file,
the topmost mount that has the path wins. For a [definition](#definition), a whole file replaces the
one below, a [patch](#patch) changes only the fields it names, and a new path adds a definition;
a scene layers the same way, a stylesheet's patch is appended to it, and string tables merge key by
key. See [Files, packs and mods](files-and-mods.md).

### Local frame

The frame of reference of one named body inside a [frame](#frame-coordinates), such as a platform or
a moving deck, moving with the body but not turning (`Physics::LocalFrame`). Each local frame with
bodies is a Jolt world of its own. See [Coordinate frames and units](coordinates.md#local-frames)
and, for why, [Physics](physics.md#why-local-frames).

### Manifest

The engine's first production game, in the sibling repository `../manifest`: a trade simulation set
in space. It owns its gameplay, its content and every space feature; the engine provides neutral
capabilities it builds on. See [game integration](game-integration.md).

### Manifest task ID

A row of Manifest's delivery order, a letter and a number such as `W05` or `M04`, naming one piece
of Manifest's work. The engine's documents cite one only with a link to the row in [Manifest's
delivery order](../../manifest/docs/delivery-order.md), which is the authority.

### Meshlet

A small cluster of a mesh's triangles (up to a few hundred), with its bounds, normal cone and error,
culled and drawn as a unit by task and mesh shaders. Not a cluster: that word is the light grid's
([rendering](rendering.md#clustered-lights)). Planned: see [dense geometry and ray tracing](dense-geometry-and-ray-tracing.md).

### Meshlet hierarchy

A model's [meshlets](#meshlet) at every level of detail, each group simplified from the one below
with its border locked, so any cut through it is crack-free and each meshlet decides alone whether
it is drawn. Cooked into a page file beside the model, read in pages as the view needs them.
Planned: see [dense geometry and ray tracing](dense-geometry-and-ray-tracing.md).

### Migration

A step that brings data of one format version up to the next (`Serialise::Migrations`). A chain of
steps reads data of any old version a step at a time, so a format change is one new step rather than
a change to every reader. Saves migrate as binary documents, and definitions as YAML. See
[Persistence](persistence.md#format-versions-and-migrations).

### Mod

A player-installed [package](#package): a directory, or a [pack](#pack) of one, under the user's
`mods/` directory, mounted above the game's content as `mod:<identifier>`. The player's list,
`mods.yaml`, gives their order and which are enabled. A mod changes the game by
[layering](#layering): it shadows files, [patches](#patch) definitions and adds scripts. A mod that
cannot be mounted is reported and left out. See [Files, packs and mods](files-and-mods.md).

### Motion profile

How a [passage](#passage) covers its way when it is not eased (`Transit::MotionProfile`): a boost
acceleration for a limited time, a sustained acceleration between, turning at the midpoint, at rest
at both ends. An optional thrust share makes it coast in the middle. A game uses it to time a lift
or price a long journey's burns. See [Transit](transit.md).

### Mount

One source in the virtual file system (`Vfs::Mount`): a directory or a pack mapped under a name.
Mounts are stacked at start-up, bottom to top: `engine`, `base` (the game's content), each
`mod:<identifier>`, then `user`, the only writable one, where saves go; settings are in the config
directory, outside the mounts. A later mount shadows an earlier one path by path. See [Files, packs
and mods](files-and-mods.md).

### Neutral

Free of any one game's setting. The engine names its capabilities in words any game would use, such
as platform, vehicle, berth and portal, rather than the space setting's station, ship, pad and jump
gate. The `neutral_words` test refuses the space setting's words in the engine's code. See [game
integration](game-integration.md).

### Offscreen

Drawing with no display, window or surface (`render_test --offscreen`). Outside the sandbox it draws
on a real GPU, and `--screenshot` writes the frame. See [headless](#headless) and [build and
gates](build-and-gates.md#headless-render-test).

### Pack

A directory's files in one file (`.pack`, header `JETPACK`), for shipping, mounted exactly as the
directory would be. Each file is stored as it is or compressed as one zstd frame, whose checksum is
checked on read. `jettison_pack` (`source/tools/pack`) makes one. See [Files, packs and
mods](files-and-mods.md#packs).

### Package

A content source as mounted, a directory or a pack, described by the `mod.yaml` at its root
(`Host::PackageDescription`): its name, release, oldest engine and requirements. The game's content
and every [mod](#mod) are packages. See [Files, packs and
mods](files-and-mods.md#packages-and-modyaml).

### Pass list

The [frame](#frame-rendering) as a declared list of render passes (`Render::PassList`). Each pass
names the resources it touches and how, and the barriers between passes are derived from that once,
at start-up. Compute passes sit beside graphics passes on the same terms. See
[Rendering](rendering.md).

### Passage

A journey out of physics (`Transit::Passage`): a lift's car up its shaft, a train along its line, a
vehicle carried across a frame faster than it could fly. The entity leaves physics with its body
kept, is placed each step along a way the game gives, and comes out at rest, clear of what is there.
See [Transit](transit.md).

### Patch

A file beside a [definition](#definition), a scene or a stylesheet that changes only what it names:
`military/corvette.patch.yaml` over `military/corvette.yaml`. Two mods can then each change one
definition without copying it, and a patch may add to a list or take elements out of it. See [Files, packs and mods](files-and-mods.md#layering-definitions).

### Phase label

A label such as "9c": phase 9, lettered sub-phase c, one block of the engine's early development
(phases 0 to 13, finished 2026-11-14). Code comments, commits and older documents use them. Each
row, its work and its exit criterion are recorded in [history: phases](history/phases.md); a
document links that row rather than citing the bare label.

### Picking

Finding what is under the mouse: presentation sends a ray as a `Physics::Pick` command, and physics
casts it as the next step begins and answers with a `Picked` event, so presentation never reads the
simulation's state. See [Physics](physics.md#casts-and-picking).

### Pin

A value fixed in a test or a build so that any change to it is a decision, not drift. A pinned state
hash (`CHECK(first.StateHash() == 0x2e0a15d18e042773ull)` in `source/engine/sim/tests/SimTests.cpp`)
fails when a world's results or save layout change; a dependency is pinned to a tag or a commit, and
Manifest pins the Jettison commit it builds against. See [build and
gates](build-and-gates.md#the-gate) and the [coding standards](coding-standards.md#dependencies).

### Platform

An overloaded word. In the docks, a platform is a structure built to a layout, with berths for
vehicles and a queue for them (`Dock::Platform`, in `defs/platforms/<name>.yaml`): in Manifest, a
station ([Docks](docks.md)). The `platform` module is the engine's operating-system layer: user
paths, the CPU check and crash reports ([Runtime: core and platform](runtime.md)).

### Portal

A way from one place to another that takes no path through space (`Transit::InPortal`): a door
between levels, a teleporter, a jump between frames. The entity leaves its frame for a set time, in
no frame meanwhile, and comes out at a named exit the game places, in any frame. See
[Transit](transit.md).

### Presentation

Everything that shows the world to the player: the [scene](#scene), cameras, animation, audio, UI
and rendering. It reads [snapshots](#snapshot) and [events](#event) and sends [commands](#command);
it never holds a reference into the simulation. Its state is not saved and need not be
deterministic. See [Simulation](simulation.md) and [architecture](architecture.md).

### Preset

A named CMake configuration in `CMakePresets.json`, such as `clang-debug`; build with
`cmake --build --preset clang-debug`. The five are in [build and gates](build-and-gates.md#presets).

### Proxy

A presentation entity that shows a simulation entity (`Scene::SimProxyComponent`). It holds the
simulation entity's identifier, never a reference into the world, and each frame the sync phase
writes the entity's interpolated transform into it. The game makes its proxies, as `GroundGame`
does. See [Scenes](scene.md#proxies-of-the-simulation).

### Quiet step

A step in which a system, with no command applied, would write nothing, or only what a formula of
the clock gives. A system declares this with `Sim::Quiet`: the next step it must run in, and how to
write the passed-over steps' result at once. When every system is quiet, the world can
[fast-forward](#fast-forward). See [Simulation](simulation.md).

### Rails

An entity's place set by the game's rules rather than by forces: each step, `Physics::OnRails` puts
the entity where a named body of its frame's [frame model](#frame-model) is at the step's end,
turned as that body is, at an optional offset. A berth on a platform's deck is on rails, and in
Manifest so are planets and the stations they carry. See [Physics](physics.md).

### Reason

Why the simulation refused or gave something up, as an event carries it (`Sim::Reason`): a
string-table key such as `reason.dock.no_berth_fits` and positional arguments. The player reads it
in their language; the step itself writes no English. A module's reasons are an enumeration
registered as `<Module>.Reason`. See [Simulation](simulation.md).

### Reference frame (image)

A committed PNG of a view of `render_test` (`source/test/frames`, listed in `views.txt`), which the
gate captures again and compares. Not a [coordinate frame](#frame-coordinates). See [build and
gates](build-and-gates.md#reference-frames).

### Reflection

The engine's type tables over EnTT's meta system (`Reflect`): types, fields and enumerations by
name, with attributes. `Saved` marks what archives write; `Moddable`, `Editable`, `Console` and
`Script` mark what mods, the editor, the console and scripts may see. Saves, definitions, commands,
events and data models all work through it. See [Persistence](persistence.md).

### Render space

The coordinates the GPU sees: the [frame](#frame-coordinates) with the camera at its origin, in
`float`. See [Coordinate frames and units](coordinates.md#render-space).

### Render test

`render_test` (`source/test`): the [shell](#shell) with no game, over the [test
content](#test-content)'s static scenes. The smoke test, the reference frames and
`scripts/capture.sh` run it. See [The app shell and tools](app-shell.md).

### Replay

Running a recorded session again: the [commands](#command) applied, each with its step, fed back to
a world loaded from the same start give the same states, to the bit. See
[Simulation](simulation.md#saving-hashing-and-replay).

### Resource handle

A typed handle to a loaded resource (`Resource::ResourceHandle<T>`): an index into the manager's
table and the slot's generation. A handle whose generation no longer matches is stale, which is how
a cache learns a resource was released. See [Files, packs and mods](files-and-mods.md).

### Rig

A model's node hierarchy, the joints of its skin and the clips that move it (`Animation::Rig`),
loaded from glTF and kept apart from the renderer. See [Animation and cinematics](animation.md).

### Save

One file holding a game in progress (`Sim::SaveGame`, format `Jettison.Save`): its info (for a list
of saves), the world's state, and the game's [sections](#section). Each part keeps its own [format
version](#format-version) and migrations. It is written whole and durably to the user mount, so a
crash mid-write leaves the previous save. See [Persistence](persistence.md).

### Scene

Presentation's world (`Scene::Scene`): an EnTT registry of presentation components, loaded from and
saved to a YAML scene file. Everything it draws is named by virtual path or procedural shape, so a
mod can shadow any of it. It shows the simulation through [proxies](#proxy). See [Scenes](scene.md).

### Script

Mod behaviour as a WebAssembly module, run by the WAMR interpreter (`Script::ScriptHost`). A script
is a [definition](#definition) in `defs/scripts/<name>.yaml` naming its module, its [fuel](#fuel),
its memory, its [grants](#grant), and the exports that handle events and answer AI inputs. It acts
on the world only through granted commands. See [Scripts](scripts.md).

### Section

A game's own state in a [save](#save), beyond the world's (`Sim::SaveSection`): a binary archive in
the game's format, with its own version and migrations. The save holds it and does not read it. The
script host keeps scripts' stored keys in a section of its own. See [Persistence](persistence.md).

### Sequence

A cinematic (`Scene::Sequence`, `cinematics/<name>.sequence.yaml`): camera shots on rails, cut or
blended, with a fade, letterbox bars and events on one timeline. It is presentation alone and hands
the camera back to the game's rigs at the end. The console's `sequence` command plays one. See
[Animation and cinematics](animation.md).

### Session

An overloaded word. A host session (`Host::Session`) owns one game's file system, definitions and
world, made in start-up phases ([the host session](simulation.md#the-host-session)). A net session
(`Net::SessionId`) is one admitted connection on a server ([Networking](networking.md)).

### Shell

The engine's windowed application (`RunRenderApplication`, `App::Shell`): the window, renderer,
input, UI, scene and frame loop. It starts a game's world, its `Host::Game`, through a session, and
composes the game's `App::Game` over it. With no game it is
`render_test` or `flotsam`. A game reaches it through the `Shell` interface its hooks are given. See
[The app shell and tools](app-shell.md).

### Simulation

The authoritative world (`Sim::SimWorld`): its own registry, advanced in [fixed steps](#fixed-step)
on simulation time, deterministic across runs and across Debug and Release. Everything a save
contains lives here. It takes [commands](#command) in and gives [snapshots](#snapshot) and
[events](#event) out. See [Simulation](simulation.md).

### Slice

One of the eight steps of the space extraction (finished 2026-11-23), which moved the space game's
code from the engine into Manifest and renamed what stayed in neutral words. Code comments cite them
as "slice 7". See [history: space extraction](history/space-extraction.md).

### Slot

Something one entity holds at a time, with a first-come queue of entities waiting (`Transit::Slot`):
a car park's bay, a toll booth's lane. A hold lapses at a set step unless given up. What a hold asks
of its holder is the game's. See [Transit](transit.md).

### Smoke test

`scripts/smoke-test.sh`: runs `render_test --smoke-test`, then the ground sample's, through a
scripted sequence of what has broken the engine before, with Vulkan validation on. A stage of the
gate, it runs with no desktop: offscreen on the GPU, or on lavapipe under Xvfb. See [build and
gates](build-and-gates.md#headless-render-test).

### Snapshot

The part of the world presentation may see, captured after a step (`Sim::Snapshot`): every entity's
transform and frame, and copies of the components the world shows (`SimWorld::ShowInSnapshots`).
Presentation interpolates between the last two, so it shows the world a step behind real time. Not
the network's snapshot, a game-encoded view sent to a client ([Networking](networking.md)). See
[Simulation](simulation.md#snapshots-and-the-stepper).

### Spatial index

An octree per [frame](#frame-coordinates) over the simulation's entities (`Spatial::FrameIndices`),
for nearest, within-range and volume queries, rebuilt each step and not saved. See
[Scenes](scene.md#the-spatial-index).

### State hash

A hash of a world's save (`SimWorld::StateHash`). The same seed and command stream give the same
hash in every build, so a test [pins](#pin) it to catch any change to the simulation's results or
its save layout. See [Simulation](simulation.md).

### Steering

Local flight for agents (`ai/Steering.h`): behaviours such as arrive and avoidance, each a velocity,
whose sum flight assist holds, flying a vehicle to its [destination](#destination). See
[AI](ai.md#steering).

### Step

One advance of the simulation by the [fixed step](#fixed-step) (`SimWorld::Step`): apply the queued
commands in order, run the simulation-phase systems, advance the clock. A [frame](#frame-rendering)
runs zero or more steps; a step is the unit of saved time, counted by `Time::SimClock::Step`. See
[Simulation](simulation.md).

### String table

The text a player reads, by key, in their language (`Ui::StringTable`): YAML under
`strings/<language>/`, nested keys joined by dots, as `market.buy`. Tables merge across mounts key
by key, and a key a translation lacks reads the English base. Developer text (the log, the console)
stays English. See [UI, input and localisation](ui.md).

### Sync marker

A clip event the engine itself reads (`foot.left`, `foot.right`), which the cook finds from a
clip's foot contacts, so clips blended together keep their steps in phase. Planned: see [characters](characters.md).

### System

A named function run once per phase of a frame, or once per step for the simulation phase
(`Ecs::SystemDescription`). It declares the components it reads and writes, and its order by `after`
and `before`; otherwise registration order decides. Simulation systems are registered with the world
and may declare a [quiet](#quiet-step). See [Simulation](simulation.md).

### Test content

The engine's own test assets (`source/test/content`): scenes, models, textures, audio, grades and
cinematics, mounted as `base` by `render_test` and read by the suites. See
[Samples](sample-game.md).

### Tick

One [frame](#frame-rendering), whose six phases are the tick phases (`Ecs::TickPhase`); not a
simulation [step](#step). A behaviour tree is also "ticked", once a step.

### Token

A bearer token for a network client: 32 random bytes as 64 hex characters (`Net::GenerateToken`).
The server keeps only its BLAKE2b digest (`Net::DigestToken`, `Net::TokenMatches`), so a leaked
store gives no token away. See [Networking](networking.md#tokens).

### Trap

A call into a [script](#script) that fails: out of fuel, out of bounds, an unreachable instruction,
or a host call refused. The call's effects stop where it trapped and the trap is logged; the script
stays loaded. See [Scripts](scripts.md).

### Utility AI

Choosing by score rather than by sequence (`ai/UtilityAi.h`): a decision (`Ai::DecisionDefinition`)
is data, actions scored by considerations, and `Ai::Choose` picks among the options the game offers.
See [AI](ai.md#utility-ai).

### Vehicle

A craft a pilot or an agent flies. In physics, a dynamic body with `Physics::Handling` and [flight
controls](#flight-controls), such as the ground sample's rover
([Physics](physics.md#the-flight-model)). In the docks, an entity with `Dock::Vehicle`, naming its
definition (`Dock::VehicleDefinition`, in `defs/vehicles/<name>.yaml`): its size and its [berth
class](#berth-class). Such a vehicle collides as the box of its size, whatever its model
([Docks](docks.md)).

### Virtual path

A case-sensitive path relative to every mount's root, with forward slashes, such as
`models/arm.glb`, which cannot name a file outside a mount. See [Files, packs and
mods](files-and-mods.md#virtual-paths).

### Watchdog

The script host's one extra thread, which stops a call into a script that runs past the host's
wall-time limit: fuel bounds what a script does, the watchdog what fuel cannot see, such as a
stalled machine. See [Scripts](scripts.md#limits-fuel-memory-and-time).

### World service

State the simulation keeps outside its registry (`Sim::WorldService`): physics' bodies and contacts,
held by Jolt. A service is saved and hashed beside the components, so a world loaded from a save
carries on as the saved one did. See [Simulation](simulation.md).
