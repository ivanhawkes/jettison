# History

The finished part of the [roadmap](roadmap.md): phases 0–4 as they were closed, and catalogue
entries 1–18, the briefs for the systems those phases built, with what each one became. Entry
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

## System catalogue, entries 1–18

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
