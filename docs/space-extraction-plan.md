# Space extraction plan

Selected 2026-11-22 (the user's): the engine keeps a concept only when it has a neutral name and is
useful in more than one genre, the games driving it from outside; what is the space setting's alone
moves out. Where it goes: into Manifest for what Manifest uses (its two game modes, Astralis and
Manifest, share it), and into the space sample for what only the sample uses. The sample is rebuilt
on the neutral capabilities (option 2 of the three offered), so nothing in the engine is kept for its
sake. The station traffic lanes wait for this ([roadmap](roadmap.md#then-robustness-tools-and-what-a-mod-can-reach)).
Planning only so far: no code has moved. The inventory behind it was taken on Jettison `ccc0ae7` and
Manifest `92c62a1`.

## Where things stand

The configure already holds every neutral module to needing none of `celestial`, `space` and
`spacescene` (`jettison_check_optional`, `cmake/JettisonModule.cmake`), and physics reaches star
systems only through the abstract `Physics::FrameModel`, which `space` implements. What is left is
the setting's vocabulary and data in neutral modules, and three optional modules that are the
setting itself:

- `celestial` (933 lines): Kepler orbits, star systems, the galaxy, body kinds, jump points, and the
  conversion to render space.
- `space` (2,622): star systems as physics frames, cruises planned round bodies, jump lanes, jump
  gates, galaxy routes.
- `spacescene` (915): a star system's scene and the jump's transition.
- `dock` (2,913) counts as neutral but holds station layouts tied to celestial bodies and a cargo
  deck (crates, bays, buggies, roads) that only the sample uses.
- In neutral modules: about fifteen space-only `Ai::Reason` values; scene files' star-system path and
  `Scene::CelestialBodyComponent`, which the renderer reads; the app's `--galaxy`, `LoadSystem`, its
  smoke sequence's jump and its orbit targets; `SaveGame`'s migration naming `galaxy` and `system`;
  ship words in public APIs (`Sim::Helm`, `MayFly`, `Physics.Thrusters`, `SystemIndices`); and
  comments throughout.

## What stays in the engine, under a neutral name

| Now | Neutral capability | Other genres |
| --- | --- | --- |
| `Sim::InFrame` | frames: levels, regions, zones (already neutral) | any |
| `Physics::OnRails` on a body | an entity carried on a path its frame's model gives | trains, lifts, moving platforms |
| `Space::CruiseProfile` | a motion profile: accelerate, sustain or coast, decelerate, in closed form | vehicles, lifts, trains |
| the cruise's out-of-physics passage | transit: an entity leaves physics, follows a path the game gives at a profile, is placed each step and fast-forwarded, saved mid-way, and comes out clear of what is there in a frame the game names | fast travel, trips in tycoon and city games |
| jump lanes | portals: out of one frame for a set time, into another at a named exit | doors between levels, teleporters |
| a gate's clearance and queue, a pad's reservation | slots: held in command order, a queue first come first served, lapsing, with the game's charge rule | parking, berths, workshop bays, entry queues |
| a gate's ring | a trigger volume that sends what crosses it through its portal | any |
| docks' pads, approach, clamp and release | berths: a class a vehicle fits, flown or driven in and attached to a moving platform | harbours, platforms, car parks |
| steering, `Destination` | vehicle and agent steering (vocabulary only) | any |
| `Physics.Thrusters`, `FlightControls`, `Sim::Helm`, `MayFly` | vehicle handling and an operator's seat | flight, driving, submarines, drones |
| `Spatial::SystemIndices` | spatial indices per frame | any |
| atmosphere, the sky from a sun, horizon culling, render space | sky and atmosphere, large-world culling and precision | every outdoor game, open worlds |
| the app's `--galaxy`, `LoadSystem`, orbit targets | a world to load and an orbit camera about any target | any |

## What moves

| What | To | Why there |
| --- | --- | --- |
| `celestial` but render space: orbits, star systems, galaxy, bodies | Manifest, a `space` module of its own | its content and travel read them |
| `space`'s setting: the star-system frame model, courses round bodies, arrival standoffs and gravity, a station's reach, galaxy routes, the drives' power law (Astralis's pacing) | Manifest's `space` | its travel, on transit and portals |
| `spacescene` | the space sample | Manifest has no graphical client yet (L15), which takes it from the sample when it is built, rather than carrying unused code |
| `dock`'s station layouts' tie to celestial bodies | Manifest, and the sample, each its own | what a station is, is the game's |
| `dock`'s cargo deck: crates, bays, buggies, roads | the space sample | Manifest has its own cargo and uses none of it |
| the space-only `Ai::Reason` values | each with its owner, as `Space.Reason` | a reason belongs to the module that refuses |
| scene files' star-system path, `CelestialBodyComponent` | a neutral placed-by-an-extension record, which the game's frame model fills | the scene stops knowing what a system is |
| `SaveGame`'s `galaxy`/`system` migration | a migration step the game supplies | the keys are a game's |
| the smoke sequence's jump stage | the space sample's own smoke run | the app's sequence keeps the neutral stages |

Manifest's code that moves to or changes with this: `content` (systems, drives, station layouts), `travel`,
`policy` and `company` (pads), each through the neutral capabilities and its own `space` module.

## The space sample, rebuilt on the neutral capabilities

The sample stays the engine's space-flavoured consumer and its renderer coverage: render_test, the
reference frames and jetsam all run on its scenes. It keeps its gameplay by building it on transit,
portals, slots and berths, with what is the setting's in sample code: a small frame model placing its
stations and planets, a straight course to a station's standoff, its jump routes on `Ai`'s path
finding, its cargo deck, and `spacescene`. Its own code grows by roughly what it takes over
(an estimated 2,000 lines, `spacescene` and the cargo deck most of it); the engine shrinks by about
4,500 lines plus the cargo deck.

## Rules for every slice

- Saved and reflected names stay as they are ("Ai.Cruising", "Celestial.StarSystem",
  "Physics.Thrusters"): C++ names change, bytes do not, so every pinned hash in both repositories
  holds unless a slice says otherwise and shows why, as a reason's key does.
- The reference frames hold to the byte through the move.
- A vocabulary check joins `jettison_check_optional`: neutral modules' headers and sources may not
  use the setting's words (star system, celestial, galaxy, jump, station, ship, hull, cruise, orbit,
  pad, cargo) outside an allow-list, which each slice shrinks and the last empties.
- Each slice is a commit with its suites; the gate runs per chunk, Jettison's and Manifest's; Manifest
  moves its SDK pin once per Jettison chunk.

## Slices

1. **The check and the words.** The vocabulary check, with today's uses as its allow-list; neutral
   names in neutral modules' C++ and comments: vehicle handling, the operator's seat, per-frame
   indices, rails and frames described as any game's, steering's vocabulary; the app's `--world`
   (with `--galaxy` kept as an alias until the sample and Manifest move). Every pin held.
2. **Reasons, scenes and saves.** The space-only reasons to `Space.Reason`, their string tables with
   them (keys move from `reason.ai.*` to `reason.space.*`, shown to change no pinned save, or the pins
   moved by that alone); the scene's placed-by-an-extension record in place of
   `CelestialBodyComponent` and the system path, the renderer reading it; `SaveGame`'s migration
   supplied by the game. Reference frames held.
3. **Transit, portals and slots.** The neutral capabilities as an engine module, with an independent
   consumer in its suite (a lift and a train on a path, a door between two frames, a car park's
   bays); `space`'s cruises, lanes and gates rebuilt on them, still in Jettison; every pin held,
   which proves the neutral layer carries the setting.
4. **Berths.** `dock` split: berths, approach, clamp and release stay; station layouts take a
   placement the game gives rather than a celestial body; the cargo deck moves into the sample.
   Manifest's pads on berths; pins held.
5. **The move.** `celestial` and `space`'s setting into Manifest's `space` module, its reflected
   names unchanged, `spacescene` and the setting's code into the sample; the engine's optional list
   loses the three. Manifest's suites and pins held on the new SDK pin; the sample's suites and the
   reference frames held.
6. **Close-out.** The allow-list empty; architecture, scope and game integration rewritten for the
   boundary as it is; Manifest's boundary assessment's status; the consumer matrix (ground, the
   sample, Manifest) run; a cross-review of the whole, as at a stage's end.

## Open, for the user

- **Order against W12.** Before W12, as "soon" suggests, or after it. W12 is mostly trade and touches
  travel little, so either order works; before means W12's fixture is written against the moved code.
- **Planetary rings.** Atmosphere is neutral, rings are not, but the renderer has no extension point
  for a game's own passes. Keep rings in the renderer as the one named exception until it has one,
  or build that point in this plan (larger).
- **Orbits in the sample.** Its frames hold to the byte only if it places bodies exactly as now, so
  it keeps a copy of the Kepler evaluation (about 150 lines). The alternative is circular orbits and
  every reference frame re-baselined once.

## Risks

- Slice 3 is the large one: rebuilding cruises on transit without moving a pin needs the same
  floating-point operations in the same order.
- Renaming in C++ while keeping saved names leaves the two apart; the vocabulary check holds the C++
  side, and a saved name changes later only with a migration.
- The sample's estimate of what it takes over is rough; `spacescene`'s and the cargo deck's share was
  counted, its course and frame model guessed.
