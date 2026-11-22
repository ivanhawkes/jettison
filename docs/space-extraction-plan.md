# Space extraction plan

Selected 2026-11-22 (the user's). The engine keeps a concept only when it has a neutral name and is
useful in more than one genre, the games driving it from outside; what is the space setting's alone
moves into Manifest, where future work builds a full game players can mod, and which now tests all
space code. The space sample has served its purpose and is retired: what of it still tests something
nothing else can (the reference frames, the smoke run, large-world precision, a few engine paths)
is folded into neutral fixtures, and the rest goes, with whatever engine code only it used. The
station traffic lanes wait for this ([roadmap](roadmap.md#then-robustness-tools-and-what-a-mod-can-reach)).
Planning only so far: no code has moved. The inventory behind it was taken on Jettison `ccc0ae7` and
Manifest `92c62a1`.

## Where things stand

The configure already holds every neutral module to needing none of `celestial`, `space` and
`spacescene` (`jettison_check_optional`, `cmake/JettisonModule.cmake`), and physics reaches star
systems only through the abstract `Physics::FrameModel`, which `space` implements. What is left:

- `celestial` (933 lines): Kepler orbits, star systems, the galaxy, body kinds, jump points, and the
  conversion to render space.
- `space` (2,622): star systems as physics frames, cruises planned round bodies, jump lanes, jump
  gates, galaxy routes. Manifest uses the frames and cruises; it attaches the lanes only because the
  cruise system is ordered after them, and uses no jump, gate or route.
- `spacescene` (915): a star system's scene and the jump's transition, used by the sample alone.
- `dock` (2,913) counts as neutral but ties station layouts to celestial bodies, and holds a cargo
  deck (crates, bays, buggies, roads) that only the sample uses.
- In neutral modules: about fifteen space-only `Ai::Reason` values; scene files' star-system path and
  `Scene::CelestialBodyComponent`, which the renderer reads; the app's `--galaxy`, `LoadSystem`, its
  smoke sequence's trade and jump, and its orbit targets; `SaveGame`'s migration naming `galaxy` and
  `system`; ship words in public APIs (`Sim::Helm`, `MayFly`, `Physics.Thrusters`,
  `SystemIndices`); comments throughout; and the ai and dock suites flying in the sample's systems.
- The space sample (7,310 lines of code, its assets, its suite `sample-space`), and jetsam, the
  shell running it; render_test and the smoke run play its scenes.

## What stays in the engine, under a neutral name

| Now | Neutral capability | Other genres |
| --- | --- | --- |
| `Sim::InFrame` | frames: levels, regions, zones (already neutral) | any |
| `Physics::OnRails` on a body | an entity carried on a path its frame's model gives | trains, lifts, moving platforms |
| `Space::CruiseProfile` | a motion profile: accelerate, sustain or coast, decelerate, in closed form | vehicles, lifts, trains |
| the cruise's out-of-physics passage | transit: an entity leaves physics, follows a path the game gives at a profile, is placed each step and fast-forwarded, saved mid-way, and comes out clear of what is there in a frame the game names | fast travel, trips in tycoon and city games |
| a pad's reservation and queue | slots: held in command order, a queue first come first served, lapsing | parking, berths, workshop bays |
| docks' pads, approach, clamp and release | berths: a class a vehicle fits, flown or driven in and attached to a moving platform | harbours, platforms, car parks |
| steering, `Destination` | vehicle and agent steering (vocabulary only) | any |
| `Physics.Thrusters`, `FlightControls`, `Sim::Helm`, `MayFly` | vehicle handling and an operator's seat | flight, driving, submarines, drones |
| `Spatial::SystemIndices` | spatial indices per frame | any |
| atmosphere, the sky from a sun, horizon culling, render space | sky and atmosphere, large-world culling and precision | every outdoor game, open worlds |
| the app's `--galaxy`, `LoadSystem`, orbit targets | a world to load and an orbit camera about any target | any |

## What moves to Manifest

| What | Into |
| --- | --- |
| `celestial`'s orbits, star systems and bodies (render space stays) | a `space` module of Manifest's own |
| `space`'s setting: the star-system frame model, courses round bodies, arrival standoffs and gravity, a station's reach and `ClearOf`, the drives' power law (Astralis's pacing) | the same, on transit and slots |
| station layouts' tie to a celestial body | the same: a station is placed by the game's frame model |
| the space-only reasons, as `Space.Reason`, and their words in both languages | the same, and Manifest's string tables |

Manifest's code that changes with it: `content` (systems, drives, station layouts), `travel`,
`policy` and `company` (pads), each onto the neutral capabilities and its own `space` module.

## What goes, with no consumer left

The sample's gameplay (trading, traders, fleet, repair, stations, piloting, its shell state), its
dressing, jetsam, `spacescene`, the cargo deck, and the jump lanes, gates, galaxy and routes with
their reasons and words. Manifest covers trading, traders, fleets and travel; nothing else uses the
rest. Each stays in the history at the commit that removes it, for Manifest to take up when its
graphical client (L15) or travel between systems needs it, rebuilt on the neutral capabilities.

## What of the sample is kept, and where

| Test now | Kept as |
| --- | --- |
| render_test's scenes (orbit, vesper, effects, post) and the reference frames | static renderer test scenes under `source/test`, planets, rings, stations and the effects and post benches placed where they are now, by the scene's own placement; the frames held to the byte where the placement reproduces today's, the rest re-baselined once and shown to the user |
| the smoke run (resizes, a reload, flight, a pick, a save, a trade, a jump) | the same run on those scenes and the ground sample, but the trade and the jump, which are Manifest's |
| large world: a station at real scale drawn within a hundredth of a pixel | the render suite, the planet and station placed by numbers |
| mounts: a data mod in the user's directory over a pack | the host suite, on a test mod over the ground sample's content; the drive-profile mod becomes Manifest's |
| reasons: every reason worded in English and German, arguments worded | the ui suite, for the engine's own reasons; Manifest's for its own |
| piloting: a keyboard, a gamepad and a HOTAS fly alike; a collision mesh from drawn shapes | the input and physics suites, on a neutral vehicle and a mesh |
| a pad's clamps' sounds from the snapshots on the effects bus | the audio suite, if it has no case of a snapshot's event sounded |
| a mod's script weighing choices and hearing events; a document's data and buttons | the script and ui suites, each only where theirs do not already cover it |
| the ai and dock suites' flights in the sample's systems | a neutral frames fixture in those suites |

The ground sample stays, the engine's consumer of no space, and `dependency_probe` with it.

## Rules for every slice

- Saved and reflected names stay as they are ("Ai.Cruising", "Celestial.StarSystem",
  "Physics.Thrusters"): C++ names change, bytes do not, so every pinned hash in Manifest holds unless
  a slice says otherwise and shows why, as a reason's key does.
- A vocabulary check joins `jettison_check_optional`: neutral modules' headers and sources may not
  use the setting's words (star system, celestial, galaxy, jump, station, ship, hull, cruise, orbit,
  pad, cargo) outside an allow-list, which each slice shrinks and the last empties.
- Delete before converting: what has no consumer goes first, so no later slice converts it.
- Each slice is a commit with its suites; the gate runs per chunk, Jettison's and Manifest's; Manifest
  moves its SDK pin once per Jettison chunk.

## Slices

1. **The check and the words.** The vocabulary check, with today's uses as its allow-list; neutral
   names in neutral modules' C++ and comments: vehicle handling, the operator's seat, per-frame
   indices, rails and frames described as any game's, steering's vocabulary; the app's `--world`.
   Every pin held.
2. **The sample folded and retired.** Its kept tests in their new homes, render_test on the static
   scenes, the smoke run's neutral stages, the reference frames held or re-baselined once; then the
   sample, jetsam, `spacescene`, the cargo deck and the jump lanes, gates, galaxy and routes deleted,
   the cruise ordered after physics alone. Manifest drops its lanes' attach; its pins held.
3. **Reasons, scenes and saves.** The space-only reasons to `Space.Reason` (keys from
   `reason.ai.*` to `reason.space.*`, shown to change no pinned save, or pins moved by that alone);
   the scene's placed-by-an-extension record in place of `CelestialBodyComponent` and the system path;
   `SaveGame`'s migration supplied by the game.
4. **Transit, motion profiles and slots.** The neutral capabilities as an engine module, with an
   independent consumer in its suite (a lift and a train on a path, a car park's bays); cruises
   rebuilt on them, still in Jettison; every Manifest pin held, which proves the neutral layer carries
   the setting.
5. **Berths.** `dock` without stations' celestial tie: a station layout takes the placement its game
   gives; Manifest's pads on berths; pins held.
6. **The move.** `celestial`'s orbits and bodies and `space`'s setting into Manifest's `space`
   module, reflected names unchanged; `celestial`, `space` and `spacescene` leave the engine and its
   optional list. Manifest's suites and pins held on the new SDK pin.
7. **Close-out.** The allow-list empty; architecture, scope, game integration and the sample docs
   rewritten for the boundary as it is; Manifest's boundary assessment's status; the consumer matrix
   (ground, Manifest) run; a cross-review of the whole, as at a stage's end.

## Open, for the user

- **Order against W12.** Before W12, as "soon" suggests, or after it. W12 is mostly trade and touches
  travel little, so either order works.
- **Planetary rings.** Atmosphere is neutral, rings are not, but the renderer has no extension point
  for a game's own passes, and Manifest no renderer yet. Keep rings in the renderer as the one named
  exception until both exist.
- **Jump lanes and gates deleted** rather than kept for Manifest's travel between systems: nothing
  uses them, and they are rebuilt on transit when that travel is planned.

## Risks

- Slice 4 is the large one: rebuilding cruises on transit without moving a pin needs the same
  floating-point operations in the same order.
- Renaming in C++ while keeping saved names leaves the two apart; the vocabulary check holds the C++
  side, and a saved name changes later only with a migration.
- The static scenes reproduce today's frames only if their placement matches Kepler's positions at
  the frame's time to the bit; where it does not, those frames are re-baselined.
