# System catalogue, entries 1–31

The briefs the engine's systems were built to in phases 1 to 13 (2026-11-10 to 2026-11-14), each
with what it became ("Filled in") and later notes dated where they were added. Entry numbers are the
old roadmap's, which the [phases](phases.md) table, commits and older documents cite as "entry 3";
the current backlog in the [roadmap](../roadmap.md) is numbered J01 to J29 instead. Each system's
current design is in its feature document, listed in the [documentation index](../README.md).

## 1. Build, layout and packaging · phase 1

`jettison_add_module(<name> DEPENDS ...)` creates `jettison_<name>`, the `Jettison::<Name>`
alias, the include layout, the private `jettison_warnings` link and the tests directory.
It exports the CMake package for the game repository.
*Stub*: the helper, `Jettison::Engine`, `cmake/JettisonConfig.cmake.in`.

## 2. Testing · phase 1

doctest, one test executable per module, registered with CTest and run by `scripts/ci.sh`
before the smoke test. The existing `smoke_sequence_test` becomes a doctest case.
*Library*: doctest.

## 3. Core · phases 1–2 · `engine/core`

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

## 4. Math · phase 2 · `engine/math`

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

## 5. Profiling · phase 2 · `engine/profile`

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

## 6. Platform · phases 1–2 · `engine/platform`

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

## 7. Time · phase 2 · `engine/time`

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

## 8. Reflection · phase 3 · `engine/reflect`

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

## 9. Serialisation · phase 3 · `engine/serialise`

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
[files, packs and mods](../files-and-mods.md). A YAML migration step edits only what
the data has, since a patch leaves out what it does not change.

