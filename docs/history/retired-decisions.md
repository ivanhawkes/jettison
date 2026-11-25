# Retired decisions

Decisions that no longer apply to the engine, word for word as [decisions](../decisions.md) held
them, with why each was retired (2026-11-25). Most were about the space game, whose code moved to
Manifest in the space extraction (2026-11-23); a few were superseded. Only relative links were
adjusted for this folder. Where part of one still holds, the note links its restatement.

## Superseded

- **Physics is simulation state, a Jolt world per star system, on one thread** (2026-11-11, phase
  7a; a world per frame of each system since 7b, below). Jolt's state is saved inside the world's save, as a service of the world, rather than
  rebuilt from components on a load: the contact cache each step warm-starts from is Jolt's alone,
  and a world rebuilt without it steps on differently from the world saved. A world per system
  rather than one with a filter per system, since every system's coordinates start at its own
  primary and their bodies would share one broad phase; 0.7 MB each at the default capacities.
  Body identifiers are chosen by the engine, the lowest free index with a saved sequence number,
  because Jolt's free list is history a save does not hold and its results depend on them. Jolt's
  job system is single-threaded, as the threading rules have the simulation.

  *Retired:* Superseded the same day by one Jolt world per frame of each system, and since by a
  world per frame and per local frame. Its live parts, Jolt's state saved with the world,
  identifiers chosen by the engine and one thread, are restated in
  [decisions](../decisions.md#physics).

- **Superseded** (2026-11-15, by [game integration](../game-integration.md): the example is not grown
  from Manifest's plan or kept in step with it). **The sample game is Manifest's plan** (2026-11-12, phase 8b; the project was the Civilisation
  Simulation until it was renamed the same day). The shell plays the game's part from the plan in
  `~/projects/manifest`, the local copy over the GitHub one, slice by slice as the engine systems that each needs arrive
  ([sample game](../sample-game.md)). Its rules are the shell's, standing in for the game's, and none
  of them is engine code.

  *Retired:* Superseded on 2026-11-15 by the [game integration](../game-integration.md) contract;
  the space sample it described was retired on 2026-11-22.

- **The space capability is two optional modules over neutral ones** (2026-11-18, with W01).
  `space` is the simulation's (deterministic): a star system as a physics frame, and the transit AI
  (cruise, gates, lanes, routes); `spacescene` is presentation's (the system scene, the jump
  transition). Physics, AI and the scene depend on neither, nor on celestial; the dock and the
  shell depend on `space`. Saved type names (`Ai.OnLane` and the rest) stay as they are, so no save
  or pin moves with a type's module. Splitting targets inside the old directories was the
  alternative, which leaves folder names that do not show the boundary. A pad class is a definition
  ranked among the others, a hull fitting its own class's pads and those ranked above (Manifest's
  D-044); fitting by size alone was the alternative, which would let a ship onto a pad the game
  means for another kind. Superseded on 2026-11-23 by the [space extraction](space-extraction.md):
  `celestial` and `space` are Manifest's `space` module (slice 6), and `spacescene` went with the
  sample (slice 2).

  *Retired:* Superseded on 2026-11-23 by the space extraction: `celestial` and `space` became
  Manifest's, and `spacescene` went with the sample. Its berth-class ranking is restated in
  [decisions](../decisions.md#docks).

## Moved to Manifest with the space setting (2026-11-23)

- **A jump reads its files on the I/O worker and parses them on the main thread** (2026-11-11,
  phase 6d). The archives read EnTT's meta context, and a type registered on the main thread while
  the worker resolves another inserts into the table it is reading; nothing in it guards against
  that, and registration is lazy throughout the engine (every reader registers its types on first
  use). A system file and a scene file are a few kilobytes of YAML; the megabytes a destination
  needs, its models and textures, decode on the worker as they always have, and touch no reflection.

  *Retired:* The jump, its star-system files and their scenes are Manifest's; the engine has no
  jump.

- **A jump point is the end of one lane** (2026-11-11, phase 6d), so a gate says where it leads, and
  flying through it needs no choice of destination. A system with two ways out has two gates.

  *Retired:* Jump points and lanes are Manifest's (`source/game/space`).

- **The simulation's jump is the engine's, in `ai`** (2026-11-12, phase 9a). What sends a ship along
  a lane (a pilot through a gate, a trader's route) is the game's; carrying it is the engine's
  (`Ai::LaneTravel`): the command, the component a save holds, and the system that brings it out,
  after physics, so physics removes its body the step it enters and makes it again the step after it
  arrives. A transit is whole steps, counted from the clock's step rather than compared in seconds,
  so it is the same every run however its seconds round. `ai` depends on `physics` for it, as 9b's
  steering does.

  *Retired:* `Ai::LaneTravel` is gone from the engine. Its neutral part, a transit counted in whole
  steps with the body removed and made again, is restated in [decisions](../decisions.md#transit).

- **A ship comes out of a gate at rest, in its station's frame, facing the planet** (2026-11-12,
  phase 9a). A ship's speed carried through a lane would send one that went into a gate at speed out
  of the far one at a station a few kilometres ahead of it. So a ship arrives still relative to the
  station the gate keeps by, in that station's frame, where it can meet and dock with the station
  without changing frames, turned the way the gate faces: at the planet it orbits, or else the
  nearest planet, or else the nearest station (`Celestial::JumpPointFacing`). The gate's ring is
  drawn facing the same way. A gate of no station is its own frame.

  *Retired:* Gates, stations and `Celestial::JumpPointFacing` are Manifest's. Its neutral part, an
  arrival at rest in a local frame, is restated in [decisions](../decisions.md#transit).

- **Gates keep station by their stations** (2026-11-12, phase 9a). The sample's gates were hundreds
  of millions of kilometres from any station, days at a ship's 150 m/s. Each now orbits its station
  in the station's plane, at its phase and with its period (given, since a station has no gravity to
  derive one), so it stays straight out from the planet beyond the station: Halcyon Gate and Vesper
  Gate 5 km out, half a minute's flight, and Vesper Deep Gate 6 km out at 30° ahead along the orbit,
  so a ship coming out of either sees the planet with the station before it. In a station's frame,
  which does not turn, a gate moves at the orbit's rate times its distance, 1.1 m/s at Corva
  Refinery, which a ship at rest drifts from. Marrow, with no station, has its gate three of
  Ashfall's radii out, where the world is 40° across.

  *Retired:* The sample's star systems and their gates went with the sample.

- **A route is over jump points, with the crossings the caller's** (2026-11-12, phase 9a). A graph of
  systems joined by lanes would make a route of two short jumps across a system whose gates are a
  long way apart cheaper than one long jump. Each jump point is a node instead, its lane an edge at
  the transit time and every other gate of its system an edge at what crossing costs, which the
  galaxy cannot say (it has no positions) and the caller can: `CrossingByCourse` makes it from a
  course through the system's waypoints at a speed.

  *Retired:* Galaxy routes and `CrossingByCourse` are Manifest's.

- **Hulls and pads at the middle of the scales weighed** (2026-11-12, for 9c). A transport up to
  60 m, the largest transport 140 m and the display ships 300 m. Twice that was chosen first and
  set back the same day: its twelve-pad station was three kilometres across, past the cluster range
  (2 km, entry 19), with the gates to move out and a buggy's trip from bay to pad minutes long. At
  this scale a twelve-pad station, four piers of three, is about a kilometre and a half across,
  within the cluster range from its centre, and the gates at 5 and 6 km stay beyond its piers.

  *Retired:* Hull and pad sizes are a game's content; the engine's berth classes are data
  ([decisions](../decisions.md#docks)).

- **Saves from before the docks are refused** (2026-11-12, phase 9c). The shell's world held its
  docking port (`Shell.DockingPort`), which the docks replace, and a world refuses a component it
  does not register; its section held the port too. The section is version 2, without it, and a
  save of 9b's is refused by name rather than loaded into a world with no pads. The engine's own
  formats are unchanged in layout: the world's fixtures load as they did.

  *Retired:* `Shell.DockingPort` and the shell's section went with the sample; formats now change in
  place ([decisions](../decisions.md#persistence)).

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

  *Retired:* A fleet, credits and `App::Owner` are a game's rules. The engine's part, a controller
  and a helm, is restated in [decisions](../decisions.md#simulation).

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

  *Retired:* Cruising, the `Cruise` command and the system scale are Manifest's; the engine's
  neutral successor is a transit passage ([decisions](../decisions.md#transit)).

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

  *Retired:* `RequestTransit`, `ai/Gates.h` and `Ai::TollRule` are gone. The neutral part, a gated
  portal as a slot with a sensor, is restated in [decisions](../decisions.md#transit).

- **All travel between systems is by gates, and a toll is reckoned by distance** (2026-11-14, the
  user's, after 13c). Each gate joins two systems, a lane's two ends, and a ship goes from one system
  to another only through them; the tools' jump (the console's `jump`, the J key, the galaxy panel's
  buttons), which sends a ship along a lane from anywhere in its system, goes once the smoke
  sequence, the reference frames and the tools no longer need it. A lane's toll is no longer its own
  number but the distance between its systems at the galaxy's rate: each system has a position in
  light years (`Celestial::GalaxySystem::position`) and the galaxy a `tollPerLightYear`, so a lane's
  toll follows from where its systems are and the star gates' long lanes are dearer for their
  length rather than their kind. A lane's transit time stays its own.

  *Retired:* Gates between systems and tolls are Manifest's; the console's `jump`, the J key and the
  galaxy panel went with the sample.

## Removed with the docks' cargo (2026-11-23)

- **Cargo is carried in the simulation** (2026-11-12, for 9e). The buggies that carry a trade's
  crates between a station's bays and a ship's hatch decide when the ship can leave, so they are
  simulation state, deterministic and saved, rather than presentation's show of a trade already
  done; every player sees the same buggy at the same place.

  *Retired:* The engine's `dock` now holds berths, vehicles and platforms only; neither repository
  carries buggies or crates.

- **A bay is the middle of its door's sill, on the deck** (2026-11-12, phase 9d), rather than the
  middle of its door: a buggy drives through it there, so the road graph of 9e has its nodes on the
  deck the buggies run on. Highport's and Corva's wall doors moved down onto their piers' decks.

  *Retired:* Bays and the road graph were removed from `dock`.

- **A station's roads are made from its layout** (2026-11-12, phase 9e), rather than drawn in it:
  a road down each pier's side, legs joining each bay and pad to it and to each other where the deck
  allows, and each pad routed once by A*. A layout already says where its decks, pads and bays are,
  and roads written beside them would have to agree with all three; made from them, a mod that
  moves a pad moves its road, and a layout whose pad no bay reaches is refused as it is defined.
  The graph is the layout's, rebuilt as it is defined, and a buggy's route in a save is its nodes,
  which the same layout makes the same.

  *Retired:* The road graph was removed from `dock`.

- **A buggy is out only while it carries** (2026-11-12, phase 9e). It comes out of its pad's bay
  when the pad's ship has cargo for it and goes back in when there is none, an entity only while it
  is on the deck, so a station at rest holds no buggies in its state and costs nothing a step, and
  a pad's buggies are a cap on how many are out at once. Parked buggies are the dressing's, never
  the simulation's: a station whose buggies do not work (`DeckTraffic::working`) moves its cargo at
  the rate working ones would, a load each way at each round trip of the pad's road shared among
  them, so the choice changes what a station costs and not how long it takes, within a load.

  *Retired:* Buggies were removed from `dock`.

- **Buggies give way at nodes by holding them** (2026-11-12, phase 9e). A buggy holds a node while
  it is within 6 m of it, before or after, and one coming up to a node another holds stops short of
  its reach; in a lane it keeps 8 m behind the buggy ahead; the lanes are 4 m apart, so two buggies
  pass in opposite lanes. Who holds a node is where the buggies are, not state of its own, and of two
  that reach for it in one step the first in entity order has it. Every edge is at least twice the
  reach, so a buggy never holds the node ahead while it holds the one behind, and no ring of
  buggies can each wait for the next: twelve pads loading at once at Highport finish.

  *Retired:* Buggies were removed from `dock`.

- **A ship is held until its cargo has crossed** (2026-11-12, phase 9e): until every crate for it
  is aboard and every crate off it has been taken from its hatch, not until the last has reached the
  bay. The trade settled the crates as it was made, so what holds the ship is its hatch: crates on a
  buggy on its way to the bay are the station's to bring in.

  *Retired:* Cargo was removed from `dock`.
