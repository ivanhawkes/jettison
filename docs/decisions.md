# Decided

The engine's settled decisions, each with the date it was settled and why. The
[roadmap](roadmap.md) names the work, and [open decisions](roadmap.md#open-decisions) the ones not
yet settled.

The 2026-11-15 genre-neutral engine and separate-example contract in
[game integration](game-integration.md) supersedes older game-specific scoping decisions
where they conflict. Historical mechanics remain supported example/extension behaviour
until deliberately migrated; they are not mandatory semantics for every game.

Settled on 2026-11-10:

- **C++20.** The language standard stays where it is. `std::expected` and `std::print` are not
  available; see [coding standards](coding-standards.md#errors) for what is used instead.
- **Linux only**, on x86-64: Wayland for the desktop, X11 for the headless tests, one
  toolchain (Clang 21 from the devenv shell).
- **spdlog for logging** (catalogue entry 3).
- **Clustered forward rendering.** Lights are assigned to view-space clusters in compute, and
  every surface is shaded once in its own material program. There is no G-buffer; SSAO, when it
  came (10c), reads the depth prepass's depth and normals. Phases 4, 5
  and 10 are planned around it (catalogue entries 17, 19 and 28), and
  [architecture](architecture.md#rendering-clustered-forward) has the frame and the rules it sets.
- **KTX-Software stays on v4.4.2** (2026-11-11, phase 5a). Phase 5 was to move to v5 if it was out
  of release candidate; its newest tag is still v5.0.0-rc2. v4.4.2 reads every file the engine
  needs (KTX 2, Basis Universal ETC1S and UASTC, zstd supercompression). Revisit when v5 is
  released.
- **Ambient occlusion is off on a CPU device by default** (2026-11-13, phase 10c). It runs the depth
  prepass, which now shades two multisampled targets as well as laying down the depth: on lavapipe
  at 640 × 480 the dressed orbit scene's prepass took 11.2 ms and its frame 53 ms, against a budget
  of 50 ms and 44.2 ms without it. Hardware has it on, at 0.16 ms with the prepass from the orbit
  scene's 1.5 km on the RTX 5060 Ti. The smoke test and the reference views therefore draw without
  it, but for the post chain's bench, whose view asks for it.
- **A pad's clamps are presentation's, and sound as a clip's events** (2026-11-13, phase 10d). The
  docks clamp a ship as a kinematic body the moment it is within reach (9c), and nothing in the
  simulation waits on a clamp; the dressing draws its pad's clamps closing over the next two seconds
  from the snapshots, which show each ship's `Docking` and `Hull` for them, and opening as it is let
  go, its jaws clear of the hull 0.3 s after it starts to lift. The docking sounds moved from the
  docks' `Landed` and `Left`, which the shell heard for the player's ship alone, from the station, to
  the clip's `clamp.closed` and `clamp.released`, heard from the pad for every ship: the jaws are
  heard biting as they are seen to, and a trader docking beside the player is heard too.
- **The cluster grid is 32 × 18 × 48** (2026-11-11, phase 5e), up from 16 × 9 × 24, with the
  index list at 32 entries a cluster on average and the cluster range at 2 km. Against 265 lights on
  the orbit scene's station, the opaque pass fell from 0.43 ms to 0.34 ms on an RTX 5060 Ti and from
  47 ms to 36 ms on lavapipe, with the assignment no dearer; 16 × 9 × 48 and 32 × 18 × 24 each
  gained about half that. The list's busiest frame, from 20 m, used 16 entries a cluster. The cluster
  range was not varied: every light measured was within 200 m.
- **The depth prepass is kept, and off** (2026-11-11, phase 5e). Measured on `orbit.yaml`, it
  cost more than it saved on both devices: 0.023 ms for 0.016 ms of the opaque pass on hardware,
  3.4 ms for 0.5 ms on lavapipe, where front-to-back batches already let the depth test reject what
  is behind. It is the place ambient occlusion's normals and the outlines' object IDs are written
  (10c), which turn it on.
- **A CPU device gets RGBA8 textures, not BC7** (2026-11-11, phase 5e). Lavapipe offers BC7 and
  decodes it in software at every fetch: the orbit scene's opaque pass took 469 ms at 640 × 480 with
  the hull plating in BC7 and 23 ms in RGBA8. The gate's smoke test therefore uploads RGBA8;
  transcoding to BC7 is covered by the render suite without a device, and uploading it on hardware.
- **Stencil and an object ID buffer, both** (2026-11-11, for phase 10c). The stencil, on the depth
  target as `D32_SFLOAT_S8_UINT` (a float depth, which reversed-Z needs; `D24_UNORM_S8_UINT` would
  throw its precision away), is the cheap mask: the hardware tests it before shading, for crisp
  outlines and for keeping post-processing off the cockpit and the HUD. It costs about 8 MB at
  1080p with 4× MSAA, barriers over both aspects, and a sample-zero resolve for the post passes.
  The ID buffer, written by the depth prepass rather than by every material program, is the
  flexible one: any pass can read it, and wide, soft target highlights are drawn from it. Its cost
  is the prepass running whenever outlines are on, 0.02 ms on hardware as measured in 5e.
- **Screen-space reflections stay out** (2026-11-11, reconsidered with SSAO and bloom brought in).
  SSAO needs the prepass's depth and normals; SSR would also need each pixel's roughness and
  metalness, and a colour of the scene to march through, which is most of the G-buffer clustered
  forward was chosen to do without. What it adds is reflection of what is on screen, and its
  failure, where the reflected ray leaves the screen or goes behind something, falls back to the
  environment cube: against black space that seam is plain to see. The reflection actually missing
  from a hull is the planet, which is not in the environment's capture; adding the planets to it
  (10a) is stable and right off screen too.
- **The spatial index is an octree** (2026-11-11, phase 6c), not a sorted grid, and not loose. Both
  were measured over a system's traffic (`spatial/tests/SpatialBenchmark.cpp`: 60% of ships within
  60 km of eight stations, 30% on the lanes between them at up to 300 km/s, the rest scattered),
  stepped at 60 Hz, on a Ryzen 9 3900X in Release. At 100,000 ships the octree (leaves of 64) took
  0.74 ms a step to update every ship, 1.4 µs for a 5 km avoidance query, 4.6 µs for eight nearest
  neighbours and 2.4 µs for the nearest ship to a point in open space; the best grid for each, its
  cells from 1 km to 1,000 km, took 1.1 ms, 2.9 µs, 36 µs and 64 µs. A grid finds a neighbour by
  growing its search through cells, which in open space are almost all empty, and pays for cells
  small enough for avoidance in updates (4.4 ms at 10 km). It was ahead on the 500 km sensor sweep,
  43 µs to 52 µs, and, by a tenth of a microsecond, on avoidance at 10,000 ships and fewer. Loose
  cubes (1.5 and 2 times the cube) made every small query 2.5 to 4 times slower, since they overlap,
  and the update no cheaper at 60 Hz; at steps of a second they saved a quarter of it. Range results
  past 512 are radix sorted: sorting a sweep's 7,500 entities was four fifths of the query.
- **A jump reads its files on the I/O worker and parses them on the main thread** (2026-11-11,
  phase 6d). The archives read EnTT's meta context, and a type registered on the main thread while
  the worker resolves another inserts into the table it is reading; nothing in it guards against
  that, and registration is lazy throughout the engine (every reader registers its types on first
  use). A system file and a scene file are a few kilobytes of YAML; the megabytes a destination
  needs, its models and textures, decode on the worker as they always have, and touch no reflection.
- **A jump point is the end of one lane** (2026-11-11, phase 6d), so a gate says where it leads, and
  flying through it needs no choice of destination. A system with two ways out has two gates.
- **Physics is simulation state, a Jolt world per star system, on one thread** (2026-11-11, phase
  7a; a world per frame of each system since 7b, below). Jolt's state is saved inside the world's save, as a service of the world, rather than
  rebuilt from components on a load: the contact cache each step warm-starts from is Jolt's alone,
  and a world rebuilt without it steps on differently from the world saved. A world per system
  rather than one with a filter per system, since every system's coordinates start at its own
  primary and their bodies would share one broad phase; 0.7 MB each at the default capacities.
  Body identifiers are chosen by the engine, the lowest free index with a saved sequence number,
  because Jolt's free list is history a save does not hold and its results depend on them. Jolt's
  job system is single-threaded, as the threading rules have the simulation.
- **Physics runs in frames, a Jolt world per frame of each star system** (2026-11-11, phase 7b),
  revising 7a's world per system. A frame is the system's own or the local frame of a body on rails,
  a station or a planet, translating with it; Jolt's positions are metres from the body and its
  velocities relative to its motion. In the star's frame, a ship flown at a station moving at
  30 km/s about the star (500 m a step) could not be swept against it: Jolt sweeps a fast body by
  their relative motion, but finds what to sweep against along the body's own absolute motion
  (`PhysicsSystem::JobFindCCDContacts` casts the broadphase with it), so a ball fired at 240 m/s at a
  panel beside it passed through, swept or not. Its broadphase bounds are floats, 8 km apart at
  150 million kilometres, and a float velocity at 30 km/s is 4 mm/s between values: a ship let drift
  beside the station for a minute moved 1.8 m in the star's frame, and 5 mm in the station's. The
  frame's change of speed between steps is taken off its dynamic bodies, which telescopes, so the
  rounding of the difference of two positions a hundred and fifty million kilometres out does not
  accumulate. One frame per system for its busiest place would not do: two stations about two
  planets are 30 km/s apart. Bodies in different frames do not meet, and a body is put in a frame by
  the game (carried forward).
- **Gravity is physics', from the star system's bodies** (2026-11-11, phase 7b). Orbits stay on
  rails; dynamic bodies are pulled by every body with a gravitational parameter, as point masses,
  evaluated as each step begins. Without it a ship beside a station on its orbit falls away from it at
  the planet's 8.6 m/s², 15 km in a minute, and an assist holds it there only by thrusting against
  the planet all the while.
- **Contacts are the simulation's, from Jolt's contact cache** (2026-11-11, phase 7c). The event
  queue is presentation's, and nothing in the simulation reads it back, while what a contact means
  (a ship docked, a hull damaged) is decided in the step. So the physics world keeps them: what the
  last step began and ended and what touches now, saved with the world, so a system ordered before
  `physics` reads after a load what it read before the save. Whether two bodies touch is asked of
  Jolt's cache once the step is over (`PhysicsSystem::WereBodiesInContact`) rather than counted from
  its callbacks: Jolt adds and removes a contact per pair of sub-shapes (a hull across two of a
  mesh's triangles is two), reports a continuous body's swept contact without caching it until the
  next step, and drops a sleeping body's contacts though it lies where it was. A contact added in the
  step counts, and a pair that both sleep still touches. A sensor is kinematic to Jolt and never
  sleeps, since a static sensor sees only bodies that are awake, and a ship come to rest in a docking
  zone would leave it as it slept.
- **Picking is a command, answered by an event** (2026-11-11, phase 7c). Presentation reads
  snapshots and events and never the simulation's state, which keeps holding once the simulation
  leaves the main thread; so a pick is `Physics::Pick`, applied as the next step begins and answered
  among its events, a frame later, which a click does not notice. The ray is drawn from what
  presentation shows, half a step behind the simulation on average, and a station's frame moves
  500 m a step about the star, so the pick carries the time it was drawn at and each frame is placed
  there for the cast: without it a click on the station misses by the distance between the two. A
  pick is in a recording like any command, and a replay raises its answer again. The cost, a paused
  world answering when it resumes, is carried forward.
- **Migrations are a chain of steps over a document** (2026-11-12, phase 8a), one step a version,
  written once and never changed, rather than readers that know every old layout: a step reads the
  data its version wrote and nothing else, so a format's history is a list of small edits, each
  tested by the fixture of its version. A binary step edits the archive's table of names and its
  payload without types, which serves a rename and an addition at the end; one that needs the types
  keeps the old ones registered under names of their own and reads with them. Every part of a save
  is versioned on its own (the save game, the world, each of the game's sections), so a change to one
  is a step in that one's chain. Saves written by earlier versions are kept as fixtures and never
  regenerated; `world-v1.save` was written by the phase 6d build.
- **Definitions migrate** (2026-11-12, phase 8a, the decision entry 13 left open). A definition is
  authored content that mods ship, and a mod's author is not there when the game changes a
  definition's format; refusing every file of an older version would break every mod at each change.
  A type's YAML migrations are run on its files as they load, and cost nothing for a file that is
  current. Saves hold no definitions, only their names, so a definition's format can change without
  touching a save.
- **Saves and settings are written durably** (2026-11-12, phase 8a, the fsync entry 10 left open).
  A whole-file write flushes the partial file before the rename and the directory after it: without
  the first, a power cut after the rename can leave an empty file where the save was, since the
  rename can reach the disk before the data; without the second, the rename itself may not. Saves and
  settings are small (4 KB for the orbit scene's world) and written when the player asks, so the two
  flushes cost nothing anyone waits on.
- **A joystick is named by its device** (2026-11-12, phase 8a). A binding's joystick is a number; one
  the player has bound is named after the GUID of the device that answered to it then, and from then
  on is that device wherever it is plugged in, and nothing while it is not. Numbers no name claims
  are the unclaimed devices in slot order, which is GLFW's slots when nothing is named, so defaults
  and a player who never rebinds see no change. Two devices of one model share a GUID, and a name
  takes the first of them free; telling them apart needs the device's path, which GLFW does not give.
- **The UI draws into an overlay the renderer reads** (2026-11-12, phase 8b), rather than a
  Vulkan render interface for RmlUi inside `render`, or RmlUi's own Vulkan backend. The overlay is a
  plain draw list in render's public headers (vertices, indices, draws with a translation, a scissor
  and a transform, and textures by identifier), so `ui` depends on `render` for its CPU side, RmlUi
  stays private to `ui`, Vulkan to `render`, and the UI's suite reads what a document drew without a
  device. RmlUi's backend sample makes its own allocator, swapchain and pipelines; the overlay is
  drawn in the output pass with the renderer's pipeline cache, VMA and frame slots. RmlUi's geometry
  is copied into the overlay each frame, as ImGui's is: a few thousand vertices for a screen. Colours
  are multiplied and filtered as encoded, as a browser does, and decoded once before an sRGB
  swapchain writes them, so blending is in linear light; a browser blends encoded values, which
  leaves the half-covered edge of light text on dark a little brighter here.
- **Data models read reflected types in place, from snapshots** (2026-11-12, phase 8b). A screen
  binds any type registered with Reflect, read through the field addresses the registration now
  records, with no code per type: the reflection the archives, the console and the definitions use
  is the UI's too. What a screen reads is presentation's copy of what a snapshot carries, never the
  world, which keeps holding once the simulation leaves the main thread; a world shows a component
  to presentation by name (`ShowInSnapshots`), and its copy is taken with the transforms at each
  capture. A screen's data is built from a snapshot when a step brings a new one and the model told
  only when what it shows has changed, so a still screen costs a comparison a step.
- **Superseded** (2026-11-15, by [game integration](game-integration.md): the example is not grown
  from Manifest's plan or kept in step with it). **The sample game is Manifest's plan** (2026-11-12, phase 8b; the project was the Civilisation
  Simulation until it was renamed the same day). The shell plays the game's part from the plan in
  `~/projects/manifest`, the local copy over the GitHub one, slice by slice as the engine systems that each needs arrive
  ([sample game](sample-game.md)). Its rules are the shell's, standing in for the game's, and none
  of them is engine code.
- **The GPU drawn with is the one with the monitor** (2026-11-12). A machine may have a second card
  kept for compute with no monitor on it, and on Wayland that card still says it can present,
  through the compositor's copy across the bus; taking the first device Vulkan lists that has the
  features, as the renderer did, could draw a game on it. So a device is ruled out for what it cannot
  do, each reason logged, and of the rest the one whose DRM card has a connector reading `connected`
  (`VK_EXT_physical_device_drm`, then sysfs) is chosen, then by type, then in Vulkan's order, which
  Mesa's device-select layer puts the display's GPU first in. On the development machine, a 5060 Ti
  with the monitor and a 5070 Ti for inference, the 5060 Ti is chosen on Wayland, and lavapipe under
  Xvfb, where neither card can present. A laptop's monitor on its integrated GPU takes the integrated
  GPU, as the device-select layer would; `--gpu` takes the discrete one.
- **A run with no display draws offscreen, on GLFW's null platform** (2026-11-20). NVIDIA's driver
  cannot present to Xvfb and has no `VK_EXT_headless_surface`, and the desktop is not to be given
  windows or keys, so the frame is drawn into images of the renderer's own with no surface, on a
  window that has a size and callbacks and no display; the swapchain stays for every run with a
  display. The gate's plain smoke test draws so on the GPU; CI, the reference frames and the
  sanitised run stay on lavapipe under Xvfb, through the swapchain, and one reference view holds the
  offscreen frame to the swapchain's byte for byte. On the user's workstation the smoke test pins the
  5060 Ti by name, so the 5070 Ti kept for inference draws nothing whatever the ranking says.
- **x86-64 with AVX2 is the minimum CPU**: Intel Haswell (2013) or AMD Excavator (2015) and
  later. Jolt adds its AVX2 instruction-set flags publicly, so they reach every target that links
  it, and the engine accepts that rather than building Jolt for an older instruction set
  (catalogue entries 6 and 22).

- **The simulation's jump is the engine's, in `ai`** (2026-11-12, phase 9a). What sends a ship along
  a lane (a pilot through a gate, a trader's route) is the game's; carrying it is the engine's
  (`Ai::LaneTravel`): the command, the component a save holds, and the system that brings it out,
  after physics, so physics removes its body the step it enters and makes it again the step after it
  arrives. A transit is whole steps, counted from the clock's step rather than compared in seconds,
  so it is the same every run however its seconds round. `ai` depends on `physics` for it, as 9b's
  steering does.
- **A ship comes out of a gate at rest, in its station's frame, facing the planet** (2026-11-12,
  phase 9a). A ship's speed carried through a lane would send one that went into a gate at speed out
  of the far one at a station a few kilometres ahead of it. So a ship arrives still relative to the
  station the gate keeps by, in that station's frame, where it can meet and dock with the station
  without changing frames, turned the way the gate faces: at the planet it orbits, or else the
  nearest planet, or else the nearest station (`Celestial::JumpPointFacing`). The gate's ring is
  drawn facing the same way. A gate of no station is its own frame.
- **Gates keep station by their stations** (2026-11-12, phase 9a). The sample's gates were hundreds
  of millions of kilometres from any station, days at a ship's 150 m/s. Each now orbits its station
  in the station's plane, at its phase and with its period (given, since a station has no gravity to
  derive one), so it stays straight out from the planet beyond the station: Halcyon Gate and Vesper
  Gate 5 km out, half a minute's flight, and Vesper Deep Gate 6 km out at 30° ahead along the orbit,
  so a ship coming out of either sees the planet with the station before it. In a station's frame,
  which does not turn, a gate moves at the orbit's rate times its distance, 1.1 m/s at Corva
  Refinery, which a ship at rest drifts from. Marrow, with no station, has its gate three of
  Ashfall's radii out, where the world is 40° across.
- **A route is over jump points, with the crossings the caller's** (2026-11-12, phase 9a). A graph of
  systems joined by lanes would make a route of two short jumps across a system whose gates are a
  long way apart cheaper than one long jump. Each jump point is a node instead, its lane an edge at
  the transit time and every other gate of its system an edge at what crossing costs, which the
  galaxy cannot say (it has no positions) and the caller can: `CrossingByCourse` makes it from a
  course through the system's waypoints at a speed.
- **Steering is velocities, which flight assist holds** (2026-11-12, phase 9b). The stub had each
  behaviour return an acceleration to be summed and clamped by the flight model. But assist already
  takes a velocity and reaches it at the thrusters' limits, axis by axis, so a behaviour that is a
  velocity is what assist flies, and the flight model, not steering, is what knows how fast the ship
  can change it. Arrival and the limit before an obstacle brake at half the weakest thruster, which
  leaves the rest for the turn and for a dodge, and the last metres of each are at the distance a
  second: √(2ad) alone, its slope unbounded at the end, was overshot a step at a time.
- **An obstacle is the sphere that holds its shape, and the guarantee is the braking** (2026-11-12,
  phase 9b). Avoidance finds what is near through the spatial index, not Jolt: the index answers for
  a whole system in identifier order, and a sphere's closest approach is arithmetic, where a cast a
  ship each step is a query into every frame. The turn aside from the first threat takes a ship
  round; what keeps it off any hull is that it never closes on an obstacle faster than it can stop
  before the clearance, which holds whatever the turn does. Twelve ships converging on a station kept
  31.8 m between hulls and 25 m from its sphere.
- **The spatial index is made from the components each step** (2026-11-12, phase 9b). It is not state:
  a save does not hold it, and a world loaded from one has it from its next step. Kept from the
  simulation, by the system `spatial` as the step's systems begin, it is in `spatial`, which now
  depends on `sim` (a sibling edge in the World tier), so that a sensor or a game system reads it as
  steering does. Steering orders itself after it and before `flight`, through the scheduler's new
  `before`: physics cannot name steering, which is above it.
- **Docking is the engine's, as pads a station gives** (2026-11-12, for 9c; the question entry 22
  left to 9c's behaviour trees). Stations are to be large enough to land a dozen of the largest
  transports, each on a pad of its own, and the game is to be multiplayer in time, so who holds a
  pad is authoritative state that every player sees the same: simulation state, saved, given by
  commands in their order, in a module of its own (`dock`, entry 31), before the traders that
  would otherwise be written against the shell's one port.
- **Hulls and pads at the middle of the scales weighed** (2026-11-12, for 9c). A transport up to
  60 m, the largest transport 140 m and the display ships 300 m. Twice that was chosen first and
  set back the same day: its twelve-pad station was three kilometres across, past the cluster range
  (2 km, entry 19), with the gates to move out and a buggy's trip from bay to pad minutes long. At
  this scale a twelve-pad station, four piers of three, is about a kilometre and a half across,
  within the cluster range from its centre, and the gates at 5 and 6 km stay beyond its piers.
- **A ship comes down onto its pad from outside its station's sphere** (2026-11-12, phase 9c).
  Steering already takes a ship round a sphere, so a ship flown to a pad is flown first to the point
  above it, outside the station's sphere along the pad's normal, round the sphere by a course
  (`Ai::Waypoints`); and only then, inside the sphere, kept clear by its hull, straight down. A
  course over the station's own hull is not plotted: a layout is checked as it is defined to leave
  the column over every pad clear, out to the sphere, and the shape cast along the way is what
  stops a ship short of anything there regardless. A shuttle 10 km out on Highport's far side is
  over its pad in 111 s and clamped 24 s later.
- **A docked ship is a kinematic body on its station's rails** (2026-11-12, phase 9c), rather
  than a dynamic one held by its assist or a constraint to the deck. On rails it is where its pad
  is at every step, exactly, and is placed the same in every build; it meets nothing kinematic, so a
  station's deck and the ships parked on it make no contacts; and what holds it is its
  `Physics::OnRails`, saved like any component. Let go, it is dynamic again where it lay, at rest
  in the station's frame. The one contact undocking makes is the hull on its pad as it is let go.
- **Saves from before the docks are refused** (2026-11-12, phase 9c). The shell's world held its
  docking port (`Shell.DockingPort`), which the docks replace, and a world refuses a component it
  does not register; its section held the port too. The section is version 2, without it, and a
  save of 9b's is refused by name rather than loaded into a world with no pads. The engine's own
  formats are unchanged in layout: the world's fixtures load as they did.
- **Cargo is carried in the simulation** (2026-11-12, for 9e). The buggies that carry a trade's
  crates between a station's bays and a ship's hatch decide when the ship can leave, so they are
  simulation state, deterministic and saved, rather than presentation's show of a trade already
  done; every player sees the same buggy at the same place.
- **A display pad's ship is presentation's** (2026-11-12, phase 9d). No ship is given a display pad
  (scope), and the ship it shows never moves, trades or docks, so a body there would be state every
  save carries for nothing. The layout names the hull it shows (`Dock::PadLayout::ship`), checked to
  be on a display pad alone, and the dressing draws that hull's proxy on it; the docks still make
  the pad an entity, shown on display, and refuse it to `Park`.
- **The dressing reads the pads from the snapshots** (2026-11-12, phase 9d). A pad's lamp shows its
  state, which is the simulation's, so the docks show `Dock::Pad` in every snapshot
  (`Docks::Attach`) and the lamps are turned from the latest one, as the market screen is from its
  market; the shell finds a station's simulation entity from its celestial body only when its pads
  are not yet among them, as when a world is loaded over the one it was found in.
- **A bay is the middle of its door's sill, on the deck** (2026-11-12, phase 9d), rather than the
  middle of its door: a buggy drives through it there, so the road graph of 9e has its nodes on the
  deck the buggies run on. Highport's and Corva's wall doors moved down onto their piers' decks.
- **A draw under a pixel across is not drawn** (2026-11-12, phase 9d). The view leaves out a draw
  whose breadth (its model's radius times its transform's middle scale) covers less than a pixel at
  its nearest point, as it leaves out one behind a horizon (`Render::TooSmallToSee`). The middle
  scale rather than the largest, so a long, thin stroke of a pad's paint goes when it is a sliver,
  and rather than the smallest, so the thin plate it is painted on stays. What is left out draws
  nothing a viewer could tell from its absence, and on lavapipe each of its triangles shaded a block
  of the lit program: from 1.5 km at 640 × 480 it leaves out 529 of Highport's 925 meshes and takes
  the opaque pass from 31 ms to 22 ms. Shadow casters are not culled by it; a skinned draw never is.
- **A CPU device filters without anisotropy** (2026-11-12, phase 9d), as it is given RGBA8 rather
  than BC7 (5e): it takes each tap in software, and at 16x on the display wing's round hulls, seen
  at grazing angles through the triplanar program's nine fetches a pixel, the orbit scene's opaque
  pass on lavapipe was 59 ms against 22 ms without. Hardware filters at 16x.
- **A station's roads are made from its layout** (2026-11-12, phase 9e), rather than drawn in it:
  a road down each pier's side, legs joining each bay and pad to it and to each other where the deck
  allows, and each pad routed once by A*. A layout already says where its decks, pads and bays are,
  and roads written beside them would have to agree with all three; made from them, a mod that
  moves a pad moves its road, and a layout whose pad no bay reaches is refused as it is defined.
  The graph is the layout's, rebuilt as it is defined, and a buggy's route in a save is its nodes,
  which the same layout makes the same.
- **A buggy is out only while it carries** (2026-11-12, phase 9e). It comes out of its pad's bay
  when the pad's ship has cargo for it and goes back in when there is none, an entity only while it
  is on the deck, so a station at rest holds no buggies in its state and costs nothing a step, and
  a pad's buggies are a cap on how many are out at once. Parked buggies are the dressing's, never
  the simulation's: a station whose buggies do not work (`DeckTraffic::working`) moves its cargo at
  the rate working ones would, a load each way at each round trip of the pad's road shared among
  them, so the choice changes what a station costs and not how long it takes, within a load.
- **Buggies give way at nodes by holding them** (2026-11-12, phase 9e). A buggy holds a node while
  it is within 6 m of it, before or after, and one coming up to a node another holds stops short of
  its reach; in a lane it keeps 8 m behind the buggy ahead; the lanes are 4 m apart, so two buggies
  pass in opposite lanes. Who holds a node is where the buggies are, not state of its own, and of two
  that reach for it in one step the first in entity order has it. Every edge is at least twice the
  reach, so a buggy never holds the node ahead while it holds the one behind, and no ring of
  buggies can each wait for the next: twelve pads loading at once at Highport finish.
- **A ship is held until its cargo has crossed** (2026-11-12, phase 9e): until every crate for it
  is aboard and every crate off it has been taken from its hatch, not until the last has reached the
  bay. The trade settled the crates as it was made, so what holds the ship is its hatch: crates on a
  buggy on its way to the bay are the station's to bring in.
- **The simulation's agents act through commands applied at once** (2026-11-12, phase 9f). A task
  of a tree sends the commands a player would (a pad asked for, a trade, a lane entered), and
  `Sim::SimWorld::Apply` applies each as it is sent, through the handler the queue's go through, so
  it meets the same checks and the docks answer an agent's request and a player's alike. Not queued
  for the next step: the queue is what a session records, and a replay feeding back the agents'
  commands would have them twice, since the replaying world's trees send them again. The trees run
  first in the step, so an agent's commands find the world where the queue's do.
- **Behaviour trees resume their running child, and nothing interrupts it** (2026-11-12, phase 9f).
  A sequence or a selector goes back to the child that was running rather than ticking its children
  again from the first, and no node leaves a running one, so a task that sent a command and waits on
  its answer is never left half done and needs no undoing, and an entity's place in its tree is a
  memory and a flag a node, saved with the world. A repeat or a loop starts its child's next run at
  the next tick, so a step's work is bounded. A tree that must notice something mid-task wants
  reactive nodes, which halt what they leave ([carried forward](roadmap.md#carried-forward)); none is built
  until a game's tree needs one.
- **Multiplayer in time; single-player first** ([scope](scope.md), 2026-11-12). No networking is
  built yet, but what the simulation holds is planned for several players: commands carry who sent
  them, shared facilities (pads, queues, bays) are given by the step in command order, and nothing
  in a simulation module assumes one player's ship.
- **Pack files are compressed with zstd, one copy shared with libktx** (2026-11-13, phase 11a, the
  open decision of phase 5). libktx compiles its own zstd 1.5.5, with public `ZSTD_*` symbols, in
  v4.4.2 and in v5.0.0-rc2 alike, and has no option for another. The engine builds zstd 1.5.7 once,
  the library alone, and `ktx_read` is built against it: its copy is taken out of the target's
  sources and its directory off its include path in `source/external/CMakeLists.txt`, with no patch
  to libktx's files, since it calls the stable API alone (`ZSTD_decompressDCtx` and the error codes).
  One `ZSTD_versionNumber` is in the executable, and the planets' zstd-supercompressed textures draw
  as before, the reference frames unchanged. A file in a pack is its own frame, compressed only
  where that pays (vfs/Pack.h); LZ4, the fallback, would have been a second compressor for the same
  job.
- **Mods are directories or packs in the user's data directory, ordered by name** (2026-11-13, phase
  11a; the question [scope](scope.md) left for packaging). A mod is a directory, for its author, or a
  pack, for its players, under `mods/` in the user's data directory, mounted between the base and the
  user's own files in the order of their names' bytes, so the order is the same on every machine and a
  player sets it by naming. Not zip archives: a pack is a few hundred lines the engine owns, reads a
  file without reading the rest, and checks each file it reads, where a zip reader would be another
  dependency for the same thing. A list of enabled mods, in the settings, is the game's to add. Since
  12a the order is the player's list's (`mods.yaml`, below), name order standing only for the mods
  it does not yet name, and a pack's name is its identifier without `.pack`.
- **Definitions layer by patches, merged as YAML** (2026-11-13, phase 11a, entry 13's finer
  layering). A mod's `<name>.patch.yaml` beside a definition changes what it names over every layer
  below it, and a whole file still replaces one: merging the YAML before it is read, rather than the
  read values, keeps one reader for whole files and patches, with the errors and lines it already
  gives, and lets a patch say what a value cannot (a field removed, a list appended to, its elements
  merged by a key). Each layer migrates on its own, so a YAML step edits only what the data has.
- **WAMR, not wasmtime, runs the scripts** (2026-11-13, phase 11b, the open decision of phase 11).
  WAMR is C, built from its embedding script inside the engine's own build with the engine's
  toolchain and sanitisers, a few hundred kilobytes; its fast interpreter counts instructions
  (`WAMR_BUILD_INSTRUCTION_METERING`), which is the deterministic fuel the simulation needs, and its
  thread manager lets a watchdog stop a call. wasmtime's fuel is as deterministic and its Cranelift
  code much faster, but it is Rust built by cargo, or a prebuilt C library outside the sanitised
  build, and a JIT whose speed nothing needs yet: a handler call costs 0.27 µs and an input call 9 to
  10 µs, almost all of it the fresh instance (Release, the development machine). WAMR is built with
  bulk memory and reference types, which clang's and rustc's defaults use, software bounds checks
  (no signal handler of its own under the sanitisers or 11d's crash handler), and no libc or WASI:
  a script imports nothing but the host's functions. Revisit should a profile find scripts dear;
  WAMR's ahead-of-time compiler is the next step before another runtime.
- **Scripts are C or Rust, the shell's Rust** (2026-11-13, phase 11b). Both compile to wasm32 with
  the devenv's toolchain (clang and lld's wasm-ld, and nixpkgs' rustc, whose `wasm32-unknown-unknown`
  library comes with it), against bindings generated for each from the same layouts. The shell's own
  scripts, the smoke mod's among them, are Rust; the script suite runs its host API module in both
  languages, so C stays proven.
- **An input call starts from the module as it was instantiated** (2026-11-13, phase 11b). An input
  is answered inside the step, so its answer may depend on the option's inputs and on nothing a
  script kept, which a save does not hold and a replay would have to reproduce; an instance made per
  call costs 9 to 10 µs, and one without a host heap (an input is given nothing to copy in) makes it
  four times cheaper than with one. A handler, outside the step, keeps its instance. A trap is not a
  script's end, since it would do the same again: only a call past the time limit stops a script,
  the one outcome that depends on the machine.
- **A grade is applied to the display's colours, after the tonemap** (2026-11-13, phase 11c). The
  table is looked up with the tonemapped colour encoded sRGB, what a screenshot holds and a grading
  tool shows, so a grade made from a capture is that capture graded, which the `graded` reference
  frame checks against the table on the CPU. A table over the HDR colours before the tonemap would
  grade highlights the tonemap then compresses, but it needs a shaper into the table's domain and
  cannot be checked against a screenshot; the tonemapper is fixed (Khronos PBR Neutral), so what a
  display-space table cannot reach is little. The format is the `.cube`, which Resolve, Photoshop
  and Nuke write.
- **A sequence blends the point it looks at, not its orientation** (2026-11-13, phase 11c). Slerping
  the orientation from the game's camera to a shot's turned the camera off the ship half-way
  through the docking sequence's first blend, the hull at the frame's edge; moving the point looked
  at along a line, as the camera's position is, keeps a shot's subject in view through a blend, and
  only the up is slerped. The game camera's point is along its view, as far as the shot's subject is.
  The camera itself still moves on a line, so a rail is drawn to start on the game camera's side of
  what it circles.
- **A crash report names its frames in a child process, after the rest is written** (2026-11-13,
  phase 11d). Naming a frame in the handler means reading the executable's symbols and DWARF there,
  a library (libbacktrace, cpptrace) to read them without allocating, and a release build has no
  line tables anyway. So the handler writes what it can without help, every frame as a module and an
  offset from `/proc/self/maps`, with system calls alone, and only then runs `addr2line` per module by
  `vfork` and `exec`, which run no fork handlers and so take none of the locks a crash may hold; a
  hung symboliser is ended by SIGALRM, the report already on the disk. addr2line named a frame of the
  160 MB debug build in 0.09 s. A player's machine, with no symboliser, writes modules and offsets,
  which `scripts/symbolise-crash.sh` names against the build. The sanitised build keeps its
  sanitiser's report instead.
- **String tables merge key by key, and the language is a setting** (2026-11-13, phase 11d). A table
  is one flat mapping, so a mod's file at the same path is already a patch of it: no `.patch.yaml`,
  and no whole-file replacement, which would only drop keys back to English. Arguments are
  std::format's by position rather than names, so the game's code formats with the library it already
  uses and a translation still orders them; a translation is held to its base text's arguments and
  data expressions as it is read, so no translation can make a format throw or a document read what
  its screen does not bind. The language is the player's setting rather than the environment's
  locale, so a run, a reference frame or the smoke test reads the same on every machine.
- **A component migrates by its own chain, over its binary record** (2026-11-13, the first open
  decision of the first tier). Each component storage in the world's save carries a version of its
  own, which a world step sets to 1 for every storage of an older save, and a component registers a
  chain of steps with it that the world runs as it reads that storage. A step is given the old value
  as its record: the field table the binary archive already writes, each field's name and its bytes,
  so a removed or retyped field is decoded with `BinaryReader`'s primitives by the step that knows
  it, and the old C++ type is never needed. Not a YAML node: authored content has its own chain
  (`YamlMigrations`), and a save's component is binary. A reflected struct inside a component is
  migrated by that component's step; a chain of the struct's own waits for two components that share
  a struct that changes. The world's own steps stay for changes to the whole world.
- **A script keeps what it hands the host by key** (2026-11-13, the second). A script stores bytes
  under its own keys through imports (`store`, `load`), in an encoding it chooses, and migrates them
  itself, by a version under a key of its own should it want one; the host never needs its types.
  Not the instance's linear memory saved whole: that is up to `memoryPages` (256 pages, 16 MB) a
  script, and a module rebuilt would no longer read its saves. Handlers run outside the step, so
  the keys are a section of the save game, with its own version, and not the world's state, its hash
  or a replay. Each script is held to a number of keys and bytes (64 KB to start), past which a
  store is refused as any other import's misuse is, so no mod grows every save.
- **An event says why by a reason and its arguments** (2026-11-13, the third). The text the docks,
  the lanes, the steering and the trades carry is replaced by one `Sim::Reason`: a key, an
  enumerator registered with Reflect and named by its enum (`reason.dock.pad_occupied`), so modules
  and mods cannot collide, and its arguments as an ordered list of typed values (an integer, which
  keeps currency integer, an entity, or a definition's identifier). Presentation words it from the
  string table under its key, an entity by its name in the snapshot and currency in the player's
  format; the arguments are positional, as the tables' are, so a translation is held to them by the
  check that already holds it to its base text. A script's reason is the same struct, under
  `reason.script.<script>.<name>`, worded by its mod's table. Events are not saved, so this is the
  shape of what leaves the step rather than of a save, made now because every rule that refuses
  adds a reason. The step's log lines stay English: they are for developers. Built in 12c: the
  enumerator is a module's, registered as `<Module>.Reason`, the key named by it; a definition's
  identifier carries its type, and an identifier with none, a body's or a system's, reads as itself;
  a key with plural forms is chosen by its first integer.
- **Physics keeps Jolt's state, and rebuilds the bodies when Jolt's version differs** (2026-11-13,
  the fourth). A loaded run carries on as an unbroken one would, which always rebuilding would give
  up on every load. Physics' layout 4 holds Jolt's version (`JPH_VERSION_MAJOR`, `_MINOR`, `_PATCH`,
  5.6.0 now) and the build's feature bits, which change Jolt's state as an upgrade does; a save
  whose version differs rebuilds the bodies from the world's components and physics' records, which
  hold each body, where it was placed and its velocity, and loses the contact cache's warm start for
  a step. The rebuild is built and tested when the layout is, by a load given another version, so it
  is not first run the day Jolt moves; what else a rebuild loses (the docking clamps' constraints,
  sleep) is checked as it is. Built in 12b, with each body's sleep in its record, so a rebuild
  keeps it; the clamps hold nothing in Jolt, a docked ship being kinematic on its rails.

- **A player has a fleet, and the credits are the player's** (2026-11-13, the game's). Each player
  starts with one ship and buys another once their credits allow. They attach to any ship of their
  fleet to watch it go and to trade at the market it is docked at, and take the helm of one at a
  time. A ship's `ControlledBy` is its owner, and whatever flies a ship no one is at the helm of (its
  behaviour, a cruise, the docks' approach) sends its commands as that owner, so `MayCommand` holds
  as it is. The purse moves from the ship to an entity of its owner's, which a trade, a toll and a
  ship bought draw on; each trader is the owner of one ship. Attaching is presentation's (the
  camera, and the market screen bound to the station the attached ship is docked at); the helm is a
  command, saved, since the ship's behaviour stands down while a player flies it. The purse was
  built in 12c (`App::Owner`, found by the controller); a ship bought, attaching and the helm come
  with the sample's fleet.
- **A ship crosses a system by cruising** (2026-11-13, the first of the second tier). The distances
  rule the others out: Corva Refinery is 1.4 × 10⁸ m from Corva and Corva 3 × 10¹⁰ m from Vesper,
  days at the flight model's speeds. Compressed time would want a thousandfold, which runs every
  market's tick at it and which one player of several cannot have alone; gates and lanes alone would
  leave a system's scale as scenery, and Manifest places its star gates about a sun and several
  stations about a planet, so something crosses between them. A `Cruise` command, with its sender,
  takes a ship to a body: it leaves physics and its frame as on a lane but stays in its system,
  placed each step along its course, for a time its hull's definition gives as a curve of the
  distance rising slower than it (about a minute between one planet's stations and a few between
  planets, tuned by play). The bodies are on rails, so where the target will be on arrival is solved
  exactly, in a few deterministic iterations, with a waypoint at a tangent past a body in the way;
  the ship arrives at rest a few kilometres off the target, in its frame, as a lane's exit leaves
  one. A frame therefore changes at a cruise's ends and a lane's and nowhere else. A cruise does not
  engage within a station's radius, so docking stays in physics, and is refused by a `Sim::Reason`.
  Presentation shows the ship against its system at its scale; a privateer can interrupt a cruise
  later, since where the ship is is known every step; a trader gains a cruise task, its time weighed
  in the run's credits a second, and so does a fleet's ship no one is at the helm of. Time
  compression stays the economy's and the wait's, not travel's. With it the gates move to
  Manifest's places, star gates about a sun and moon gates about a planet.
- **A gate sends the ships it has cleared** (2026-11-13, the second). The engine's part is a sensor
  on the ring whose contact sends a cleared ship into its lane, the gate a shared facility given in
  command order, as a pad is. The sample game's rule: `RequestTransit`, with its sender, is granted
  when the hull fits the ring (its size as data), the owner's purse holds the lane's toll (whole
  credits in `galaxy.yaml`, a star gate's dearer than a moon gate's) and the gate is free. The toll
  is charged as the ship enters the lane rather than as it is cleared, and a clearance unused
  lapses. A ship not cleared passes through the ring, with a reason, so traffic steering across a
  ring is never charged or sent. A licence or a standing, for which the sample has no factions, goes
  in the clearance's check when one comes, perhaps as an input a script answers. Built in 13c
  (`ai/Gates.h`), where a gate that is not free puts the ship in its queue, as a station's pads do,
  rather than refusing it, so a trader asks once and waits its turn; the toll's check and charge are
  the game's rule (`Ai::TollRule`), the rest the engine's.
- **All travel between systems is by gates, and a toll is reckoned by distance** (2026-11-14, the
  user's, after 13c). Each gate joins two systems, a lane's two ends, and a ship goes from one system
  to another only through them; the tools' jump (the console's `jump`, the J key, the galaxy panel's
  buttons), which sends a ship along a lane from anywhere in its system, goes once the smoke
  sequence, the reference frames and the tools no longer need it. A lane's toll is no longer its own
  number but the distance between its systems at the galaxy's rate: each system has a position in
  light years (`Celestial::GalaxySystem::position`) and the galaxy a `tollPerLightYear`, so a lane's
  toll follows from where its systems are and the star gates' long lanes are dearer for their
  length rather than their kind. A lane's transit time stays its own.
- **A player's mods are a list beside the settings, and a save names them** (2026-11-13, the
  third). `mods.yaml`, beside `settings.yaml` and written by the game's menu, lists the mods in mount
  order, each enabled or not, a later one winning. A mod found and not listed is added at the end,
  enabled, so a directory dropped in works as it does now; one listed and missing is reported and
  keeps its place. Each mod's `mod.yaml` gives a name to show (its identifier is its directory's or
  pack's name), its version, the engine version it was written for, refused when newer than the
  engine's, and `requires`, the mods it needs before it in the list, refused with a reason when one
  is missing or after it rather than reordered. A save records the mods enabled and their versions,
  and a load with others says which differ and goes on; that is the save's shape, so it is in the
  first tier.
- **A handler reads the snapshot, and acts as its player** (2026-11-13, the fourth). A handler reads
  the newest stepped snapshot, not a frame's interpolation, and is told which step it is of, through
  imports generated from reflection as the commands' are: `get:<Component>` and `each:<Component>`,
  granted by `read:<Component>`, for the components shown in snapshots and registered with
  `Reflect::Attribute::Script`. An input still reads only its option's inputs, so nothing a script
  reads is inside the step, and a rule that wants more is given another input. A handler's commands
  carry the local player's controller: it acts for the player whose game runs it, on their fleet,
  and is refused where they would be, since otherwise every player's copy of a mod would send the
  same command. A rule of the whole world, as the economy's turns are, is an input in the step and
  never a handler.
- **A crash report stays on the machine until players outside development have the game**
  (2026-11-13, the fifth). Each module's GNU build ID goes into the report and a release's debug files
  are kept by it, so `scripts/symbolise-crash.sh` finds them by the ID rather than needing the build
  that wrote the report, whatever later carries it; on its next start the shell says where a new
  report is. When there are such players, an endpoint that speaks Sentry's protocol (Sentry, or a
  self-hosted GlitchTip) is sent each report on the next start, with the player's consent, and never
  from the handler. Not Crashpad or sentry-native, which would replace the handler with minidumps,
  unless the frames and the tail prove not to be enough.
- **The space capability is two optional modules over neutral ones** (2026-11-18, with W01).
  `space` is the simulation's (deterministic): a star system as a physics frame, and the transit AI
  (cruise, gates, lanes, routes); `spacescene` is presentation's (the system scene, the jump
  transition). Physics, AI and the scene depend on neither, nor on celestial; the dock and the
  shell depend on `space`. Saved type names (`Ai.OnLane` and the rest) stay as they are, so no save
  or pin moves with a type's module. Splitting targets inside the old directories was the
  alternative, which leaves folder names that do not show the boundary. A pad class is a definition
  ranked among the others, a hull fitting its own class's pads and those ranked above (Manifest's
  D-044); fitting by size alone was the alternative, which would let a ship onto a pad the game
  means for another kind. Superseded on 2026-11-23 by the [space extraction](space-extraction-plan.md):
  `celestial` and `space` are Manifest's `space` module (slice 6), and `spacescene` went with the
  sample (slice 2).