## 10. Virtual file system · phase 3 · `engine/vfs`

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
before the rename and the directory after it ([decided](../decisions.md)). The shell mounts the
user's data directory writable over the base, and saves go there.
*Filled in* (11a): a mount is a directory or a pack file (`vfs/Pack.h`, zstd), behind the same
calls; `Find` (a file's mount, time and size) replaces `Resolve`, since a file in a pack has no host
path, and `ReadLayers` reads every mount's file at a path. The shell mounts the user's mods between
the base and the user's directory. See [files, packs and mods](../files-and-mods.md).

## 11. Resource manager · phase 3 · `engine/resource`

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

## 12. ECS, scheduler, events and FSM · phase 3 · `engine/ecs`

The tick phases (see [architecture](../architecture.md#frame-and-tick-structure)), system
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

## 13. Simulation · phase 3 · `engine/sim`

`SimWorld` (its own registry, fixed step, deterministic), `CommandQueue`, snapshots to
presentation, and the definition registry: typed, reflected definitions loaded from data and
overridable by mods. Determinism rules are in the
[coding standards](../coding-standards.md#simulation-code).
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
*Filled in* (8a): definitions migrate ([decided](../decisions.md)): `Load` takes a type's YAML
migrations, and a file of an earlier version is brought up to the current one as it is read. The
world's save migrates from version 1 (before its services), and a save game (`sim/SaveGame.h`) holds
the world beside its info and the game's sections.
*Filled in* (11a): definitions layer finer than a file: a `.patch.yaml` beside a definition merges
over the layers below it, a whole file still replacing them, and each change a mod makes is logged
([decided](../decisions.md)).

## 14. Input · phase 3 · `engine/input`

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
follows its device, named by GUID (`input/Joysticks.h`, [decided](../decisions.md)), and
`SaveBindings` takes out of the settings an action reset to its defaults, which before stayed in them
and came back on the next load.

## 15. Console · phase 3 · `engine/ui/imgui`

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

## 16. Large-world coordinates and celestial · phase 4 · `engine/celestial`

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

## 17. Render foundations · phase 4 · `engine/render`

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
([architecture](../rendering.md#why-clustered-forward)) stands on, so phase 5 adds passes
rather than restructuring. Compute passes are nodes in the pass list like graphics passes, with
the barriers between them declared. The HDR colour target is multisampled and resolved before
post-processing. Storage buffers are allocated per frame in flight, for the light list and the
cluster data phase 5 writes.

### Shaders · phase 4

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

## 18. Cameras · phase 4 · `engine/render`

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

## 19. Space rendering · phase 5 · `engine/render`

Clustered forward shading, as [architecture](../rendering.md#why-clustered-forward)
describes. PBR, image-based lighting, the sky and tonemapping were done in 5b, the clustered
lights in 5c, the sun's cascaded shadows in 5d, and culling, instancing, levels of detail and the
frame's budget in 5e. Point and spot lights have finite ranges with windowed inverse-square
falloff, and are assigned to a grid of view-space clusters by compute: 32 × 18 × 48, tuned in 5e
against a station of 265 lights, and covering the cluster range. The directional sun is separate,
in the frame constants, with cascaded shadows. The opaque depth prepass was measured in 5e, and is
kept and off ([decided](../decisions.md)). Also in this phase: PBR (glTF metallic-roughness) with image-based lighting
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

## Shaders · phase 5

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

## 20. Geometry and textures · phase 5 · `engine/render`

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

## 21. Scene management and spatial index · phase 6 · `engine/scene`, `engine/spatial`

A scene per star system, built from the system description and the simulation state. Layers
within it. Jump transitions with asynchronous loading behind a transition effect. The spatial
index over simulation entities, an octree per system. The jump-lane graph lives in `celestial`;
the queries over it live in `ai`.
*Filled in* (phase 6): the scene left `render` for `engine/scene` (6a), naming no device, its
models by virtual path, its file at version 2. `SystemScene` builds a star system's scene around a
file or from the system alone, places the bodies, and keeps the traffic layer's proxies from the
simulation's snapshots; layers are an entity's own or its parent's, and a hidden one is neither
drawn nor lights the scene (6b). `SpatialIndex` is an octree, kept over a loose octree and a sorted
grid by measurement (6c, [decided](../decisions.md)). Jumps (6d): an entity's system is
`Sim::InSystem`, a system scene's traffic is its own system's, and `JumpTransition` replaces the
scene shown behind a fade, reading the destination's files on the I/O worker, checking them before
anything is touched, and holding the fade until the new system has streamed in. The simulation
steps through a jump as through any frame; the stepper is not resynced, since the world did not
change.
Left for later: the index kept from the simulation (9b), a jump in the simulation (9a), a jump's
particles (10b), a destination prefetched before it is shown, and a body's look as data, all under
[carried forward](../roadmap.md#the-backlog).

## 22. Physics · phase 7 · `engine/physics`

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
each world and writes the dynamic bodies back ([architecture](../physics.md)). Body
identifiers are the engine's, and a save holds Jolt's state with the contacts, so a loaded world
steps on to the same bytes.
*Filled in* (7b): physics runs in frames, a Jolt world per frame of each system, the system's own
or the local frame of a station or planet on rails, which carries its bodies with it
([architecture](../physics.md), [decided](../decisions.md)); star systems are definitions, for
rails and gravity; a `continuous` body is swept; the flight model (`physics/Flight.h`) turns a pilot's
demands into velocity before each step, with assist or without, relative to a reference. The shell
flies a ship beside Highport, from a `flight` input context, behind the scene's pilot rigs.
*Filled in* (7c): a sensor is a body marked `sensor`, kinematic to Jolt and never asleep, and an
entity on rails can be at a place on its body (`OnRails::offset`), so a docking zone rides before a
station's dock. Contacts are the simulation's: what the last step began and ended and what touches
now, from Jolt's contact cache after the step, saved with the world ([decided](../decisions.md)).
`CastRay` and `CastShape` find the first body along a ray or a swept convex shape across a system's
frames, at the time a caller gives. Picking is the command `Physics::Pick`, a ray drawn by
presentation and cast at the time it showed, answered by the event `Physics::Picked`
([decided](../decisions.md)). The shell docks its ship in a sensor before Highport's dock and
picks with the mouse.
Left for later: overlap queries and casts that return every hit, compound shapes and a shape's
offset in its body, collision filtering beyond static against moving, picking a paused world and
picking under the cursor every frame, and docking as the engine's rather than the shell's, all under
[carried forward](../roadmap.md#the-backlog).

## 23. UI · phase 8 · `engine/ui`

RmlUi rendered through the Vulkan backend, with documents and stylesheets loaded through the
VFS so mods can restyle. Data models bound to simulation snapshots, never to simulation state
directly. Fonts through FreeType, shared with ImGui.
*Library*: RmlUi, FreeType.

*Filled in* (8b): `Ui::UiSystem` is RmlUi behind an engine interface, one at a time in a process,
with RmlUi's file interface over the VFS, its system interface over the presentation clock and the
log, and its render interface recording into `Render::Overlay`, which the renderer draws
([architecture](../ui.md)). `Ui::DataModel` binds variables of any reflected type by
address and events by name; the snapshots it reads are the simulation's, carrying the components a
world shows. FreeType is linked by RmlUi and ImGui alike. The shell's market and saved-games screens
are its first documents (`assets/ui`), over the sample game's first slice
([sample game](../sample-game.md)).

## 24. Audio · phase 8 · `engine/audio`

Buses (music, effects, UI, voice), 3D sources attached to presentation entities, streamed music,
and simulation events mapped to sounds.
*Library*: miniaudio.

*Filled in* (8c): `Audio::AudioSystem` is miniaudio behind an engine interface, with miniaudio's
file callbacks over the VFS, its log through the engine's under `audio`, and the buses as sound
groups under a master. Sources are generational handles: flat, positioned in the system frame and
placed relative to the listener each frame ([architecture](../audio.md)), or music,
streamed. The output is a device, the null backend, or none, which the tests mix themselves.
`Audio::EventSounds` maps simulation events to sounds from `Audio::EventSound` definitions, by the
event's Reflect name, and `Audio::SoundSources` plays `SoundComponent`s on presentation entities.
The shell is the first user: its music, the station's hum, and the sample game's four event sounds
([sample game](../sample-game.md)).

## 25. Persistence · phase 8 · `engine/serialise`, `engine/sim`

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
user ([architecture](../persistence.md)).

## 26. AI · phase 9 · `engine/ai`

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
([architecture](../ai.md#behaviour-trees)). The sample's traders are the first to use them all
([sample game](../sample-game.md)).

## Shaders · phase 10

Phases 4 and 5 made programs found by name through the asset VFS and built by `PipelineCache` for
each render state a pass asks for, with shared GLSL in `shaders/include`, and added the shaded
set: the metallic-roughness BRDF under one lighting include, the sky and its bake, the cluster
compute programs, the shadow and prepass depth programs, and the instance buffer every mesh
program reads ([history](#shaders--phase-5)). A material names its program and supplies
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

## 27. Animation · phases 1 and 10 · `engine/animation`

Phase 1 moved the glTF clip player and the rotation animation out of `Model` and `Scene` into
this module as `Rig`, `Components.h` and `Playback.h`, unchanged in behaviour. Phase 10
generalises it to keyframed TRS tracks on any node and adds clip events (for example, "clamp
closed" triggering a sound).

Phase 10d built both ([animation](../animation.md)). A clip is one thing whatever it
moves (`Clip.h`), sampled alike: a glTF rig's, which now plays on a model with no skin too, moving
where its mesh is drawn; and a clip file's, tracks on named nodes that a player binds to the
scene's own entities, so a rig of procedural parts built in code, or written in a scene file, moves
without a model. A clip file is a resource, its YAML parsed on the main thread. Events are a clip
file's alone, since glTF has no place for them, each crossed once in the order met whichever way
the clip plays, and a time set past a clip's end shows the rig there without them. The game gives
an event its meaning: the shell's are sounds, by data (`Audio::EventSound::clip`). The first rig is
a pad's docking clamps, closing on a docked ship and letting it go, which the dressing builds and
fits to the hull from the snapshots.

## 28. Presentation polish · phase 10 · `engine/render`

The atmosphere scattering shell, planet shading (clouds, night lights, rings), GPU particles
with a sorted transparent pass, glass on that transparent pass. The transparent pass is
forward-lit through the same clusters as the opaque one, which is one of the reasons the
renderer is clustered forward.

Phase 10a built the first part, and the frame written to a file the rest is judged by: air and
rings as maths over a sphere rather than a mesh shell or a baked table, drawn by the planet program
over its surface and by an atmosphere program over the sky, and the bodies in the environment's
capture ([atmospheres](../rendering.md#atmospheres-and-rings)); clouds and city lights from noise, since the
planet pack has no maps of them. Phase 10b built the transparent pass: a transparent program's draws
and the particle emitters sorted back to front and drawn last in the opaque pass's rendering, blended
by what they add and what they let through by colour, with glass as a thin tinted pane and particles
stepped on the GPU in a pool the CPU only tells of births
([transparency](../rendering-effects.md#transparency), [particles](../rendering-effects.md#particles)).

Phase 10c built the post chain ([post-processing](../rendering-effects.md#post-processing)): passes in the
pass list on the resolved HDR target between the opaque pass and the output pass's tonemapping,
bloom first (stars, engines and lit windows are far brighter than the hull), and the colour-grading
LUT of 11c later. Ambient occlusion reads the depth prepass (5e), which writes view-space normals and
runs whenever it is on; the occlusion attenuates the image-based light in `lighting.glsl`, not the
sun or the clusters' lights, so it is computed before the opaque pass shades rather than darkening
the frame after it. The prepass's multisampled depth and normals are read at sample zero, which a
resolve to sample zero would leave, without the copy. Masks come two ways ([decided](../decisions.md)). The
depth target has a stencil aspect (`D32_SFLOAT_S8_UINT`, with the device's separate depth and
stencil layouts, and resolved to sample zero for the single-sampled post passes): bits 0 to 2 the
engine's (the cockpit, the outlined, and what post-processing leaves alone), 3 to 7 the game's (a
scanner's hologram, say). Every opaque draw writes its bits, the prepass's or not, so the post
passes test it and the hardware skips what a full-screen pass masks out before shading it. And the
depth prepass writes an object ID buffer beside its normals, which no material program has to know
of: from it a jump-flood pass draws wide, soft target glows that read at any distance, and the
selected's crisp one-pixel line takes its colour. Screen-space reflections stay out
([decided](../decisions.md)).

## 29. Scripting · phase 11 · `engine/script`

A Wasm host: module loading from mod mounts, fuel/time limits, a capability-scoped host API
generated from reflection, and calls into scripts from AI considerations and events.
*Library*: WAMR (WebAssembly Micro Runtime) 2.4.5, its fast interpreter ([decided](../decisions.md)).
*Filled in* (11b): scripts as definitions (`defs/scripts`), their modules read through the VFS; fuel
by instruction metering and a watchdog's time limit; the host API's layouts, its C header and its
Rust module generated from the types `Reflect::Attribute::Script` marks, imports checked against
each script's grants as it loads; handlers of events that send commands, and inputs for a decision's
considerations. Scripts in C or Rust. See [scripts](../scripts.md).

## 30. Shipping · phase 11

Pack files, crash handling (a backtrace and the log tail, written on a fatal signal), the colour-grading LUT, the cinematic camera and sequencer, localisation string tables.
*Library*: zstd 1.5.7, for pack files, shared with libktx.
*Filled in* (11a): pack files (`vfs/Pack.h`, `jettison_pack`, `jettison_add_pack`), mounted as a
directory is; mods in the user's data directory; definitions in layers, by patches. See
[files, packs and mods](../files-and-mods.md).
*Filled in* (11c): the colour grade, a `.cube`'s table over the display's colours after the
tonemap (`render/ColourGrade.h`); the cinematic camera and sequencer, shots on rails in the frames of
roles the game binds, over the camera rigs (`scene/Sequence.h`). See
[colour grading](../rendering-effects.md#colour-grading) and [cinematics](../animation.md#sequences).
*Filled in* (11d): crash handling, a report of the signal, the frames, named where a symboliser is,
and the log's tail, written on a fatal signal (`platform/Crash.h`, `Core::LogTail`); string tables
through the VFS, a language over its base and a mod's strings over the base's, documents' keys
replaced as RmlUi reads them (`ui/StringTable.h`). See [crashes](../runtime.md#crash-reports) and
[localisation](../ui.md#string-tables).

## 31. Docks · phase 9 · `engine/dock`

Stations' pads and the traffic on their decks, in the simulation and deterministic, so `dock` joins
the simulation modules of the [coding standards](../coding-standards.md) as it is made. It depends on
`sim`, `physics` and `ai` (A* for the deck's roads, steering for the approach). Entries 22 and 26
left docking to a game and stood a sensor in for it; this is the engine's version, which a different
space game would want unchanged, with the game's rules (who may land where, what a pad costs, what
loitering on one means) left to the game.

**Scale.** A pad takes any hull that fits it ([decided](../decisions.md)):

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
the ship until it has crossed (9e) ([architecture](../docks.md)). On 2026-11-21, for
Manifest's W10, a ship flown in stops first at a point out along its pad's normal and flies straight
down it from there, a stopgap for a ship that circled the point above its pad until the pad lapsed
([carried forward](../roadmap.md#the-backlog)).
