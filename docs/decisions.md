# Decisions

The engine's settled decisions, by the feature they shape. Each bullet gives the rule in bold, the
date it was settled, why, and the document where it applies. A later decision replaces an earlier
one with its own date; one that no longer applies, its code moved to Manifest or superseded, is kept
word for word in [retired decisions](history/retired-decisions.md). Open questions and work not yet
done are in the [roadmap](roadmap.md). Every decision keeps the boundary in [game
integration](game-integration.md): the engine is genre-neutral, and a game's rules and content are
the game's ([Process](#process)).

## Simulation

- **Multiplayer in time; single-player first** (2026-11-12). Every command carries its sender
  (`Sim::Controller`), the step gives shared facilities such as berths and slots in command order,
  and no simulation module assumes one player, so a server can hold every player's commands in one
  queue without the state changing shape. A game's simulation is still single-player; the transport
  is under [Networking](#networking). [Simulation](simulation.md)
- **A controller commands an entity, and whoever holds its helm flies it** (2026-11-13).
  `Sim::MayCommand` takes a command from the entity's `ControlledBy`, or from no one in particular
  (the game's own rules); `Sim::MayFly` takes flight controls only from whoever holds its
  `Sim::Helm`, or from no one in particular, and the entity's behaviour stands down meanwhile.
  Taking the helm is a saved command, so a loaded game knows who is flying; an entity whose helm no
  one holds is flown by what the game gives it (its behaviour, the docks).
  [Simulation](simulation.md)
- **An event says why by a reason and its arguments** (2026-11-13). A `Sim::Reason` is a key named
  by an enumerator registered as `<Module>.Reason` (`Lift::Reason::OverCapacity` is
  `reason.lift.over_capacity`) and a list of positional arguments, each an integer, an entity or an
  identifier. The step makes no English, so the player reads it in their language; two modules
  cannot make one key; and the string tables' check holds a translation to the arguments. The step's
  log lines stay English, for developers. A script's own reasons are `reason.script.<script>.<name>`:
  it names them, and the host writes the prefix, so no script gives another's or a module's.
  [Simulation](simulation.md)
- **Quiet steps are passed over by each system's own word, to the same bits as stepping**
  (2026-11-20, the user's). A system says the next step it must run in and how to write what the
  steps between would have left (`Sim::Quiet`); one that says nothing runs every step, so a game's
  system is correct before it has a quiet. A save and a replay must give one hash, so extrapolating,
  or passing over steps to within a tolerance, was not considered. [Simulation](simulation.md)

## Scenes

- **The spatial index is an octree** (2026-11-11), leaves of 64, not a sorted grid and not loose. A
  grid searches cells that in open space are almost all empty: at 100,000 moving entities, eight
  nearest neighbours took 4.6 µs on the octree against 36 µs on the best grid (Release, Ryzen 9
  3900X). Loose cubes overlap, which made every small query 2.5 to 4 times slower for no cheaper
  update at 60 Hz. Range results past 512 are radix sorted. The benchmark is
  `source/engine/spatial/tests/SpatialBenchmark.cpp`; the grid it was measured against is at commit
  64d5488. [Scenes](scene.md)
- **The spatial index is made from the components each step** (2026-11-12). It is not state: a save
  does not hold it, and a loaded world has it from its next step. One system builds it as the step
  begins (`Spatial::FrameIndices`, an index a frame), so steering, a sensor and a game system read
  the same index; how the systems are ordered round it is in [the spatial
  index](scene.md#the-spatial-index). [Scenes](scene.md)

## Coordinate frames and units

What moves a frame's bodies and what pulls in it are under [Physics](#physics).

- **An entity's frame is the game's** (2026-11-18). `Sim::InFrame` holds a frame, a partition the
  game defines, by its name hashed (`Sim::FrameId`). An entity in no frame has no body and is not in
  the spatial index, so even a game of one frame gives its entities that frame. The neutral snapshot
  needs a frame to stop interpolation at, and physics, AI, the docks and the spatial index must have
  one without depending on a space module. [Coordinate frames and units](coordinates.md)

## Physics

- **Physics is simulation state, saved with Jolt's own state, on one thread** (2026-11-11; the
  rebuild across Jolt versions 2026-11-13). The world saves Jolt's state as a world service rather
  than rebuilding it from components on a load: each step warm-starts from Jolt's contact cache, and
  a world rebuilt without it steps on differently. Physics' layout (5 now) holds Jolt's version and
  the feature bits that change its state, and a save of another version rebuilds the bodies from the
  components and physics' records (each body's place, velocity and sleep), losing one step's warm
  start. The rebuild is tested now, by a load given another version, rather than first run the day
  Jolt moves. Body identifiers are the engine's (the lowest free index, with a saved sequence
  number), because Jolt's free list is history a save does not hold. Jolt's job system is
  single-threaded, as the simulation is ([J07](roadmap.md#j07)). [Physics](physics.md)
- **A Jolt world per frame and per local frame** (2026-11-11, revising one world per star system the
  same day). Each frame (`Sim::InFrame`) and each local frame in it (`Physics::LocalFrame`) is a
  Jolt world, made with its first body, its positions metres from the frame's body and its
  velocities relative to that body's motion. That keeps Jolt's float bounds and sweeps on small
  numbers: swept in an outer frame, a ball fired at 240 m/s passed through a panel on a body moving
  at 30 km/s. A frame's change of speed between steps is taken off its dynamic bodies, so its
  rounding does not accumulate; bodies in different frames do not meet. [Physics](physics.md),
  [Coordinate frames and units](coordinates.md)
- **What moves a frame's bodies, and what pulls in it, is the game's** (2026-11-11; corrected
  2026-11-25 to the frame model). A frame's `Physics::FrameModel` places the bodies that rails and
  local frames name (`BodyAt`) and gives the frame's gravity (`GravityAt`), which physics adds to
  each dynamic body as the step begins; a frame with no model has no rails, no local frames and no
  gravity. Without gravity a vehicle beside a body on rails would fall away from it, and an assist
  could hold it only by thrusting all the while. [Physics](physics.md)
- **Contacts are the simulation's, from Jolt's contact cache** (2026-11-11). The physics world
  keeps, and saves, what the last step began and ended and what touches now, because what a contact
  means is decided in the step and presentation's event queue is never read back. Whether two bodies
  touch is asked of Jolt's cache after the step (`PhysicsSystem::WereBodiesInContact`), not counted
  from its callbacks, which add a contact per pair of sub-shapes, report a swept contact a step late
  and drop a sleeping body's. A sensor never sleeps, so a body come to rest inside it is still
  reported inside. [Physics](physics.md)
- **Picking is a command, answered by an event** (2026-11-11). `Physics::Pick` is applied as the
  next step begins and answered by `Physics::Picked` among its events, so presentation never reads
  the simulation's state. A pick is recorded like any command, and a replay raises its answer again.
  The pick carries the simulation time its ray was drawn at, and each frame is placed there for the
  cast: presentation draws behind the simulation, and a click on a fast-moving body would otherwise
  miss. [Physics](physics.md)
- **A pick presentation makes for itself reads the snapshots** (2026-11-26). What the cursor is over
  and what a click selects are presentation's, answered at once against the bodies as the snapshots
  draw them (`Physics::SnapshotPicker`), with Jolt's own ray test on shapes built from their
  definitions, so a paused world is picked and a hover costs no command a frame. It reads no
  simulation state, so a client with no physics world picks the same way. A pick a game's rule
  acts on stays the command, recorded and replayed. [Physics](physics.md)
- **A compound's parts are definitions inside it, and only a cast names the part** (2026-11-28).
  A shape's offset and turn are fields of every `ShapeDefinition`, and a compound's parts are
  definitions of their own in its `parts`, each placed by its own offset and turn, rather than
  names of shapes defined elsewhere: a compound is one file and one definition, a mod replaces it
  whole, the order shapes are defined in cannot matter, and a picker builds it from the definition
  alone. It is Jolt's `StaticCompoundShape`, a shape being fixed once built. `CastHit::part` names
  the part a ray or sweep hit, read from the hit's sub-shape for nothing. A contact names none: it
  is a pair of bodies read from Jolt's cache, which keeps no sub-shape, and `ContactEvent` is saved,
  so a part there would be a contact per pair of parts and a new save layout. An overlap answers
  bodies, each once, so it names none either. [Physics](physics.md)

## Transit

- **A transit is counted in whole steps** (2026-11-12, restated without the space game 2026-11-25).
  An entity in a portal comes out at a step count (`arrivalStep`), never by comparing seconds, so it
  is the same every run however its seconds round. Physics removes the entity's body the step it
  leaves and makes it again the step after it arrives. [Transit](transit.md)
- **An arrival comes out at rest, in a local frame the game names, clear of what is there**
  (2026-11-12, restated 2026-11-25). Speed carried through a passage or a portal would send an
  entity that went in fast into whatever is ahead of the exit. At rest in the exit's local frame, it
  can meet what is there, a platform's berths among them, without changing frames.
  [Transit](transit.md)
- **A gated portal sends only its slot's holder** (2026-11-13, restated 2026-11-25). A gated portal
  is a `Transit::Slot` with a sensor: its holder touching the sensor goes through, and any other
  body passes unsent. The slot is a shared facility given first come first, as a berth is, so an
  entity asks once and waits its turn; what a hold asks of its holder, a toll or a licence, is the
  game's. [Transit](transit.md)

## Docks

- **Docking is the engine's, as berths a platform gives** (2026-11-12). Who holds a berth is
  simulation state in the module `dock`, saved and given by commands in their order
  (`Dock::RequestBerth`): a berth is a shared facility in a world planned for several players, so
  every player must see the same holder. [Docks](docks.md)
- **A vehicle fits its own berth class and those ranked above it** (2026-11-18). A berth class
  (`Dock::BerthClassDefinition`) is a definition with a rank and the largest vehicle it holds. A
  vehicle names its own class, and fits a berth of that rank or above whose largest vehicle holds
  it; the lowest-ranked free berth is given first. Fitting by size alone would let a vehicle onto a
  berth the game means for another kind. [Docks](docks.md)
- **A vehicle comes down onto its berth from outside the platform's sphere, down a corridor**
  (2026-11-12; the corridor 2026-11-26, the user's design of 2026-11-21). The docks fly it round the
  platform's sphere by a course (`Ai::Waypoints`) to the mouth of a corridor out along the berth's
  normal, and down the corridor onto the berth without stopping: the speed falls with the way left,
  the velocity across the line is damped rather than the point pursued, and the platform is no
  obstacle on it (`byShape`), as approach paths in other games relax avoidance against the
  station. A reservation lapses when the vehicle stops coming nearer, not at a fixed time. A covered
  berth, under a pier, is flown to among the decks and piers by a course round their boxes, in the
  berth's axes, and is allowed where its headroom is twice its largest vehicle's height and 4 m
  more. [Docks](docks.md)
- **A docked vehicle is a kinematic body on its platform's rails** (2026-11-12), not a dynamic one
  held by its assist or a constraint. On rails (`Physics::OnRails`) it is exactly where its berth is
  at every step in every build, and meets nothing kinematic, so a deck and its vehicles make no
  contacts; what holds it is a saved component. Let go, it is dynamic again where it lay, at rest in
  the platform's frame. Docking uses no sensor. [Docks](docks.md)
- **A clamp's motion is presentation's** (2026-11-13). The docks clamp a vehicle the moment it is
  within reach, and nothing in the simulation waits on a clamp; presentation animates the clamps
  from the snapshots, their sounds the clip's events ([Audio](#audio)). The motion changes nothing
  the step reads, so a save does not carry it. [Docks](docks.md)
- **A display berth's vehicle is presentation's** (2026-11-12). No vehicle is given a display berth:
  its layout names the vehicle definition shown, and presentation draws it. That vehicle never moves
  or docks, so a body there would be state every save carries for nothing. [Docks](docks.md)
- **The berths are shown in snapshots** (2026-11-12). `Dock::Docks::Attach` shows `Dock::Berth` in
  every snapshot, so presentation (a berth's lamp, a screen) reads a berth's state from the latest
  snapshot and never from the world. [Docks](docks.md)

## AI

- **Steering is velocities, which flight assist holds** (2026-11-12). Each behaviour returns a
  velocity, and flight assist (`Physics::FlightControls`) reaches their sum at the thrusters'
  limits, so the flight model, not steering, decides how fast a vehicle changes speed. Arrival
  brakes at half the weakest thruster, leaving the rest for turning and dodging, and its last metres
  are at the distance a second: √(2ad) alone, unbounded in slope at the end, was overshot a step at
  a time. [AI](ai.md)
- **An obstacle is the sphere that holds its shape, and the guarantee is the braking** (2026-11-12).
  Avoidance finds what is near through the spatial index, not Jolt: the index answers for a whole
  frame in identifier order, and a sphere's closest approach is arithmetic, where a cast a vehicle a
  step is a query into a Jolt world. What keeps a vehicle off a hull is that it never closes on an
  obstacle faster than it can stop before the clearance, whatever the turn aside does. [AI](ai.md)
- **Agents act through commands applied at once** (2026-11-12). A behaviour tree's task sends the
  commands a player would, and `Sim::SimWorld::Apply` applies each as it is sent, through the same
  handler and checks. They are not queued, because the queue is what a session records, and a replay
  would apply them twice (its trees send them again); the trees run first in the step, so their
  commands find the world as queued ones do. [AI](ai.md)
- **Behaviour trees resume their running child, and interrupt one only with a halt** (2026-11-12;
  the halt 2026-11-26). A sequence or a selector goes back to the running child rather than starting
  from the first. Only a reactive node, a parallel once decided, a new tree or a helm taken leaves a
  running node, and each tells its tasks so (`Ai::TaskHalt`), so a task waiting on an answer can let
  go of what it holds. An entity's place in its tree is a memory and a flag a node, saved with the
  world. [AI](ai.md)

## Persistence

- **Migrations are a chain of steps over a document** (2026-11-12; suspended until player testing,
  2026-11-23). One step a version, written once and never changed, reads only the data its version
  wrote, so a format's history is a list of small edits, each tested by its version's fixture. Each
  part of a save (the save game, the world, each game section) is versioned on its own, so a change
  to one is a step in its own chain. The machinery stays, held by its suites.
  [Persistence](persistence.md)
- **A component migrates by its own chain, over its binary record** (2026-11-13). Each component
  storage in a world's save carries its own version, and a component's chain
  (`Serialise::Migrations<Serialise::BinaryRecord>`) is given the old value as its field table, each
  field's name and bytes, so the old C++ type is never needed. Authored content has its own YAML
  chain (`YamlMigrations`); the world's own steps stay for changes to the whole world.
  [Persistence](persistence.md)
- **Definitions migrate** (2026-11-12; suspended until player testing, 2026-11-23). A definition is
  authored content that mods ship, and refusing every file of an older version would break every mod
  at each format change. A type's YAML migrations run on its files as they load, and cost nothing
  for a current file; saves hold only definitions' names, so a definition's format changes without
  touching a save. [Persistence](persistence.md)
- **Saves and settings are written durably** (2026-11-12). A whole-file write flushes the new file
  before the rename and the directory after it (`Core::WriteFileDurably`): without the first, a
  power cut can leave an empty file where the save was; without the second, the rename may be lost.
  Saves and settings are small and written when the player asks, so no one waits on the flushes.
  [Persistence](persistence.md)
- **A save's details are the game's strings** (2026-11-18). `Sim::SaveInfo::details` is a list of
  key and value strings, read with the save's info, in place of fields that named a galaxy and a
  system, so a save list reads them without decoding the game's sections and the engine defines no
  format for the game's metadata. [Persistence](persistence.md)
- **Formats change in place until player testing** (2026-11-23, the user's). Every save, definition,
  scene and other versioned file is in Jettison's or Manifest's repository, so a format change
  rewrites those files in the same commit instead of adding a version and a migration step. Versions
  are still checked, so a file of another version is refused by name, and a field whose meaning or
  type changes takes a new name. The first build given to anyone outside the two repositories sets
  the baseline: from then on every format change carries a step. [Persistence](persistence.md)

## Files, packs and mods

- **A game's content mounts under its own identifier** (2026-11-27, the user's). The shell mounts a
  game's content under its `mod.yaml`'s `identifier`, `base` when it has none, so a mod's
  `requires` can name it (`manifest`) and a dedicated server's session, which names it the same,
  agrees with the client on the world content's packages. Overlays requiring `base` instead was
  rejected. [Files, packs and mods](files-and-mods.md#the-shells-mounts)
- **Pack files are compressed with zstd, one copy shared with libktx** (2026-11-13). libktx compiles
  its own zstd with public symbols and no option for another, so the engine builds zstd 1.5.7 once
  and builds `ktx_read` against it, taking libktx's copy out of its sources in
  `source/external/CMakeLists.txt`. A file in a pack is its own frame, compressed only where that
  pays (`vfs/Pack.h`); LZ4 would have been a second compressor for the same job. [Files, packs and
  mods](files-and-mods.md)
- **Mods are directories or packs under `mods/` in the user's data directory** (2026-11-13). A mod
  is a directory for its author or a pack for its players, mounted between the game's content and
  the user's own files. Not zip archives: a pack is a small format the engine owns, which reads one
  file without the rest and checks what it reads. [Files, packs and mods](files-and-mods.md)
- **A player's mods are a list beside the settings, and a save names them** (2026-11-13). The
  player's `mods.yaml` sets the mount order and which mods are on; a mod whose engine or
  requirements are not met is refused, not reordered. A save records the mods enabled and their
  versions, and a load with others says which differ and goes on. The rules are in [the player's mod
  list](files-and-mods.md#the-players-mod-list). [Files, packs and mods](files-and-mods.md)
- **Definitions layer by patches, merged as YAML** (2026-11-13). A mod's `<name>.patch.yaml` beside
  a definition changes what it names over every layer below, and a whole file still replaces one.
  Merging the YAML before it is read keeps one reader for files and patches, with its errors and
  lines, and lets a patch say what a value cannot (a field removed, a list appended to, elements
  merged by a key, elements removed). Each layer migrates on its own. Scenes layer so too, and a
  stylesheet's `<name>.patch.rcss` is appended to it (2026-11-26); one function,
  `FileSystem::ReadPatched`, holds the rules. [Files, packs and mods](files-and-mods.md)
- **A pack is reopened when its file changes, on request** (2026-11-26). `FileSystem::Refresh`
  opens a changed pack again, and only the resource manager's hot-reload check asks, so a shipped
  build reads a pack as mounted and no thread or file watcher is added. A pack that does not open
  keeps the old one. [Files, packs and mods](files-and-mods.md#packs)
- **Assets are cooked from a source tree outside the game's repository** (2026-11-25, the user's).
  `jettison_cook` cooks models and the images they use, once or watching as a daemon, into the
  game's asset folder, mirroring the source tree; a settings file beside a source
  (`big-ship.gltf.yaml`) layers over the game's `cook.yaml` and the engine's defaults, merged as
  patches are. A cooked model is a normalised GLB, which the rig and clips still read, and a meshlet
  page file beside it; an uncooked glTF draws on the indexed path with a warning. The engine's own
  content is cooked at build time and its dense test meshes generated, so nothing cooked or large
  is committed. [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)
- **Device-free mesh work is the `geometry` module's** (2026-11-25, the user's). A CPU module in the
  data tier, so the cook, which builds without presentation, the renderer and later physics
  ([J12](roadmap.md#j12)) share one reader of a model's triangles. [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)
- **A package holds both sides, split by directory; the server's bundle leaves presentation out**
  (2026-11-27, the user's). The world is the server's alone and a client is not trusted, so what a
  server reads (`defs/`, `scripts/`, and cooked collision shapes) is the world's, and models,
  textures, scenes, UI, sounds and string tables are presentation's. One package a mod, not a client
  and a server tree: most mods span both sides (a ship is a definition and a model), and a client
  needs the world's definitions to show and pre-check what it sends. The packer makes a server pack
  without the presentation directories; a mod whose `mod.yaml` says `side: client` is cosmetic,
  which a server ignores and players need not match; and a client checks on connecting that it holds
  the server's world content ([files, packs and mods](files-and-mods.md#the-servers-world-content)). [Files, packs and mods](files-and-mods.md)

## Scripts

- **WAMR, not wasmtime, runs the scripts** (2026-11-13). WAMR is C, built inside the engine's build
  with its toolchain and sanitisers; its fast interpreter counts instructions
  (`WAMR_BUILD_INSTRUCTION_METERING`), the deterministic fuel the simulation needs, and its thread
  manager lets a watchdog stop a call. wasmtime is Rust built by cargo, or a prebuilt library
  outside the sanitised build, and its JIT's speed is not needed: a handler call costs 0.27 µs
  (Release). WAMR's ahead-of-time compiler is the next step should a profile find scripts dear, and
  for handlers alone: its compiled code counts no fuel (2026-11-26), so a call inside the step
  would lose the bound that makes it the same on every machine. [Scripts](scripts.md)
- **Scripts are C or Rust** (2026-11-13). Both compile to wasm32 with the devenv's toolchain (clang
  with wasm-ld, and nixpkgs' rustc) against bindings generated for each from the same layouts. The
  script suite runs its host API modules in both languages (`source/engine/script/tests/scripts`),
  so each stays proven; the engine's shell has no scripts of its own. [Scripts](scripts.md)
- **An input call starts from the module as it was instantiated** (2026-11-13). An input is answered
  inside the step, so its answer may depend only on its inputs, never on anything a script kept,
  which a save does not hold. A tree's task a script answers is ticked so too (2026-11-26), keeping
  only the memory its tree saves, and its commands are applied at once, as a task's are. A handler,
  called outside the step, keeps its instance. A trap is not
  a script's end, since a call would trap the same way again; only a call past the time limit stops
  a script, the one outcome that depends on the machine. [Scripts](scripts.md)
- **A script keeps what it hands the host by key** (2026-11-13). A script stores bytes under its own
  keys (the `store` and `load` imports), in an encoding it chooses and migrates itself, so the host
  never needs its types; saving its linear memory whole would be up to 16 MiB a script and
  unreadable by a rebuilt module. The keys are a section of the save game, not world state, since
  handlers run outside the step. Each script is held to 64 KiB and 256 keys, past which a store is
  refused, so no mod grows every save. [Scripts](scripts.md)
- **A handler reads the snapshot, and its commands carry a sender the host gives** (2026-11-13; the
  sender corrected 2026-11-25). A handler reads the newest stepped snapshot, a component at a time
  as it is granted (`read:<Component>`), through imports generated from reflection, never the
  step's state, which a handler outside the step could see half way; an input reads nothing.
  Every `Sim::Controller` in a script's command, nested ones included, is replaced by the sender the
  game names for the event its handler runs for (`ScriptHost::SendCommandsTo`'s `Sender`, decided
  2026-11-26), and with none such a command is refused, so a script cannot command as a player, an
  NPC or no one in particular. [Scripts](scripts.md)
- **The session runs the mods' scripts, not the shell** (2026-11-27). The world is the server's
  alone and a client is not trusted, yet a script answers an NPC's tree and sends commands, so the
  scripts run where the world does: `Host::Session` owns the `ScriptHost` when its options ask for
  scripts, made once the world is registered and before its initial state, and the shell takes the
  session's. The game says whose authority a script's command carries through
  `Host::Game::ScriptSender`; the session drives no loop, so what steps the world gives the host
  its events and a snapshot, as the shell does. [Scripts](scripts.md#host-scripts-in-a-game)

## Networking

- **The network seam is ENet over UDP, with a game's verifier and bearer tokens** (2026-11-20, the
  user's). ENet gives reliable-ordered and unreliable-sequenced channels, so one stack carries an
  idle game's requests and a real-time game's inputs and snapshots; TCP would serve only the first,
  TLS does not run over UDP, and yojimbo's connect tokens would shape the game's accounts. The
  engine runs the handshake and asks the game's verifier, checks tokens against digests, frames and
  bounds messages, and sends each session the newest snapshot at a bounded rate. libsodium is
  fetched and built static, through robinlinden's CMake build, since nixpkgs ships it shared only.
  [Networking](networking.md)
- **A pinned server key, ephemeral keys each side, and the credential sent after both** (2026-11-26,
  J01). The client pins the server's public key, so no certificate or authority is needed; the
  connection's keys mix the client's fresh key with the server's own and with the server's fresh
  one, so a server key stolen later opens no recorded connection. The hello, with its credential,
  goes after the server's fresh key and its proof, a round trip later than sealing it to the
  server's key alone would: a recorded hello replayed on a new connection does not open and cannot
  hold one of an actor's sessions, and no credential is sent to a server that has not shown it holds
  the pinned key now, which an offer of the pinned public key as a fresh one would otherwise get. Messages are sealed above ENet, whose headers stay plain: an attacker on the
  path can close a connection, as one can a TCP connection under TLS, but not read or forge a
  message. [Networking](networking.md)

## Rendering

- **Texture coordinate sets by role, fetched by vertex pulling** (2026-11-27, the user's). A mesh
  carries up to eight sets, each named by a role in its asset's cook settings, and a material's slot
  reads a role; the shaders fetch only the sets a material reads, from per-mesh streams, on the
  indexed, mesh-shader and ray-traced paths alike. A fixed vertex with every set, or a variant per
  count of sets, was rejected. [Roadmap J64](roadmap.md#j64)
- **The material library holds the look** (2026-11-27, the user's). A glTF material's name binds the
  library material of that name, and the glTF's own material values are not read, so a shared
  material changes in one file. [Roadmap J65](roadmap.md#j65)
- **Manifest defines the art standards** (2026-11-27, the user's). Names, layout, scale and
  authoring rules are Manifest's [art standards](../../manifest/docs/art-standards.md); the engine
  supports the workflow and its test content follows them. [Roadmap J64 to
  J67](roadmap.md#a-separate-track-the-art-standards-support)
- **Bindless textures, with descriptor indexing required** (2026-11-27, the user's). Every material
  texture is in one descriptor array, indexed by slot from the material buffer, so a program's slot
  count is not the descriptor layout's. Device choice refuses a device without descriptor indexing:
  of those today's 1.3 check accepts, that leaves out Intel's Broadwell on `hasvk`, and NVIDIA's own
  driver is unchecked ([devices](dense-geometry-and-ray-tracing.md#devices)); those limits are
  accepted. Raising the fixed slot count to sixteen, past the per-stage sampler minimum, was
  rejected. [J68](history.md#j68)
- **Clustered forward rendering** (2026-11-10). Lights are assigned to view-space clusters in
  compute, and every surface is shaded once in its own material program. There is no G-buffer:
  ambient occlusion reads the depth prepass's depth and normals instead. [Rendering](rendering.md)
- **The cluster grid is 32 × 18 × 48** (2026-11-11), with the index list at 32 entries a cluster on
  average. Against 265 lights, the opaque pass fell from 0.43 ms at 16 × 9 × 24 to 0.34 ms on an RTX
  5060 Ti, with the assignment no dearer. [Rendering](rendering.md)
- **The depth prepass is kept, and off** (2026-11-11). It cost more than it saved on both devices,
  since front-to-back batches already let the depth test reject what is behind: 3.4 ms for 0.5 ms
  saved on lavapipe. It is where ambient occlusion's normals and the outlines' object IDs are
  written, which turn it on. [Rendering](rendering.md)
- **A CPU device gets RGBA8 textures, not BC7** (2026-11-11). Lavapipe decodes BC7 in software at
  every fetch: an opaque pass took 469 ms with BC7 against 23 ms with RGBA8 at 640 × 480.
  Transcoding to BC7 is tested without a device, and uploading it on hardware.
  [Rendering](rendering.md)
- **A CPU device filters without anisotropy** (2026-11-12). A CPU takes each anisotropic tap in
  software: at 16x through the triplanar program's nine fetches a pixel, an opaque pass on lavapipe
  took 59 ms against 22 ms without. Hardware filters at 16x. [Rendering](rendering.md)
- **Screen-space reflections stay out** (2026-11-11). They would need each pixel's roughness,
  metalness and a colour of the scene to march through, most of the G-buffer clustered forward does
  without. Where a reflected ray leaves the screen they fall back to the environment cube, a seam
  plain to see against a dark environment. [Rendering](rendering.md)
- **A draw under a pixel across is not drawn** (2026-11-12, `Render::TooSmallToSee`). It draws
  nothing a viewer could tell from its absence, and each triangle left out is a block of shading
  saved on lavapipe. How a draw's breadth is measured, and what is exempt, is in [collecting and
  culling](rendering.md#collecting-and-culling). [Rendering](rendering.md)
- **The GPU drawn with is the one with the monitor** (2026-11-12). A card kept for compute with no
  monitor still says it can present on Wayland, through the compositor's copy across the bus. So a
  device is ruled out for what it cannot do, each reason logged, and of the rest the one whose DRM
  card has a connected connector is chosen, then by type, then in Vulkan's order; `--gpu` overrides
  it. [Rendering](rendering.md)
- **A run with no display draws offscreen, on GLFW's null platform** (2026-11-20). NVIDIA's driver
  cannot present to Xvfb and has no headless surface, and tests must not put windows on the desktop,
  so the frame is drawn into the renderer's own images with no surface. The swapchain stays for
  every run with a display, and one reference view holds the offscreen frame to the swapchain's byte
  for byte. [Rendering](rendering.md)
- **Ray tracing comes in, on ray queries, with Mega Geometry where NVIDIA offers it** (2026-11-25, the
  user's). The sun's and opted-in local lights' shadows, ambient occlusion and reflections are traced
  by `VK_KHR_ray_query` inside the forward pass, on plain KHR acceleration structures, which every
  vendor and lavapipe run. `VK_NV_cluster_acceleration_structure` is an optional accelerator behind
  the device's capability table: no cross-vendor equivalent exists on Vulkan, and AMD's
  `VK_AMDX_dense_geometry_format` is a compressed input for future hardware. It replaces the scope's
  "not worth its cost", since Manifest's art now asks for dense kits and reflections. [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)
- **Today's paths stay as the fallback, kept under test** (2026-11-25, the user's). A device without
  mesh shaders or ray queries draws as now: indexed draws, cascades, screen-space ambient occlusion,
  the environment cube. Each new path is a switch whose off reason is logged, and reference views
  run with each forced off. Older cards (AMD before RDNA 2, NVIDIA before Turing) have neither and
  run every fallback, with no other path planned for them (2026-11-26, the user's). [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)
- **No AMD device measures the plan** (2026-11-26, the user's). None is at hand: lavapipe checks
  the paths' correctness on a driver that is not NVIDIA's, and AMD's performance is measured later,
  or by others. [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)
- **Models are drawn as meshlets, culled on the GPU, with occlusion culling and streaming**
  (2026-11-25, the user's). Named meshlets because the light grid owns "cluster". Occlusion culling
  comes in for station interiors and dense fields; virtual shadow maps stay out, ray-traced sun
  shadows serving instead. [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)
- **Reflections are ray traced, sharp first** (2026-11-25, the user's, at the art team's request).
  One ray a pixel below a roughness cut-off, with no denoiser; glossy reflections wait for the
  temporal stage. Screen-space reflections stay out, as decided above. [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)
- **A point or spot light casts a shadow only when it opts in** (2026-11-25, the user's), and at most
  a fixed number of a pixel's lights trace one, nearest first, so a scene cannot ask for an unbounded
  cost. [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)
- **Temporal AA replaces MSAA** (2026-11-25, the user's), in J49, on the frame history the denoisers
  need. One anti-aliasing path is kept, not two. It lets the visibility buffer and software raster
  (J50) in, since they give one sample a pixel. [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)
- **Mega Geometry is checked on Lythir, not by the gate** (2026-11-25, the user's). Lavapipe lacks
  the extension, so `scripts/mega-geometry-check.sh` compares the frames with and without it on the
  RTX 5060 Ti before a push that touches it. [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)

## Transparency, particles and post-processing

- **Ambient occlusion is off on a CPU device by default** (2026-11-13). It runs the depth prepass,
  which on lavapipe took a test frame from 44.2 ms to 53 ms against a 50 ms budget; hardware has it
  on. The smoke test and the reference views therefore draw without it, except the post chain's
  view, which asks for it. [Transparency, particles and post-processing](rendering-effects.md)
- **Stencil and an object ID buffer, both** (2026-11-11). The stencil, on a `D32_SFLOAT_S8_UINT`
  depth target (reversed-Z needs float depth), is the cheap mask the hardware tests before shading,
  for crisp outlines and for keeping post-processing off chosen surfaces. The ID buffer, written by
  the depth prepass, is the flexible one any pass can read, for wide, soft highlights; its cost is
  the prepass running whenever outlines are on. [Transparency, particles and
  post-processing](rendering-effects.md)
- **A grade is applied to the display's colours, after the tonemap** (2026-11-13). The table is
  looked up with the tonemapped colour encoded sRGB, what a screenshot holds and a grading tool
  shows, so a grade made from a capture is that capture graded, which the `graded` reference frame
  checks. The tonemapper is fixed (Khronos PBR Neutral), so what a display-space table cannot reach
  is little, and the format is the `.cube` that grading tools write. [Transparency, particles and
  post-processing](rendering-effects.md)

## UI, input and localisation

- **A joystick is named by its device** (2026-11-12). A bound joystick is named by its device's
  GUID, so a binding follows the device wherever it is plugged in, and defaults are unchanged
  ([bindings as settings](ui.md#bindings-as-settings)). Two devices of one model share a GUID;
  telling them apart needs the device's path, which GLFW does not give. [UI, input and
  localisation](ui.md)
- **The UI draws into an overlay the renderer reads** (2026-11-12), not a Vulkan render interface
  for RmlUi inside `render`, nor RmlUi's own backend. The overlay (`render/Overlay.h`) is a plain
  draw list, so RmlUi stays private to `ui`, Vulkan to `render`, and the UI suite reads what a
  document drew without a device. [UI, input and localisation](ui.md)
- **Data models read reflected types in place, from snapshots** (2026-11-12). A screen binds any
  type registered with Reflect, with no code per type, and reads presentation's copy of what a
  snapshot carries, never the world (`SimWorld::ShowInSnapshots`). The model is told only when what
  it shows has changed, so a still screen costs a comparison a step. [UI, input and
  localisation](ui.md)
- **String tables merge key by key, and the language is a setting** (2026-11-13). A mod's table at
  the same path changes the keys it names and keeps the rest, with no patch files. Arguments are
  `std::format`'s, by position, and a translation is held to its base text's arguments as it is
  read, so no translation can make a format throw. The language is the player's setting, chosen at
  the first start from the environment's locale and kept (2026-11-27, the user's); a test's run (the
  smoke test, a fixed frame step) never reads the locale, so it reads the same on every machine.
  [UI, input and localisation](ui.md)
- **A game ships the fonts for its scripts** (2026-11-27, the user's). The engine's fonts (Roboto,
  Hack, Droid Sans) have no Chinese, Japanese or Korean, so Manifest's Japanese table drew as boxes.
  Every face under a game's or a mod's `fonts/` is loaded as a fallback after the engine's, and a
  character no face has is logged and counted a problem. Rejected: the engine shipping a CJK face,
  which would put several megabytes and a third-party licence into every game's assets for the
  scripts one game uses, and a choice of subset only the game can make. [UI, input and
  localisation](ui.md#the-game-ui)

## Audio

- **A mechanism sounds as its clip's events** (2026-11-13). An event sound (`Audio::EventSound`)
  names a simulation event's type or a clip's event (`clamp.closed`), and a clip's is heard from
  where the entity playing the clip is. A part is then heard as it is seen to move, and for every
  entity that plays the clip, not only the player's. [Audio](audio.md)

## Animation and cinematics

- **A sequence blends the point it looks at, not its orientation** (2026-11-13). Slerping the
  orientation from the game's camera to a shot's turned the camera off its subject half-way through
  a blend; moving the point looked at along a line, as the camera's position is, keeps the subject
  in view, and only the up is slerped. The camera still moves on a line, so a rail is drawn to start
  on the game camera's side of what it circles. [Animation and cinematics](animation.md)
- **Locomotion plays in place** (2026-11-25, the user's). The simulation moves a character through
  the character controller; a blend space follows its velocity at a rate matched to its speed, and no
  clip moves the simulation, so animation stays presentation. [Characters](characters.md)
- **Players, cross-fades and layers, chosen by the game's code** (2026-11-25, the user's). No
  animation state machine or graph is built. [Characters](characters.md)
- **The engine's sampler is extended, ozz-animation not adopted** (2026-11-25, the user's). Blends,
  masks and sync markers are small beside the clips, events, cook and reference frames they keep as
  one system. [Characters](characters.md)
- **Gaits keep in step on sync markers the cook finds** (2026-11-25, the user's). Normalised time
  alone needs every clip to have the same steps from the same foot. [Characters](characters.md)
- **Morphing and skinning run in one compute pass** (2026-11-25, the user's), built in the characters
  track and reused by ray tracing, rather than in every pass's vertex shader. [Characters](characters.md)
- **A model may have several mesh nodes** (2026-11-25, the user's), sharing one hierarchy, its clips,
  and shape weights addressed by name. [Characters](characters.md)
- **Faces use ARKit's 52 shape names** (2026-11-25, the user's), so lip sync, expressions and capture
  data serve every face. [Characters](characters.md)
- **Lip sync is made by the cook** (2026-11-25, the user's) with Rhubarb Lip Sync, an outside program
  under MIT, from each line's audio and text, as a clip. [Characters](characters.md)
- **Skin is pre-integrated; hair is alpha-tested cards** (2026-11-25, the user's). Neither adds a
  pass or a buffer to the forward renderer; hair brings the first alpha-tested materials and
  non-opaque ray-query geometry. [Characters](characters.md)
- **The character controller is the engine's** (2026-11-25, the user's), in physics over Jolt's
  `CharacterVirtual`, neutral of genre. [Characters](characters.md)

## Runtime: core and platform

- **spdlog for logging, on `std::format`** (2026-11-10). spdlog is built with
  `SPDLOG_USE_STD_FORMAT`, so the engine carries one formatting library, checked at compile time,
  not spdlog's bundled fmt as well. Engine code logs through `Core::Log`, never spdlog directly, so
  categories and sinks stay the engine's. [Runtime: core and platform](runtime.md)
- **A crash report names its frames in a child process, after the rest is written** (2026-11-13;
  corrected 2026-11-25). The handler takes the frames with glibc's `backtrace` and writes each as a
  module and an offset from `/proc/self/maps` with system calls alone; only then does it run
  `addr2line` per module by `vfork` and `exec`, ended by SIGALRM if it hangs. Naming frames in the
  handler would need a library that reads DWARF without allocating, and a release build has no line
  tables anyway; without a symboliser, `scripts/symbolise-crash.sh` names the offsets against the
  build. [Runtime: core and platform](runtime.md)
- **A crash report stays on the machine until players outside development have the game**
  (2026-11-13). Each module's GNU build ID in the report, and an upload on the next start with the
  player's consent to an endpoint speaking Sentry's protocol, are the plan, not built
  ([J11](roadmap.md#j11)). Crashpad and sentry-native are not used, since they would replace the
  handler with minidumps, unless the frames and the log's tail prove not to be enough. [Runtime:
  core and platform](runtime.md)

## The app shell and tools

- **The engine's shell is generic; gameplay is the game's** (2026-11-18, the user's). The shell
  (`app`) gives what any game needs: the window, renderer, game UI and console, saves, mods and
  settings, input, the developer views, and a camera and controlled-entity seam a game drives.
  Gameplay is composed into it through `App::Game` and built on the engine's neutral modules
  (`transit`, `dock` and the rest). A shell with a game of its own and a neutral mode would leave
  every other genre carrying and switching off a game it does not play. [The app shell and
  tools](app-shell.md)

## Build and platform

- **C++20** (2026-11-10). `std::expected` and `std::print` are not available; [coding
  standards](coding-standards.md#errors) say what is used instead. [Build and
  gates](build-and-gates.md)
- **Linux only, on x86-64** (2026-11-10): Wayland for the desktop, X11 for the headless tests, one
  toolchain (Clang 21 from the devenv shell). [Build and gates](build-and-gates.md)
- **Windows comes near the end of this development phase** (2026-11-25, the user's). Until then the
  code is designed for Linux alone, with no compromise made for a Windows build; platform code stays
  behind `platform`, and every dependency chosen builds on Windows, so the port is work, not a
  redesign. [Scope](scope.md)
- **x86-64 with AVX2 is the minimum CPU** (2026-11-10): Intel Haswell (2013), AMD Excavator (2015)
  and later. Jolt adds its AVX2 flags publicly, so they reach `physics`, which links it privately,
  and nothing above it; the engine accepts that rather than building Jolt for an older instruction
  set. Raising the minimum further (AVX-512) is a new decision. [Build and
  gates](build-and-gates.md)
- **KTX-Software stays on v4.4.2** (2026-11-11). Its newest tag is still v5.0.0-rc2, and v4.4.2
  reads every file the engine needs (KTX 2, Basis Universal ETC1S and UASTC, zstd supercompression);
  revisit when v5 is released. [Build and gates](build-and-gates.md)
- **Vulkan 1.4** (2026-11-25, the user's; built in J36). Every device with mesh shaders and ray
  queries reports 1.4 on a current driver, and it makes scalar block layout and push descriptors
  required. Devices whose drivers stop at 1.3 (GCN 1 and 2, Mesa before 25.0) can no longer run the
  engine. [Dense geometry and ray tracing](dense-geometry-and-ray-tracing.md)
- **No dependency under a licence that forbids open-source use** (2026-11-25). NVIDIA's RTX SDKs
  licence (NRD, RTXDI, RTXGI, RTXMG) forbids use that would put the SDK under an open-source licence,
  and FidelityFX 2 ships as binaries only; FidelityFX v1.1.4's MIT shader sources are the denoisers'
  starting point.
  [Coding standards](coding-standards.md#dependencies)

## Process

- **The engine is genre-neutral, and its examples are separate** (2026-11-15). Jettison provides
  capabilities for several kinds of game; a game's rules, content and policy are its own, and the
  engine's samples live in sample and test targets the engine cannot depend on. It supersedes older
  decisions that scoped the engine to one game. [Game integration](game-integration.md)
- **The space setting is Manifest's** (selected 2026-11-22, done 2026-11-23, the user's). The engine
  keeps a concept only when it has a neutral name and is useful in more than one genre; the space
  sample was retired, and Manifest tests all space code. The configure's dependency checks and the
  `neutral_words` suite hold the boundary. [Game integration](game-integration.md#enforcement),
  [samples](sample-game.md)
