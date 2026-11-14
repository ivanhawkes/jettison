# Roadmap

The order is set by what is expensive to change later, then by what later work stands on. It
is not set by what looks good in a screenshot. Architectural decisions (layout, the
simulation split, coordinate precision, colour space, serialisation) come before the systems
that would otherwise bake in the wrong answer. "Done" means the gate covers it, with the
commit that did it in the history. Phases 0–12, and the catalogue entries for the systems they
built, are recorded in [history](history.md).

## Phases

| Phase | Work | Exit criterion |
|---|---|---|
| 0–12 | **Done** 2026-11-10 to 2026-11-14: plan, layout and scaffolding, core, runtime architecture, coordinates and render foundations, space rendering, star systems and galaxy, physics and flight, UI, audio and persistence, AI and docks, presentation polish, scripting and shipping, saves and the step's interface. Recorded in [history](history.md). | Met. |
| 13 | **Crossing a system, its gates and the fleet.** Sub-phases 13a–13d: the sample's fleet ([sample game](sample-game.md#what-comes-next)) and the space it is played in, the head of the second tier of [carried forward](#carried-forward), with the first tier's last commands to carry their senders. | A ship crosses its system by cruising and leaves it through a gate that cleared it, its owner charged the lane's toll; a player buys a second ship, attaches to either and takes the helm of one while the other trades for them by its behaviour. |
| 13a | **The cruise.** Done 2026-11-14. Catalogue entries 22 and 26 ([decided](#decided): a ship crosses a system by cruising). `Ai::Cruise`, with its sender, takes a ship to a body of its system (`ai/Cruise.h`): out of physics and its frame, its body kept in its `Ai::Cruising`, placed each step along its course by the system `cruise`, after the lanes'; and out at rest, facing the body, a quarter of its radius and 4 km beyond it and no nearer than where its gravity pulls at 1 m/s², which a ship's assist holds against, in its frame or, for a gate by a station, the station's, so a frame changes at a cruise's ends and a lane's and nowhere else. Its time is the hull's drive (`Ai::CruiseDrive`, a hull definition's `cruise`), the seconds for 10⁸ m rising as the distance's fourth root; the target's place at arrival and the time are solved from each other in eight rounds; the course is straight legs round the stars, planets and moons in the way (`Ai::Waypoints`, 2 km clear), relative to its carrier, the nearest body both ends orbit, and carried with it, each body in the way placed where the ship passes it; along it the ship eases out and in by an in-out cubic and faces along its leg. Refused with a reason (`reason.ai.*`, in English and German) for a ship its sender does not command, cruising already, of no drive or on rails, within 2 km of a station's radius, already where it would arrive, to a body its system does not have, or of no course; a lane is refused to a cruising ship. The shell gives every ship its hull's drive, sends the player's with `cruise <body>`, and its flight panel, controls and pilot camera follow the ship's frame from the snapshots ([cruising](architecture.md#cruising)). | Met: in the ai suite, the shuttle's drive takes Highport to Pell, 392,995 km, in 56.3 s, the whole steps its course's length gives; out of physics and its frame and in its system throughout, clear of Ostra and Pell at every step, 15 km out a second in; out in the step its arrival names at 2,214.7 km from Pell's centre, where Pell pulls at 1 m/s², to a part in 10⁹, facing it, at rest in Pell's frame, with its body made in the next step; and back to Halcyon Gate, in Highport's frame, to the millimetre. A course from the far side of Ostra goes round it by four points, 389 km above it at nearest; a ship arriving off Ostra, 19,965 km from its centre, is held by its assist to within a metre for a minute; to the star takes 243 s, carried by it; each refusal gives its reason, and a world saved mid-cruise arrives at the hash the one saved does. In the shell under Xvfb, the ship out of the lane at Vesper Gate cruises to Corva, 52,522 km in 34.1 s at up to 3,218 km/s, the flight panel counting down, and arrives facing it 87,504 km from its centre; within Highport's reach it is refused, worded from the table. |
| 13b | **Gates in Manifest's places, and the traders cruising.** Done 2026-11-14. Catalogue entry 26 (9a, 9f). The gates are in Manifest's places: Halcyon Gate a moon gate about Ostra, Vesper Gate one about Corva, Vesper Deep Gate and Marrow Gate star gates about their suns, each on its parent's orbit and facing a planet (`JumpPointFacing`), so a ship comes out in the gate's own frame and no longer beside a station. A second station in Halcyon, Ostra Anchorage, a mining block in low orbit over another of Ostra's economic blocks (its layout and a market, `defs/stations`, `defs/markets`), gives a run within a system to cruise between. A trader's `cruise` task (`app/src/Traders.cpp`), with `gate` or `market` for its argument: flown out of a station's 2 km reach round the station as `fly-to-gate` goes, then `Ai::Cruise` with its sender, running until it arrives, and failing if refused; the tree is `leave`, then for each jump `cruise gate`, `fly-to-gate`, `jump`, then `cruise market` and `dock`. A route's crossings are costed by the ship's drive, the new `Ai::CrossingByCourse` overload taking a time of the course's length (`CourseTime`), the trader giving its drive's seconds and 45 s to leave a station's reach and come in to a gate, so the run's credits a second are weighed in the time it takes. Arrivals are spread: the lanes' ring placement is now `Detail::ClearPlace`, shared, and a cruise's arrival, which `CruiseTravel::Attach` now gives physics for the hulls' radii, comes out clear of every body there and of what arrived before it in the step by `kArrivalClearance`. | Met: in the app suite the seven traders trade for two hours of the galaxy's time, 76 runs sold, 221 cruises of 17 to 73 s and none refused, 289 arrivals by lane and cruise, the tightest 35 m between hulls against the 20 m asked, no contact but the traders' own on their pads, and the world saved at four hours carries on to the hash the original reaches, pinned at `0x487ab0bd2d27a6b8` (moved from `0x4b2e0ff03b7e40b4`, since the traders now cruise where they flew, and a third market changes what they choose; no other suite's pin moved). In the ai suite two ships set out four metres apart arrive at Pell 20.007 m between hulls; the gates are checked at each of their parents. The reference frames are unchanged but the market screen's, whose other markets now name Ostra Anchorage. |
| 13c | **A gate flown through.** Done 2026-11-14. Catalogue entry 26 ([decided](#decided): a gate sends the ships it has cleared). Each jump point's gate is an entity (`ai/Gates.h`, `Ai::Gate`), made at the start (`Gates::MakeGates`, the shell's `OpenGates`, which also gives a world saved before gates its own) and saved: a kinematic sensor on the jump point's rails and in its frame, a cylinder of the ring's radius 60 m deep (`Gates::RingShape`), turned as the jump point faces (`Celestial::BodyOrientation`, which physics' rails now use). `Ai::RequestTransit`, with its sender, clears a ship at once if the gate is free or queues it, as a pad does, the ship holding an `Ai::Transit`; refused with a reason (`reason.ai.*`, English and German) for a sender that does not command the ship, a ship not flying or not in the gate's system and frame, one already cleared or waiting, a gate no lane leaves through, a hull larger than the ring, and what the game's `Ai::TollRule` refuses. The system `gates`, after the lanes', sends a cleared ship touching its ring along the lane, charging its owner as it enters (`TransitSent`); lets every other body that begins to touch a ring pass through with a reason, charged nothing (`GatePassed`); lapses a clearance unused for two minutes; gives up one whose ship has gone, left the frame or cruises; and clears the next in the queue. Each lane has a toll, carried by its exits and routes: since the same day, the light years between its systems at the galaxy's rate (`tollPerLightYear`, 500 credits; Vesper 4 light years from Halcyon, Marrow 12 beyond), 2,000 credits the moon gates' hop and 6,000 the star gates' ([decided](#decided)), where it was first a number of the lane's; the sample's rule draws it on the owner's purse (`App::ChargeTolls`, `reason.shell.short_of_toll`). A ship comes out of a lane ahead of the ring, clear of its sensor, and gives up its destination as it enters one; arrivals keep clear of no sensor. A trader's tree is now `cruise gate`, `clear`, `fly-through`, `jump`, its runs weighed net of their tolls and the tolls on to its market kept back from what it buys. The shell's `transit [gate]` asks for its ship, the flight panel counts the clearance down, and the scene jumps as the gate sends the ship; `jump`, J and the galaxy panel stay the tools' jump ([gates](architecture.md#gates)). | Met: in the ai suite a ship cleared at Halcyon Gate and flying at 100 m/s from 400 m out is sent as it reaches the ring's near face, about 222 steps on, and charged the 2,000 credits, coming out at Vesper ahead of that ring unseen by it; two ships flying through uncleared, one of them waiting in the queue, pass through 600 m out the far side with `reason.ai.not_cleared`, in Halcyon on their credits; a clearance lapses in the 7,200th step and the gate clears the next in its queue in that step; a cleared ship whose owner has spent the toll passes through with the rule's reason and frees the gate; each refusal gives its reason; and a world saved with a ship cleared and one waiting goes on to the hash the original reaches. In the app suite the seven traders over two hours are sent through gates 65 times for 130,000 credits, 7 waiting their turn, the longest 33.8 s from cleared to sent, with none refused, lapsed or passing through unsent; 81 runs sold, the tightest arrival 34 m between hulls; the pin moved to `0x5546eebbec058964` (from `0xb84ccd89f6ba5ea8`, since the traders now wait at gates and pay tolls; no other suite's pin moved). In the shell under Xvfb, the shuttle cruised to Vesper Gate was cleared by `transit`, the panel counting down; let lapse, then asked again and queued behind a trader, cleared 7 s later, flown through at 144 m/s, sent and charged 2,000 credits, the scene jumping to Halcyon, and out at Halcyon Gate 40 s on with 248,000 credits. |
| 13d | **The fleet and the helm.** The sample's fleet ([decided](#decided): a player has a fleet), and the first tier's commands that carry their senders (9c). A ship bought at a station; attaching to a ship of the fleet, the camera's and the market screen's; the helm a saved command, which stands the ship's behaviour down; the flight controls, a destination, a lane entered and a pick carrying their senders. Before it, on 2026-11-14, a ship undocked by its pilot, which seemed never to be let go: the docks flew every ship released out to the point above its pad, beyond the station's sphere, 755 m over Highport's pad 1 at the lift's 20 m/s, 39 s with steering writing over the pilot's controls, so the pad was given up only after the run that saw it had stopped watching. `Dock::ReleasePad` gained `fly`, as `RequestPad` has it: a ship the docks fly out (the traders', which cruise on) is flown as before; one its pilot flies (the shell's) is lifted straight off the pad by its thrusters, 40 m (`Dock::kLiftHeight`), and held for the pilot, in the dock suite 2.7 s after the clamps open, answering its controls after; no pin moved. | A player buys a second ship, attaches to it and trades from it, takes its helm while the first trades on by its behaviour, and a flight control for a ship whose helm is another's is refused. |
| 13e | **Thrusters seen.** Done 2026-11-14, before 13d, at the user's asking: a ship flown by its AI shows when it fires its engines and its manoeuvring jets. The flight model writes each ship's `Physics::Thrust` every step (`physics/Flight.h`), the share of each axis's thrust, along and about it, that changed its velocity: shown in snapshots, and neither registered, saved nor hashed, since nothing in the simulation reads it, so no pin moved; the snapshots no longer carry `FlightControls`. Each ship proxy of the simulation's (`app/src/Dressing.h`) has a port at each engine nozzle and a jet at each of fourteen manoeuvring ports, a cluster each side of the bow and the stern firing out, up and down, and the bow's forward; `ShowThrust` fires each from the snapshot's thrust, an engine by the thrust ahead and a jet by how far its push and its turn about the hull's middle lie along the thrust and turn fired, its emitter's plume or puff as dense as it fires, at rest in the station's frame. Each nozzle glows at 3% at rest and warms towards how hard it fires with a time constant of a second, cooling over two and a half. | Met: in the physics suite a ship at full ahead fires all of its forward thrust, shown in its snapshot; held at 150 m/s it fires nothing; let go it brakes astern at full and turns at full about the axis asked; one no longer flown has no thrust. In the app suite a shuttle's proxy has two engines and fourteen jets; firing nothing shows nothing; half ahead a plume half as dense as full; astern the two bow jets; a strafe two jets, a climb four, a yaw three; a ship with no thrust nothing; its nozzle at 3% warms to 64.2% of full in a second of firing and cools by e in two and a half. Under Xvfb the shuttle lifted off its pad by the docks fires its four downward jets, their nozzles warm, and holding 20 m/s it fires nothing as they cool. The reference frames `docked` and `dock-sequence` are updated for the nozzles at rest. |

Phase 1 was the last step before large-scale work: every system now has a home, a namespace, a
target and a test, and phases 2–11 fill stubs in rather than invent structure.

Phases 5–13 are split into lettered sub-phases (5–12's now in [history](history.md)), each a
block of related work that touches the same modules and documents, so it can be done in one session
and the context cleared after it.
A sub-phase names the catalogue entries that are its brief; read those, not the whole catalogue.
A phase is done when its last sub-phase meets the phase's exit criterion, so a reference to
"phase 7" in the code or the documents still means the whole phase. A sub-phase with no exit
criterion yet gets one when it starts.

## Carried forward

Work that phases 1–12 moved on, in the order it is worth doing: by what is expensive to change later,
then by what the game's next work stands on, as the phases were ordered. Within a tier the first is
the most pressing. The decisions they waited on are settled, under [decided](#decided). Phases
1–12's full record, and catalogue entries 1–31, are in [history](history.md).

### First: what saves and the simulation's interface hold

Each of these fixes what a save holds or the shape of what goes into and out of the step, and gets
dearer with every save a player has and once a second player sends commands.

- **Commands that carry their sender** (9c): the docks' commands, the trade (12c), a cruise (13a)
  and a gate's clearance (13c) carry a `Sim::Controller` and are refused from one that does not command the ship, and a
  trader sends its own with its AI controller (9f); the flight controls, a destination, a lane
  entered and a pick do not yet, and are taken from anyone. They gain theirs with the helm, in 13d.
  Phase 12 took the rest of this tier.

### Next: what the game's next slices stand on

The order book, the economy's turns and the scripted rules ([sample
game](sample-game.md#what-comes-next)), and the space they are played in: a system crossed at its
own scale, gates flown through, traders that react, and mods a player can choose between.

- **The sample game's later slices** ([sample game](sample-game.md)): a player's fleet, a ship
  bought and the helm of one of them (13d); ManifestCore's order book, which the traders (9f) post to once
  they trade by orders; the economy's turns and the skill tree's effects, then scripting (11).
- **A screen to buy ships from** (the user's, 2026-11-14, as 13d began): 13d buys a ship with the
  console's `buy <hull>` and lists and attaches to the fleet with `fleet` and `attach`, on keys; a
  shipyard screen of the game UI's, beside the market screen, with the hulls a station sells, their
  prices and the purse, is held over for a later phase, as is a fleet screen.
- **Cruising, not yet placed** (entries 22 and 26, 13a): a cruise to a ship rather than a body;
  one broken off part-way, as a privateer's would be, which knowing where the ship is every step
  allows; a course that bends round a body rather than turning at waypoints, which the ship does in
  a step; and a body in the way placed where the ship passes it by its time alone, so a planet
  neither end orbits is placed half-way and widened by half of its own motion. A cruise's arrivals
  are spread clear of each other as a lane's are (13b), but a ship that comes out at rest is not
  made to move off if another then flies into its place.
- **Gates, not yet placed** (entry 26, 13c): a ship waiting its turn at a gate is not placed, so a
  trader waits where its cruise left it, spread from the others only as arrivals are; a clearance is
  given up only by lapsing or by its ship leaving the gate's frame, and no command gives one up; a
  licence or a standing, which the clearance's check takes when the game has factions (decided,
  perhaps as an input a script answers). The console's `jump`, the J key and the galaxy panel's
  buttons are the tools' jump, which sends the ship along a lane wherever it is, past the gate and
  its toll, as the smoke sequence and the reference frames use it; it goes, since all travel is by
  gates (decided, 2026-11-14), once they fly through a gate instead; the smoke sequence does not wait
  the 40 s for the ship to come out, and the AI and app suites cover the arrival.
- **Decisions, not yet placed** (entry 26, 9f): reactive nodes, which tick a condition above a
  running branch and halt what they leave, as a trader breaking off its run to flee would need, and
  with them a task's halt; parallel nodes; a tree that names another as a subtree, where the
  trader's tree repeats its three-node jump loop for each leg; a blackboard of the engine's, where a
  trader's run is its own component; a command that gives an entity a tree, with its sender, where
  the shell gives a trader its tree as it makes it; and a tree ticked less often than every step,
  which seven traders do not need. A trader's run that cannot be made waits half a minute and
  chooses again; what a trader does about a hull it cannot dock, a market that has gone, or the
  player in its way beyond steering clear is the game's.
- **Scripts, not yet placed** (entry 29, 11b): a script reading the world, which today it sees only
  through the events it is given and an option's inputs (decided, 2026-11-13: a handler reads the
  newest stepped snapshot, by component, as granted, and acts as its player; an input reads only its
  inputs); a script called from a behaviour tree's task, as an input is from a consideration; a script reloaded
  when its module changes; scripts compiled ahead of time; and the fuel a call used, which WAMR does
  not report, for the console. A script's own reasons (`reason.script.<script>.<name>`, decided),
  which wait for something a script refuses; and the sender of a command a script sends, which it
  sets itself, as a `Trade`'s (12c), until a handler acts as its player.
- **Packs and mods, not yet placed** (entry 10, 11a): the game's menu over `mods.yaml` (12a), which
  the player edits by hand until then; a patch that removes an element from a list, rather than
  replacing the list; a pack hot-reloaded, which is read as it was mounted; and layering for anything
  but definitions, so a mod still replaces a scene, a system or a stylesheet whole.
- **Picking a paused world, and under the cursor every frame** (entry 22, 7c): a pick is answered by
  the next step, so a paused world answers when it resumes, and a hover highlight would send a
  command a frame. Both want a query presentation makes itself, against the bodies' shapes in a
  snapshot. Not yet placed.
- **Physics, not yet placed** (entry 22): a shape offset in its body, and compound shapes; collision
  filtering beyond static against moving (the game's own layers and groups); Jolt's debug renderer
  drawn through debug draw; Jolt's job system on more than one thread, once a profile asks for it
  (deterministic either way); the capacities tuned against real traffic; overlap queries (every body
  in a shape) and casts that return every hit, which steering (9b) did without (below).

### Then: robustness, tools and what a mod can reach

Cheap to add when they are wanted and costly to nothing meanwhile, but for the log, whose file a
player's report of a problem needs and the shell does not write.

- **Frames drawn on the GPU, offscreen** (the user's, 2026-11-14; entries 21 and 28). Every
  headless run draws on lavapipe under Xvfb, about a second a frame at 1080p, since Xvfb cannot
  present an NVIDIA Vulkan surface and NVIDIA's driver has no `VK_EXT_headless_surface` for GLFW's
  null platform; and a window on the desktop, which is Wayland, can be given no keys safely. An
  offscreen target, the renderer drawing into an image of its own with no surface or swapchain and
  `--screenshot` reading it back, would let a local run draw on the monitor's card (the 5060 Ti,
  never the 5070 Ti, by `DeviceChoice`), with timed console lines (`--run` at a frame or a
  simulation time) to drive a scenario a capture cannot today, such as undocking and then
  thrusting. The plain smoke test and the shell's checks under it would run in seconds; the
  reference frames stay on lavapipe, whose images they are, the sanitised smoke test too, since
  NVIDIA's driver fails under ASan, and so does CI. Not yet placed.
- **Logging in the shell** (entry 3). The app shell does not call `InitLogging`, and the renderer
  writes to `std::cerr`, because the smoke test reads that output; they move to `Log.h` with the
  smoke test's patterns.
- **Crash handling, not yet placed** (entry 30, 11d): a report from a thread other than the main
  one whose stack overflowed, which wants an alternate stack set by every thread the engine starts
  (the I/O worker, the scripts' watchdog) and by Jolt's; each module's build ID in the report, and a
  release's debug files kept by it, and the shell saying on its next start where a new report is
  (decided, 2026-11-13); a report sent anywhere, which waits on players outside development; a
  minidump, should a report's frames and tail not be enough.
- **A destination prefetched** before it is shown (entry 21): the renderer asks for a model when it
  first draws it, so a jump holds its effect over the new system's first frames while they stream
  in, 0.2 to 0.4 s on lavapipe for the sample systems. Not yet placed.
- **A body's look as data** (entry 21): the system scene draws a body it builds as its kind is, from
  a table in code. A station's look is its layout's, dressed (9d); a planet's material, air and
  rings are a scene file's (10a), so Corva's rings are drawn in `scene/vesper.yaml` and not in the
  Vesper a jump builds from its system file. A look per planet, moon or kind, as a scene fragment a
  mod can shadow, is not yet placed.
- **A glTF model's collision mesh** (entry 22, 7b): a station built to a layout collides as its
  decks and piers (9c), as every station of the sample systems is, and one of no layout as the
  procedural shapes the scene draws it with (`app/src/Piloting.h`); a hull collides as the box of its
  definition's size, whether or not it names a model to be drawn with (9d). A model's triangles, read from its file on
  the I/O worker as its rig is, or a collision mesh beside it, are not yet placed, and a model on a
  station of no layout is reported and left out.
- **Steering, not yet placed** (entry 26, 9b): an obstacle is the sphere about its body that holds
  its shape, and only a destination kept clear by its hull (9c) flies inside one, by a shape cast
  along its way, which slows it and does not turn it: a course round the inside of a station, from
  one pad to another under a pier, is not plotted, and a docked ship's way to its pad is straight
  down from outside the sphere, which a layout is checked to leave clear. Steering is measured for a
  station's traffic, sixteen ships; a busier lane's, where every ship queries the index each step,
  is measured when a game has one.
- **Deck traffic, not yet placed** (9e): a buggy drives to its ship's hatch on the deck under the
  hull, through whatever of the hull's proxy is lower than the buggy, as a ramp or a lift a hull
  lowers to its hatch would not; roads run only on the tops of decks and piers, so a pad on a side
  or an underside, which no road reaches, is refused at a station with bays; the roads' legs are
  not checked against each other, so two that cross between nodes let buggies pass through each
  other there, which neither sample station has; a buggy carries crates, not goods, the trade
  having said which; and a pad's buggies are its own, not a station's pool shared out by demand.
  A buggy is placed on its station's rails each step, which for a station's few dozen is nothing,
  and is measured when a game has hundreds.
- **The station dressing, not yet placed** (9d): the dressing is drawn and not collided with, so a
  ship flown off its pad's column passes through a floodlight pole or a display ship, which the
  column a layout is checked to keep clear never meets; a pad's look as data (its paint, its poles'
  spacing and its floodlights' aim are the shell's, by class, in code; a reserved pad's lamps blink,
  10c, by a material of the shell's choosing); the floodlights' shadows,
  which the clusters' lights do not cast. The light-budget scene (`scripts/make-light-budget-scene.py`)
  still adds its 265 lights where 5e's hand-built hub was, now inside the layout's hub, so it measures
  the grid's cost and no longer shows anything; the dressing's 104 are the station's lights now.
- **The UI, not yet placed** (entry 23, 8b): a stylesheet or document reloaded when its file
  changes (RmlUi keeps a stylesheet for the run); a document's images decoded on the I/O worker
  rather than as the document lays out, which is on the main thread and fine for a screen's icons;
  RmlUi's debugger in the dev tools.
- **Localisation, not yet placed** (entry 30, 11d): numbers with fractions, dates and times in the
  language's own form, where only a whole number's thousands are; text a document holds in an
  attribute, which RmlUi does not hand to the table; right-to-left and shaped scripts, which RmlUi
  can lay out with HarfBuzz and the fonts shipped cannot draw; and the player's language chosen at
  the first start from the platform's (Steam's, the environment's), which the setting stands in for.

### Last: presentation, when a scene asks for it

Each is placed by the first scene or screen that needs it, and changes no data or save on the way.

- **Animation, not yet placed** (entry 27, 10d): a glTF clip's events, which glTF has no place for
  (its extras, or a clip file beside the model); one clip blended into another, so a clip changed
  part-way, as a pad's clamps let go before they have closed, starts the new one from its first pose
  rather than from where the rig is; the clamps fitted to the hull's box rather than its shape, so
  on a hull drawn by its model they grip where its box's sides are; and rigs of the ships' own
  (landing gear, a cargo hatch), which the hull proxies do not have.
- **The audio, not yet placed** (entry 24, 8c): music that crossfades between pieces and a playlist
  to choose them; Doppler, once something reports velocities to presentation; the ship's own sounds
  (its engines, the cockpit's) and occlusion by hulls; a sound file streamed from its mount rather
  than read whole, which waits on a streaming read in the VFS (entry 10; a pack's stored files could
  be read in ranges, its compressed ones would want zstd's streaming decoder). The voice bus has
  nothing on it yet.
- **Cinematics and grading, not yet placed** (entry 30, 11c): a jump's cutscene, which the
  sequencer plays as data but the jump does not yet wait on (its fade rises as it is asked for), and
  whose arrival would bind the gate, which the shell does not; a sequence's events heard by the shell,
  as sounds by data; a shot's roll and depth of field; a blend that steers round a hull rather than
  along a line, which a rail is drawn to avoid today (the dock's starts on the chase camera's side);
  and a grade blended between two tables, as a sequence's own look or a system's change of one
  would want.
- **The UI's drawing, not yet placed** (entry 23, 8b): RmlUi's clip masks (an element with
  `overflow` clipped by a rounded border, or clipped while transformed), which want a stencil in the
  output pass's rendering, where the UI is drawn into the swapchain and the depth target's stencil
  (10c) is not attached; and its layers, filters and shaders (`filter`, `backdrop-filter`,
  `mask-image`, gradients drawn as shaders), which want render targets of their own, as the post
  chain's are (10c). Until then RmlUi's defaults draw nothing for them, and a screen uses none.
- **The transparent pass, not yet placed** (entry 28, 10b): particles sorted within an emitter, on
  the GPU, which a smoky emitter seen through itself would want; soft particles where they meet a
  surface, which want the depth the opaque pass is still writing, so a node of their own after it;
  refraction through glass, from a copy of the frame behind it, which the opaque pass's resolved
  target would give were the transparent draws a node of their own after it; a glTF primitive's `BLEND` alpha mode, which draws opaque; a transparent
  surface's shadow, tinted, which the sun's depth-only cascades cannot hold; and an emitter
  started warm, as if it had been running, which a capture of a smoke column waits three seconds
  for instead. A ship's thrusters are shown as its proxy's class places them, a hull drawn from a
  model has none, and a hull's definition cannot place its own ports yet.
- **A nozzle's glow, to revisit** (13e): a thruster's glow is its procedural nozzle's mesh colour,
  which is in the shape's vertices, and the renderer keeps one model a colour (`Render::ModelCache`),
  dropped the first frame none draws it. Shown as it was, every frame a new colour, it made and
  uploaded a model for every nozzle every frame, jerky in a release build; it is now shown in eight
  steps that every nozzle shares (`kGlowSteps`), which bounds the models at nine but steps the
  ramp. The method is to be revisited: an emissive scale a draw carries per entity (a mesh's own,
  or a material parameter set per draw), so a glow is smooth and costs no model, and a nozzle of a
  hull drawn from a model can glow too.
- **The post chain, not yet placed** (entry 28, 10c): a transparent part of an outlined shape (a
  canopy) is not in the depth prepass, so the shape's outline runs round the opaque parts alone; a
  pick read from the object IDs under the mouse, which the physics ray answers today; the game UI and
  the dev tools under a mask, since they are drawn in the output pass, where the stencil is not
  attached; ambient occlusion at half the frame's size, or kept from frame to frame, should a
  measured frame want it cheaper.
- **Air over a mesh in it** (entry 28, 10a): the haze is the planet's own surface's and the sky's,
  not a ship's or a station's within the air, which no sample scene has yet, nor a transparent
  draw's (10b); nor are the bodies' air and rings sorted where two overlap on the screen. Not yet
  placed: the first scene with a ship in the air brings it, for the opaque and the transparent draws
  alike.
- **The planets' look in the environment's capture** (entry 28, 10a): the capture draws a body as
  a sphere of its mesh's colour, without its maps or clouds, which would about double Ostra's
  brightness in it. Not yet placed.
- **A gas giant's bands** (entry 28, 10a): Corva is one colour under its air. Not yet placed.
- **Not yet placed** (entries 19 and 20): the sun's shadow distance and split blend tuned by
  measurement; hysteresis between levels of detail, and levels for model files; the planets' own
  shadows past the shadow distance (a station in eclipse), which would need an analytic test against
  the bodies; programs that hot-reload; cube maps and texture arrays in files; normal mapping in `mesh`, which
  waits for tangent-space vertex data (`math/Tangents.h` makes it; `planet` builds its frame from
  the sphere and does not need it); a glTF model's own
  metallic-roughness factors and textures; and a model file read once rather than twice, for its
  rig and for its geometry (6a).

## Stubs

Phase 1 wrote a stub for every module in the catalogue below; the modules under
`source/engine/` are the list. A stub is:

- the module's directory, `CMakeLists.txt` and its place in the dependency graph
- its public header(s), with the main types and functions declared and documented: what they
  are for, what owns what, and which phase fills them in
- an implementation that compiles, does nothing observable, and fails loudly where doing
  nothing would be wrong (`JETTISON_NOT_IMPLEMENTED()` from `core`)
- one doctest test that the module links and its stub behaves as documented

A stub does not invent behaviour the phase that fills it would have to undo. Where a stub holds
configuration it was constructed with (a fixed step, a seed, a mount list), that much is real and
tested; everything that would need an algorithm throws.

## System catalogue

In priority order. Each entry is the brief for its stub. *Library* is the chosen dependency.
*Stub* names what phase 1 declares.

Entries 1–31, for phases 1–12, are in [history](history.md#system-catalogue-entries-131).

## Decided

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
- **The sample game is Manifest's plan** (2026-11-12, phase 8b; the project was the Civilisation
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
  reactive nodes, which halt what they leave ([carried forward](#carried-forward)); none is built
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

## Open decisions

To be settled before the work that needs them, each with what is recommended. None is open: the
first tier's of [carried forward](#carried-forward), and the second's with the game's fleet, were
settled on 2026-11-13, under [decided](#decided).