- **The engine's shell is generic; gameplay is the game's, extending it** (2026-11-18, the user's,
  scheduled as G01). The rendered shell (`app`) provides what any game needs: the window,
  renderer, game UI and console, saves, mods and settings, input, the developer views, and a
  camera and controlled-entity seam a game drives. It has no gameplay of its own: star systems and
  the galaxy, stations and docking, piloting, cruising and jumps, traffic and their cameras and
  panels are a game's, composed into the shell through `App::Game` and built on the engine's
  facilities (`space`, `spacescene`, `dock`). The dock depends on no space module either. Keeping
  a space shell with a neutral mode was the alternative, which leaves every other genre carrying
  and switching off a game it does not play.
- **An entity's frame is the game's, and a save's details are the game's strings** (2026-11-18,
  with A01). `Sim::InFrame` and `Sim::FrameId` replace `InSystem` and `SystemId`: a frame is a
  partition the game defines, a game of one frame gives its entities none, and a star system is the
  space sample's use of one. Moving it into celestial instead would have left the neutral snapshot no
  frame to stop interpolation at, and given physics, AI, docks and spatial a dependency W01 is to
  remove. `SaveInfo::details`, key and value strings read with the info, replace its `galaxy` and
  `system`; binary metadata sections were the alternative, which a list would have to decode and a
  version 1 save's step would have to write in a format the simulation defined for the shell.
