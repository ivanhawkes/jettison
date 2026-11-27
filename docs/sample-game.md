# Samples and test consumers

Jettison keeps three small consumers of its own, to test engine capabilities through the public
interfaces an external game uses. None is Manifest's game or part of the engine's contract, and none
is a second economy to grow beside Manifest's.

| Consumer | What it is | Where |
| --- | --- | --- |
| The ground sample | A small game drawn on screen: a rover driven on the ground | `source/samples/ground` |
| The test content | Static scenes and assets that `render_test`, the shell with no game, draws | `source/test/content` |
| The consumer project | A CPU-only race, composed into `Host::Session` and served over loopback | `source/test/consumer` |

Each is a row of the consumer matrix in [game integration](game-integration.md#enforcement). The
space-trading sample was retired on 2026-11-22, Manifest testing all space code
([history](history.md)).

## The ground sample

A game of no space, composed into the [shell](glossary.md#shell), to prove that the shell serves a
game that is not Manifest's: its own world, scene, input, camera, saves and smoke-test stages, with
nothing of space linked. Its world, `Sample::GroundWorld`, is a `Host::Game` the shell starts
through a session; its presentation, `Sample::GroundGame`, is an `App::Game` over it
(`source/samples/ground/include/jettison/sample/GroundGame.h`). Both are run by the `ground` executable,
with its content mounted as `base` over the engine's assets. It links the shell alone
(`Jettison::App`).

What it plays:

- A rover, a box of 800 kg, stands on a static slab in a [frame](glossary.md#frame-coordinates) of
  its own. The frame's gravity is the game's, a `Physics::FrameModel` (`src/Ground.cpp`).
- The player is at its [helm](glossary.md#helm). W and S drive it and A and D turn it, through the
  game's input context (`Sample::RoverInput`). The flight model drives it, with no vertical or
  sideways thrust.
- The engine's chase-camera rig (`Scene::ChaseCamera`), placed by the game's own camera system,
  follows behind the rover's proxy. The game
  builds that proxy as each scene loads, and the scene writer leaves it out, so a scene saved while
  it drives is the file as authored.

Its scene is `scene/main.yaml` in its content (`source/samples/ground/assets`). The ground there is
drawn as its physics shape has it:

```yaml
  - id: ground
    transform:
      translation: [0, -0.5, 0]
      scale: [200, 0.5, 200]
    mesh:
      shape: box
      color: [0.35, 0.45, 0.3]
```

### The hooks it uses

Its world's hooks are `Host::Game`'s ([the host session](simulation.md#the-host-session)):

| Hook | What `GroundWorld` does |
| --- | --- |
| `RegisterTypes` | The simulation's types |
| `RegisterWorld` | The components the shell draws, physics, the helm, and the ground's and the rover's shapes and gravity, in the order the shell registered its own before |
| `CreateInitialState` | The ground and the rover on it, the local player (`App::kLocalPlayer`) at its helm |

`App::Game` (`source/engine/app/include/jettison/app/Game.h`) is the contract the sample exists to
exercise. Every hook does nothing by default. These are the ones the ground sample overrides:

| Hook | When the shell calls it | What the ground sample does |
| --- | --- | --- |
| `Register` | As the shell is made: the game's input actions | The drive keys |
| `RegisterFrame` | Once the shell's frame systems are registered | Its camera system, after the shell's rigs |
| `SceneLoaded` | Each scene loaded, as it starts and as it reloads | Binds the rover's proxy, takes the drive context |
| `Loaded` | A save loaded into the world | Finds the rover again and takes the saved controls as the last sent |
| `WriteSave`, `ReadSave` | A save written; a save about to load | Marks its saves with a `ground` detail, and refuses a save without it |
| `PollInput` | The input phase | Sends the rover's controls when the keys change |
| `Hears` | Each event: whether the player hears it | The physics events that name the rover; the sample maps no sounds to them yet (it has no `defs/sounds`) |
| `Describe` | An entity named in the log | Names the rover and the ground |
| `PickIn` | Where a click picks | In the ground's frame |
| `DriveSmoke` | The render test's stages | Drives the rover ahead, then turning |

The other hooks (UI, panels, events taken, names for reasons, proxies synced by the
game, shutdown) are described in [the app shell](app-shell.md).

### Its tests

Its suite, `sample-ground` (`source/samples/ground/tests/GroundTests.cpp`), starts the world through
a session over the sample's content, as the shell does, and checks that:

- the rover drives ahead and turns on the ground, under gravity, at its helm;
- the same drive gives the same world, and a save taken part-way carries on to it;
- W and S drive the rover, and A and D turn it, in the game's context;
- the game drives the rover from the keys, and a save loaded with W held stops it once the keys are
  read again;
- the game loads its own saves, and refuses another game's.

The [smoke test](glossary.md#smoke-test) runs `ground` after the render test's scenes
(`scripts/smoke-test.sh`). Its piloting stages drive the rover ahead, then turning. Each waits
(`SmokeActions::piloting`) until the rover is past 1 m/s, then 10 degrees from where it faced, which
the game logs and the smoke test reads.

## The test content

`source/test/content` is the engine's test data: static scenes of a planet and a station at real
scale (`orbit.yaml`), a ringed giant (`vesper.yaml`), benches for the transparent draws and the
post chain (`effects.yaml`, `post.yaml`), with their materials, textures and models, a clip, a camera
sequence, a colour grade and a few sounds. It is what the retired space sample built, baked into
files.

`render_test` (`source/test/main.cpp`) is the shell with no game, mounting this content as `base`.
Its scenes are the [reference frames](build-and-gates.md#reference-frames) listed in
`source/test/frames/views.txt`, and several engine suites read them as test data. Its pack,
`content.pack`, is built for the smoke test's packed run.

## The consumer project

`source/test/consumer` is the smallest game build: a separate CMake project that fetches the engine
with presentation off and Vulkan refused. It runs a race with nothing of space in it through
`Host::Session`: definitions, commands, steps, a save, a load and teardown. Then it serves the race
over loopback through `Net::Server`, with bearer tokens, sealed to its server's pinned key. Its composition is shown in [a game on the
host](simulation.md#a-game-on-the-host); how to run it is in [the consumer
check](build-and-gates.md#the-consumer-check).

## When to add to a sample

Keep each consumer a small regression test of public APIs. Add gameplay only when a bounded example
is needed to test a new engine capability, and then through the interfaces an external game uses.
Manifest's order book, economy, skills, company ownership and gameplay scripts stay in Manifest, and
importing them into a sample is never an engine milestone. Before extending the engine, check
Manifest's current requirements ([game
integration](game-integration.md#checking-manifest-before-a-capability)).

## Limits and open work

- The ground sample's look is not held by a reference frame: [J28](roadmap.md#j28).
- The ground has no name in the log, and the rover slides rather than grips: [J29](roadmap.md#j29).

## Source

- `source/samples/ground`: `GroundGame.h`, `RoverInput.h`, `src/Ground.cpp`,
  `tests/GroundTests.cpp`.
- `source/test`: `main.cpp` (`render_test`), `content/`, `frames/`, `consumer/`.
- `source/engine/app/include/jettison/app/Game.h`,
  `source/engine/host/include/jettison/host/Session.h`.
