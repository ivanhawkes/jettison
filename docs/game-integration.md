# Game integration

How the engine serves [Manifest](../../manifest/README.md), its first production game: who owns
what, how a game composes the engine, which capability serves which mechanic, how a missing one is
provisioned, and the checks that hold the boundary.

## Responsibilities

Jettison provides reusable capabilities for many game types. Manifest owns its gameplay and data:
companies, economic rules, prices, progression, scenarios, travel policy, its UI and its scripts'
permissions. The space setting is Manifest's. No simulation, physics, AI or scene service may
require a space-game model or sample content. The boundary itself is set out in
[architecture](architecture.md#responsibility-boundary); what is in and out of the engine is in
[scope](scope.md).

The engine's own [consumers](glossary.md#consumer) test its APIs ([samples and test
consumers](sample-game.md)). Reusable renderer resources are engine assets; a game's markets, maps,
vehicles, music and screens are its own.

Manifest is a design and acceptance input, never an engine source or build dependency. It builds the
engine from source, pinned to an engine commit
([consuming the engine](build-and-gates.md#consuming-the-engine)): `459af99` on 2026-11-25.

## How a game composes the engine

A game reaches the engine through two public seams. It passes its composition in explicitly; the
engine never finds a game by itself.

| Seam | For | Header |
| --- | --- | --- |
| `Host::Session` and `Host::Game` | A game with no presentation: a server, a tool, a test. The [session](glossary.md#session) owns one game's file system, definitions and world. | `source/engine/host/include/jettison/host/Session.h` |
| `App::Game`, passed to `App::RunRenderApplication` | A game drawn on screen: the [shell](glossary.md#shell) with its renderer, UI, audio and dev tools, and the game's hooks at the shell's points. | `source/engine/app/include/jettison/app/Game.h` |

`Host::Session` runs the game's hooks in a fixed order of phases and names the phase that fails
([the host session](simulation.md#the-host-session)); the consumer project composes a small race
this way ([a game on the host](simulation.md#a-game-on-the-host)). `App::Game` is the same idea for
the shell: every hook does nothing by default, so a game overrides only what it needs, and the
shell with no game is the engine with nothing of a game in it. A program's `main` passes its game
to `RunRenderApplication` ([a program's main](app-shell.md#a-programs-main)); the hooks the ground
sample uses are in [samples](sample-game.md#the-hooks-it-uses).

## Checking Manifest before a capability

For each engine integration task, read the local `../manifest` checkout, not GitHub, which lags.
Record its branch, its commit and any uncommitted requirements that bear on the task. Read its
`AGENTS.md`, then:

1. `docs/delivery-order.md`, the task queue and its status; then `docs/development-stages.md` and
   the relevant `docs/stage-*-plan.md` for acceptance criteria.
2. `docs/gameplay-and-server-plan.md`, `docs/architecture.md` and `docs/host-script-boundary.md`
   for ownership, commands, simulation and scripting.
3. `docs/engine-game-boundary-assessment.md`, the inventory of what crosses the boundary.
4. `docs/market-prices.md`, `docs/model.md`, the public interfaces under `source/` and the
   definitions under `data/`, when the mechanic depends on units, schemas or existing rules.

Use Manifest's current, approved requirements, not old sample notes or assumptions about `main`.
If the checkout is unavailable, obtain the files at an identified revision. Do not invent
requirements, and do not make the engine's build fetch Manifest.

## The capability map

Before changing an API, map each mechanic the task needs to what provides it. Each row below names a
mechanic, the engine's reusable provision for it, and what stays Manifest's.

| Mechanic | Jettison provides | Manifest owns |
| --- | --- | --- |
| Validated, layered content | The VFS, packs and mods; reflected YAML archives and patches; the [definition registry](glossary.md#definition-registry); migrations and diagnostics that name the file and field ([files, packs and mods](files-and-mods.md), [persistence](persistence.md)) | Its schemas, IDs, reference checks, balance and scenarios |
| Company-owned cargo and trading | ECS state, [commands](glossary.md#command) applied in the [fixed step](glossary.md#fixed-step), serialisation ([simulation](simulation.md)); platforms, berths and vehicles ([docks](docks.md)) | Ownership, units, inventory, reservations, prices, fees and settlement |
| Physical delivery | [Frames](glossary.md#frame-coordinates) with their own gravity and rails, deterministic movement ([physics](physics.md)); optional docking ([docks](docks.md)) | Destination choice, travel pacing, access policy and cargo eligibility |
| Travel between places | `transit`: [slots](glossary.md#slot) (one holder at a time, a queue behind it), [portals](glossary.md#portal) (leave a frame, come out at a named exit), [passages](glossary.md#passage) (carried along a path out of physics) and [motion profiles](glossary.md#motion-profile) ([transit](transit.md)) | Its lanes, gates, cruises and tolls, built on these |
| Economic AI and scripts | Utility scoring, behaviour trees and steering ([AI](ai.md)); sandboxed WebAssembly scripts with grants and fuel ([scripts](scripts.md)) | The actions offered, the facts scored, trader policy and the capabilities granted |
| Persistent worlds | World save, load and state hash, save sections and details, migrations ([persistence](persistence.md)); durable writes and a file lock (`core/File.h`, `core/FileLock.h`, [runtime](runtime.md)) | Content compatibility, the accepted-command journal, acknowledgement and retry |
| A networked server | `net`: `Net::Server` and `Net::Client` on ENet over UDP; a hello naming the protocol and version; bearer tokens; each session's snapshot filtered on the server; message limits ([networking](networking.md)) | Its principals and their tokens, what each player may see, its messages and their handling |

For each row the current task needs:

1. Name the public API that exists.
2. Demonstrate what is missing, with a failing case.
3. Specify the smallest reusable contract that closes it.
4. Give a Manifest acceptance test, and an independent consumer or fixture test in the engine.

Avoid promoting a game rule merely by making its constants configurable: a pricing formula is still
game policy. Conversely, Manifest does not copy an archive, scheduler, VFS or physics integration of
its own because the engine's public seam needs a bounded improvement.

## Provisioning a capability

A gap Manifest needs is provisioned in Jettison's code, not worked round in the game. The
implementer writes the engine part; the game mechanic is written in Manifest on top of it.
Implementation follows the user's model policy (`AGENTS.md`).

```mermaid
flowchart LR
    A[Manifest task names a gap] --> B[Engine provision and its fixture test]
    B --> C[Manifest built against the local engine checkout]
    C --> D[Engine commit, gate green]
    D --> E[Manifest's pin moved to that commit]
```

The diagram shows one provision's path. The engine's test needs no Manifest; the
[gate](glossary.md#gate-ci) is green before the commit; Manifest's [pin](glossary.md#pin) moves
with its matching development environment. The delivery is a
[roadmap](roadmap.md#current-deliveries) row while it is open, and goes to the
[history](history.md) once done.

Keep dependency and ABI ownership where it is. Do not rewrite every subsystem at once, or add
speculative support for every genre.

## What remains of the boundary

The extraction that made the engine neutral is done ([history](history.md)). Two conventions remain,
each optional:

- The shell names two pieces of content by path: the default scene, `scene/main.yaml` unless the
  game names another (`App::Game::DefaultScene`), and its music, `audio/music/drift.flac` where the
  content has it (`source/engine/app/src/Application.cpp`).
- Several engine suites read the test content (`source/test/content`) as test data.

## Enforcement

These checks hold the boundary; where each runs is in [separation
checks](build-and-gates.md#separation-checks).

- **Dependency checks.** `jettison_check_dependencies` (`cmake/JettisonModule.cmake`) walks each
  engine module's full link closure at every configure, and refuses one that reaches a sample's
  target, so engine code cannot come to need a sample.
- **Neutral words.** The `neutral_words` suite (`cmake/CheckNeutralWords.cmake`) keeps the space
  setting's words out of the engine's code outside string literals, so a space concept cannot
  creep back in under its own name. Its allow-list, `cmake/neutral-words.txt`, is empty.
- **Neutral start-up.** The shell with no game registers no game's traders, markets or fleet; the
  `app` suite's `Game` cases check it.
- **Engine assets.** The same cases hold the engine's assets to its own directories, with none of
  the test content in them, so no production consumer falls back to a sample asset.
- **The consumer matrix.** Small API tests, not products. A new capability adds to it.

| Consumer | What it shows | Where |
| --- | --- | --- |
| A CPU race | Definitions, commands, steps, a save, a load and teardown through `Host::Session`, with no transform or star system; then served over loopback with tokens | `source/test/consumer`; a manual check, not in the gate ([consuming the engine](build-and-gates.md#consuming-the-engine)) |
| A physics room | Physics in a frame of its own gravity, with no space | The `physics` suite |
| The ground sample | A vehicle on the ground under a frame model's gravity, through `App::Game` | The `sample-ground` suite and its smoke-test stages |
| Manifest | All space code, its suites and pinned hashes on the local engine | Manifest's gate |
| A CPU configure | The engine with presentation off: no ImGui, Vulkan or glslc | The `clang-cpu` preset, a gate stage |

The matrix does not pass merely because one consumer works, or because a static linker discarded
unused sample symbols. The implementer runs the relevant presets and gates
([the gate](build-and-gates.md#the-gate)), and records the evidence and the limits that remain.
