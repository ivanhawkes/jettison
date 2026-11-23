# Space extraction plan

Selected 2026-11-22 (the user's). The engine keeps a concept only when it has a neutral name and is
useful in more than one genre, the games driving it from outside; what is the space setting's alone
moves into Manifest, where future work builds a full game players can mod, and which now tests all
space code. The space sample has served its purpose and is retired: what of it still tests something
nothing else can (the reference frames, the smoke run, large-world precision, a few engine paths)
is folded into neutral fixtures, and the rest goes, with whatever engine code only it used. The
station traffic lanes wait for this ([roadmap](roadmap.md#then-robustness-tools-and-what-a-mod-can-reach)).
Done 2026-11-23, all eight slices; the inventory behind the plan was taken on Jettison `ccc0ae7` and
Manifest `92c62a1`.

## Where things stand

The configure already holds every neutral module to needing none of `celestial`, `space` and
`spacescene` (`jettison_check_optional`, `cmake/JettisonModule.cmake`), and physics reaches star
systems only through the abstract `Physics::FrameModel`, which `space` implements. What is left:

- `celestial` (933 lines): Kepler orbits, star systems, the galaxy, body kinds, jump points, and the
  conversion to render space.
- `space` (2,622): star systems as physics frames, cruises planned round bodies, jump lanes, jump
  gates, galaxy routes. Manifest uses the frames and cruises; it attaches the lanes only because the
  cruise system is ordered after them, and uses no jump, gate or route yet, though it will travel
  between systems by stargates and make short jumps within one (the user's, 2026-11-22).
- `spacescene` (915): a star system's scene and the jump's transition, used by the sample alone.
- `dock` (2,913) counts as neutral but ties station layouts to celestial bodies, and holds a cargo
  deck (crates, bays, buggies, roads) that only the sample uses.
- In neutral modules: about fifteen space-only `Ai::Reason` values; scene files' star-system path and
  `Scene::CelestialBodyComponent`, which the renderer reads; the app's `--galaxy`, `LoadSystem`, its
  smoke sequence's trade and jump, and its orbit targets; `SaveGame`'s migration naming `galaxy` and
  `system`; a public API named for star systems (`Spatial::SystemIndices`); comments throughout; and the ai and dock suites flying in the sample's systems.
- The space sample (7,310 lines of code, its assets, its suite `sample-space`), and jetsam, the
  shell running it; render_test and the smoke run play its scenes.

## What stays in the engine, under a neutral name

| Now | Neutral capability | Other genres |
| --- | --- | --- |
| `Sim::InFrame` | frames: levels, regions, zones (already neutral) | any |
| `Physics::OnRails` on a body | an entity carried on a path its frame's model gives | trains, lifts, moving platforms |
| `Space::CruiseProfile` | a motion profile: accelerate, sustain or coast, decelerate, in closed form | vehicles, lifts, trains |
| the cruise's out-of-physics passage | transit: an entity leaves physics, follows a path the game gives at a profile, is placed each step and fast-forwarded, saved mid-way, and comes out clear of what is there in a frame the game names | fast travel, trips in tycoon and city games |
| jump lanes | portals: an entity out of its frame for a set time, into any frame at a named exit, the same frame's included | doors between levels, teleporters, Manifest's stargates and short jumps |
| a gate's clearance, queue and toll | a gated portal: entered through a trigger volume once cleared from its slots, charged by the game's rule (today's `TollRule`) | toll booths, checkpoints, ticketed transport |
| a pad's reservation and queue, a gate's | slots: held in command order, a queue first come first served, lapsing | parking, berths, workshop bays |
| docks' pads, approach, clamp and release | berths: a class a vehicle fits, flown or driven in and attached to a moving platform | harbours, platforms, car parks |
| steering, `Destination` | vehicle and agent steering (vocabulary only) | any |
| `Physics.Handling` (`Physics.Thrusters` until slice 7), `FlightControls`, `Sim::Helm`, `MayFly` | vehicle handling and the one at its helm, names kept: flight, thrusters and a helm are any vehicle's | flight, driving, submarines, drones |
| `Spatial::SystemIndices`, now `FrameIndices` | spatial indices per frame | any |
| atmosphere, the sky from a sun, horizon culling, render space | sky and atmosphere, large-world culling and precision | every outdoor game, open worlds |
| the app's orbit targets | an orbit camera about any target (`--galaxy` goes with the sample, `--system` and `LoadSystem` with the scene's star system) | any |

## What moves to Manifest

| What | Into |
| --- | --- |
| `celestial`'s orbits, star systems and bodies (render space stays) | a `space` module of Manifest's own |
| `space`'s setting: the star-system frame model, courses round bodies, arrival standoffs and gravity, a station's reach and `ClearOf`, the drives' power law (Astralis's pacing) | the same, on transit and slots |
| station layouts' tie to a celestial body | the same: a station is placed by the game's frame model |
| the galaxy and its routes over the lanes | the same, routing over its own graph by `Ai::FindPath` |
| the space-only reasons, as `Space.Reason`, and their words in both languages | the same, and Manifest's string tables |

Manifest's code that changes with it: `content` (systems, drives, station layouts), `travel`,
`policy` and `company` (pads), each onto the neutral capabilities and its own `space` module.

## What goes, with no consumer left

The sample's gameplay (trading, traders, fleet, repair, stations, piloting, its shell state), its
dressing, jetsam, `spacescene` and the cargo deck. Manifest covers trading, traders, fleets and
travel; nothing else uses the rest. Each stays in the history at the commit that removes it, for
Manifest to take up when its graphical client (L15) needs it. The jump lanes and gates are kept, as
portals (above), since Manifest will travel by them.

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
| the lanes' and gates' suites in the sample's galaxy | the portals' suite, on two frames and a gated door, until Manifest's stargates test them in play |

The ground sample stays, the engine's consumer of no space, and `dependency_probe` with it.

## Rules for every slice

- Saved and reflected names stay as they are ("Ai.Cruising", "Celestial.StarSystem",
  "Physics.Thrusters") until slice 7: C++ names change, bytes do not, so every pinned hash in
  Manifest holds unless a slice says otherwise and shows why, as a reason's key does.
- A vocabulary check joins `jettison_check_optional`: neutral modules' headers and sources may not
  use the setting's words (star system, celestial, galaxy, jump, station, ship, hull, cruise, orbit,
  pad, cargo) outside an allow-list, which each slice shrinks and the last empties.
- Delete before converting: what has no consumer goes first, so no later slice converts it.
- Each slice is a commit with its suites; the gate runs per chunk, Jettison's and Manifest's; Manifest
  moves its SDK pin once per Jettison chunk.

## Slices

1. **The check and the words.** The vocabulary check, with today's uses as its allow-list; neutral
   names in neutral modules' C++ and comments: per-frame indices (`FrameIndices`), rails and frames
   described as any game's, steering's vocabulary (`byShape`, `ShapeLimited`). Names that are any
   vehicle's (flight, thrusters, a helm) and geometry's convex hull stay. Every pin held. **Done**
   2026-11-22: `neutral_words`, the list from 184 entries to 66, each left for the slice that removes
   it (the dock's 39 for slice 5).
2. **The sample folded and retired.** Its kept tests in their new homes, render_test on the static
   scenes, the smoke run's neutral stages, the reference frames held or re-baselined once; then the
   sample, jetsam, `spacescene` and the cargo deck deleted; the lanes' and gates' suites on two
   frames of their own rather than the sample's galaxy. Manifest's pins held. **Done** 2026-11-22,
   in five commits: the suites on `source/test/content` and their own fixtures; render_test the
   shell with no game over static scenes baked once from what the sample built, every reference
   frame recorded again (`effects` and `post` the same to the byte, the planets 1 to 18 pixels
   off, `docked` dropped, `saves-de` for `market-de`); the sample's engine cases in the render,
   app and sim suites; the sample, Jetsam, `spacescene`, the smoke mod and `--galaxy` deleted;
   and the cargo deck, its two components kept registered (below). The lanes' and gates' suites
   already built their systems in code; the dock suite flies in a copy of the sample's Halcyon and
   stations in its fixtures, and its neutral frames fixture is slice 5's. The list at 51.
3. **Reasons, scenes and saves.** The space-only reasons to `Space.Reason` (keys from
   `reason.ai.*` to `reason.space.*`, shown to change no pinned save, or pins moved by that alone);
   the scene's placed-by-an-extension record in place of `CelestialBodyComponent` and the system path;
   `SaveGame`'s migration supplied by the game. **Done** 2026-11-22, in two commits: `Space::Reason`
   ("Space.Reason") holds the 23 reasons lane travel, cruises and gates give, with its own
   `no_such_entity` and `not_controller`, as the dock has, and their words under `reason.space.*`
   in both tables, which move with the module in slice 6; `Ai::Reason` keeps steering's and the
   helm's eight. No pinned hash moved, the engine's or Manifest's (its 53 suites on the local
   engine). The scene's star-system path, `SceneFile::StarSystem` and `CelestialBodies`,
   `Scene::LoadSystem` and `--system` are deleted, since nothing read them once the sample went;
   `CelestialBodyComponent` is `Scene::BodyComponent`, a body by name that a game may place
   (`PlacementComponent`), its record `celestial` until slice 7. The save's migration from
   version 1 stays the engine's, in neutral words: version 1's info was the engine's own record,
   and its two keys are what that version saved, so a game has nothing to supply. The list at 31:
   the smoke sequence's jump stage's five for slice 4, the dock's 26 for slice 5.
4. **Transit, portals, motion profiles and slots.** The neutral capabilities as an engine module, with
   an independent consumer in its suite (a lift and a train on a path, a door between two frames and
   one within a frame, a toll booth's gated portal, a car park's bays); cruises rebuilt on transit and
   the lanes and gates on portals, gated portals and slots, still in Jettison; every Manifest pin
   held, which proves the neutral layer carries the setting. In five parts, each a commit with its
   suites, `Cruising`, `OnLane`, `Gate` and `Transit` keeping their saved fields:
   - **4a, motion profiles.** **Done** 2026-11-22: the neutral module `transit`, its
     `Transit::MotionProfile` the cruise's profile (saved as "Ai.CruiseProfile" until slice 7) with
     `Seconds`, `Covered`, `Thrust` and `CheckProfile`, the closed forms moved unchanged and their
     tests with them; `Space::CruiseDrive` keeps its power law and takes a profile's time from it.
     Manifest names the neutral types; its 53 suites and every pin held on the local engine.
   - **4b, transit.** **Done** 2026-11-22: the cruise's passage as any game's (`transit/Path.h`,
     `Passage.h`): an entity out of physics with its body kept (`TakeOut`), placed each step along a
     way about a carrier the game places (`Underway`, `Passages::Locate`), passed over by
     fast-forward, and brought out at rest clear of what is there (`BringOut`, `ClearPlace`), in a
     local frame the game names; `Transit::Passages` runs them, a lift on a profile and a train on a
     moving ferry in the suite, fast-forwarded and loaded as stepped. The cruise is built on those
     parts and keeps `Cruising`; the lanes and gates take their steps, arrivals and given-up
     destinations from them, and space's private copies are gone. No pinned hash moved; Manifest's
     53 suites on the local engine.
   - **4c, portals.** **Done** 2026-11-22: a portal as any game's (`transit/Portal.h`): an entity out
     of its frame for a set time (`Depart`) and out at an exit the game places, in any frame, the one
     it left included, at rest ahead of the exit and clear of what is there (`ComeOut`, each `Exit`
     from `Portals::Locate`); `Transit::Portals` runs them, a door from a building onto a moving
     ferry and two through a lift door within the building in the suite, fast-forwarded as stepped.
     The lanes are built on those parts and keep `OnLane`. No pinned hash moved; Manifest's 53
     suites on the local engine.
   - **4d, slots and gated portals.** **Done** 2026-11-22: a slot as any game's (`transit/Slot.h`):
     what one entity holds at a time until it gives it up or its hold lapses, with others waiting
     first come first (`Transit::Slot`, `Claim`; `Hold`, `Wait`, `GiveUp`, `DropGone`, `Lapse`,
     `Serve`, `NextLapse`), and a gated portal as a slot with a sensor its holder goes through and
     others pass (`Crossed`); a car park's bays, fast-forwarded as stepped, and a toll booth's
     barrier onto a portal in the suite. The gates are built on those parts, `Gate` and `Transit`
     derived from the slot and claim and keeping their saved fields.
   - **4e, the smoke run's jump stage** removed with its five words, the jump being Manifest's.
     **Done** 2026-11-22: `SmokeActions` has no `jump` or `jumping`, which nothing set once the
     sample went, the sequence is eleven stages, and its piloting stage says what a game pilots
     rather than a ship. The list at 26, the dock's, for slice 5.
5. **Berths.** `dock` without stations' celestial tie: a station layout takes the placement its game
   gives; Manifest's pads on berths; pins held. In three parts:
   - **5a, berths in neutral words.** **Done** 2026-11-23: the dock's C++ in the words of any game's
     platforms, berths and vehicles (`Dock::Platform`, `Berth`, `Vehicle`, `PlatformLayout`,
     `BerthLayout`, `BerthClassDefinition`, `VehicleDefinition`, `RequestBerth`, `ReleaseBerth` and
     the events `Berth*`), its saved and reflected names kept, and its refusals' messages so worded;
     `Docks::MakePlatform` puts a platform on the rails of a body of the frame's model, which is the
     game's placement, the star system no longer named. Rails offset from the body were tried and
     dropped: composing a berth's place with the platform's zero offset and identity turn moved a
     pinned hash in the dock suite. Manifest names the neutral types, its messages' tests reworded; its 53 suites and every pin
     held on the local engine. The list is empty.
   - **5b, the dock suite on neutral frames.** **Done** 2026-11-23: its flights in a frame model of its
     own, platforms on a ferry going round a loop at 8.7 m/s, rather than on the sample's Highport in
     Halcyon; `jettison_space` off its link and the system's file gone. The approach takes the same
     117 s to the point above the berth and 23.6 s down, and the suite's one hash is pinned again for
     it. The case ordering space's lanes and cruise after the docks is the space suite's, which links
     the dock for it.
   - **5c, the ai suite on neutral frames.** **Done** 2026-11-23: steering's flights round a station on
     a ferry of the suite's own frame the same way, rather than round Highport in Halcyon, and
     `jettison_space` off the suite's link; its one hash pinned again for it. No neutral module's suite
     links a space module now.
6. **The move.** `celestial`'s orbits, bodies and galaxy, `space`'s setting and the routes into
   Manifest's `space` module, reflected names unchanged; `celestial`, `space` and `spacescene` leave the engine and its
   optional list. Manifest's suites and pins held on the new SDK pin. **Done** 2026-11-23: both
   modules, their suites and fixtures are Manifest's `space` (`Manifest::Space`,
   `manifest/space/*.h`), celestial's `RegisterTypes` now `RegisterSystemTypes` beside space's;
   the engine has no optional module, so `jettison_check_optional` is `jettison_check_dependencies`,
   the samples' check alone, and `neutral_words` reads every module. The reasons' words under
   `reason.space.*` left the engine's tables with the module; Manifest has no string tables yet, so
   they wait in Jettison's history for its client (L15). Every pin held: Manifest's 54 suites, the
   new `space` among them, on the local engine. Architecture's sections on jumps, gates, routes and
   cruising still describe what is now Manifest's code, for slice 8 to move.
7. **Saved names.** Every saved and reflected name still in the setting's words or the old module's
   (`"byHull"`, `Ai.OnLane`, `Ai.Cruising` and the rest) renamed at once, and `Dock.Transfer`
   and `Dock.Buggy`, kept with no behaviour since slice 2, dropped: one save version and its
   rename table, so a save written before loads, and every pin moved once, shown to be the names
   alone by each hash returning with the old names put back. Before players keep saves or write
   mods against the names (the user's, 2026-11-22). **Done** 2026-11-23: the world's version 5
   renames the engine's types in an older save (`Dock.Pad` to `Dock.Berth`, `Physics.Thrusters` to
   `Physics.Handling` and the rest); each component's chain renames its fields (`Dock.Berth` 3,
   `Dock.Platform`, `Dock.Docking`, `Ai.Destination` and `Physics.Body` 2, the docks' shapes from
   `hulls/` and `stations/` to `vehicles/` and `platforms/`, the physics state's layout 5 with them);
   a command still queued has its fields renamed as it is read; and the two retired storages are read
   and left out. They are renamed by type rather than over the whole table, since Manifest's own
   types share field names such as `ship` and `padClass`. The docks' definitions, the scene's
   `body` (format 3) and the dock reasons' keys moved with them, and `Ai.CruiseProfile` is
   `Transit.MotionProfile`. Manifest's space types are `Space.*`, and it has no saves to migrate.
   Five engine pins moved (sim's two, physics', steering's and the dock approach's) and four of
   Manifest's. Each was shown to be the names alone: with the old names, versions and dropped storages
   put back, every one returned, and Manifest's own tree passed all 54 suites on the engine so
   reverted. A save the build before wrote (`dock/tests/fixtures/saves`) loads to the state this
   build reaches from the same start. Manifest's gate passed on the pin, `1297b9f` fetched, its
   environment matching.
8. **Close-out.** The allow-list empty; architecture, scope, game integration and the sample docs
   rewritten for the boundary as it is; Manifest's boundary assessment's status; the consumer matrix
   (ground, Manifest) run; a cross-review of the whole, as at a stage's end. In three parts:
   - **8a, the last names.** **Done** 2026-11-23: what slice 7 left in the setting's words, and
     `system` where it meant a frame, which the check cannot hold since an ECS system is the
     engine's own word. A pick's field is `frame` (`Physics::Pick::frame`, saved "frame"), a
     queued pick's "system" renamed as the world loads, by the field and under no new version, since
     one would move every pinned hash for a field only a queued pick holds; physics' worlds keyed by
     frame and local frame, `Spatial::FrameIndices::Frames`, steering's locals likewise. The
     reasons a frame and a local frame refuse by are `other_frame` and `other_local_frame` (the
     dock's), `destination_other_frame` and `destination_other_local_frame` (steering's); reasons
     are carried by events and saved nowhere, so no save names the old keys. The engine's string
     tables word a vehicle, a platform and a berth in both languages; the saves list's detail is
     `place`, its column "Place" and "Ort"; the test content's textures are `textures/panels`;
     production comments name the retired sample only for a measurement taken on it. No pinned hash moved, and every reference
     frame held.
   - **8b, the documents.** **Done** 2026-11-23: architecture's boundary as it is, its sections on
     jumps, gates and cruising moved to Manifest's architecture (its "Space"), the system scene, the
     sample's traders, fleet, jumps, piloting and sounds left to the history, and the rest in the
     words of frames, vehicles, platforms and berths, the coordinate frames World, Frame and Local;
     scope's rows of what moved marked Moved; game integration's consumer matrix the ground sample
     and Manifest's suites; the console's help, a shape's refusal and a comment in neutral words.
   - **8c, the close.** **Done** 2026-11-23: Manifest's boundary assessment's status and its
     architecture's Space section (Manifest `6df4647`); the consumer matrix, the ground sample in
     the gate and Manifest's 54 suites on the local engine, every pin held; and a cross-review of
     slices 1 to 8, three rounds over `b4bdb41^` to `c178f1c`. Its first two rounds found slice 7's
     migrations short (a destination inside a queued command, the retired deck's definition
     fields, scenes of version 2 and their system path), fixed in `db22a74` and `c178f1c`; its third
     found the shapes' rename reaching a game's own and a passage's body, which the user settled
     with the policy that formats change in place until player testing ([decided](decisions.md)):
     every step and old fixture deleted (`14fc23a`), no pin moved. Manifest pinned to `14fc23a`
     (`7302d83`), its gate passed on the fetched pin.

## Settled with the user

- **Before W12** (2026-11-22).
- **The static scenes baked once** (2026-11-22, slice 2): what the sample's star systems and station
  dressing built, exported into the scene files, rather than stand-in stations; `docked` dropped,
  `dock-sequence` kept on the static scene's ship and pad, and the German UI frame on the engine's
  own saves screen.
- **The cargo deck's two components kept** (2026-11-22, slice 2): `Dock.Transfer` and
  `Dock.Buggy` stayed registered with no behaviour, so the pins moved once, in slice 7, which
  dropped them.
- **Planetary rings stay in the renderer** (2026-11-22), the one named exception: atmosphere is
  neutral and rings are not, but the renderer has no extension point for a game's own passes and
  Manifest no renderer yet.

## To revisit

Nothing. The save's migration from version 1, left to revisit in slice 3, was deleted with every
other step on 2026-11-23, when formats began to change in place until player testing
([decided](decisions.md)).

## Risks

- Slice 4 is the large one: rebuilding cruises on transit without moving a pin needs the same
  floating-point operations in the same order.
- Renaming in C++ while keeping saved names leaves the two apart; the vocabulary check holds the C++
  side, and a saved name changes later only with a migration.
- The static scenes reproduce today's frames only if their placement matches Kepler's positions at
  the frame's time to the bit; where it does not, those frames are re-baselined.
