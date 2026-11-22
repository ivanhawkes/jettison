# Samples

Jettison keeps small games of its own to exercise engine capabilities in automated tests; none is
Manifest's production game or part of the reusable engine contract. The ground sample is the one
left. The space-trading sample (`Sample::SpaceGame`, `source/samples/space`, run by Jetsam),
which played the first slices of Manifest's plan from phase 8b, was retired on 2026-11-22 in the
[space extraction](space-extraction-plan.md): Manifest tests all space code, the engine cases only
it held are in the engine's suites, and its scenes, baked static, are the render test's content
(`source/test/content`). Its code, content and what it played are in the history at `3a533dc`.

## The ground sample

A second, smaller game, of no space (G01f): `Sample::GroundGame` in `source/samples/ground`, run by
its own `ground` executable with its content (`scene/main.yaml`) mounted as `base`. It links the
shell alone, which links no space module. A rover, a box under the flight model with no vertical or
sideways thrust, stands on a static slab in a frame of its own whose gravity is the game's
(`Physics::FrameModel`); the player is at its helm, W and S drive it and A and D turn it, from the
game's input context (`Sample::RoverInput`), and a chase rig of the game's (`Scene::ChaseCamera`)
places the scene's camera behind its proxy, which the game builds as each scene loads and the scene
writer leaves out, so a scene saved while it drives is the file loaded. It hears its rover's events, marks its saves and refuses
another game's. Its suite, `sample-ground`, drives the rover on the ground, replays a drive and a save
taken part-way to the same state hash, reads the keys, and runs the game's hooks on a shell of its
own: the proxy built and the scene as authored while it drives, the keys sent, and a save loaded
with W held stopping the rover once the keys are read again. The smoke test runs it after the render
test's two scenes: its piloting stages drive the rover ahead and then turning, each waiting
(`SmokeActions::piloting`) until it is past 1 m/s and then 10 degrees from where it faced, which the
game logs and the smoke test reads.

## Manifest informs capability requirements

Before extending the engine, inspect the sibling `../manifest` checkout using
[game integration](game-integration.md). Its current development stages, gameplay plan,
boundary assessment and numeric contract supersede historical ideas reproduced here.
Do not assume its build status or infer current requirements from an old `idea.md`.

Provision missing reusable capabilities in Jettison public APIs; implement Manifest's
production mechanics in Manifest.

## What comes next

Maintain the ground sample as a small public-API regression consumer. Add gameplay only when a
bounded example is needed to test a new engine capability.

Manifest's order book, economic turns, skill effects, company ownership and gameplay
scripts belong in Manifest. Do not schedule importing them into a sample as an engine
milestone. The engine supplies the mechanisms they need and proves those with
small independent fixtures plus Manifest integration tests. See the
[roadmap](roadmap.md) for the extraction sequence and neutral consumer checks.