- **The network seam is ENet over UDP, with a game's verifier and bearer tokens** (2026-11-20, the
  user's, with Manifest's N01; [its contract](../../manifest/docs/gameplay-and-server-plan.md#network-contract-n01-2026-11-20)). A connection has
  reliable-ordered and unreliable-sequenced channels, so one stack carries an idle game's requests
  and a real-time combat variant's inputs and snapshots. The engine runs the handshake (protocol
  name, version, opaque credential) and asks the game's verifier, generates and checks bearer
  tokens against digests, frames messages as a kind and an archive-encoded body under a size
  limit, bounds each session's inbound queue, and sends each session the newest snapshot its
  callback gives at a bounded rate. Connections are plaintext until libsodium encryption (a key
  exchange in the handshake, the server's key pinned), which hosting outside a trusted network
  waits for. TCP through Asio was the alternative, which a real-time variant would outgrow; TLS
  does not run over UDP, and yojimbo's connect tokens would have shaped the game's accounts.
  libsodium is fetched and built static like the rest of the set, through robinlinden's CMake
  build of it (the user's, 2026-11-20, as N02 began). N01 had put it in both `devenv.nix` files,
  but nixpkgs ships it shared only, and its own build is autotools.
- **Quiet steps are passed over by each system's own word, to the same bits as stepping** (the user's,
  2026-11-20, before Manifest's W10; [architecture](architecture.md#quiet-steps)). A system says the
  step it next has to run in and how to put what it writes where the steps between would have left
  it (`Sim::Quiet`); one that says nothing runs every step, which keeps a game's system correct
  before it has a quiet. A world with nothing dynamic in it puts each kinematic body where it is,
  asleep, so a gate's ring, a spinning station and a far station's rounding are quiet. Two
  alternatives were rejected: giving Jolt each body's place on its own frame's body exactly, which
  ended the rounding but not a ring's or a spin's turning, and a local frame that turns with its
  body, which would give every ship flying in it rotation's forces. Passing over steps by
  extrapolating, or to within a tolerance, was not considered: a save and a replay must give one
  hash.
