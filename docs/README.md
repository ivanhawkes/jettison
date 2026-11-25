# Jettison documentation

Jettison is a C++20 game engine with a Vulkan renderer, built to serve more than one kind of game.
[Manifest](../../manifest/docs/README.md) is its first production game. These documents describe
the engine as the code has it today. Start with the [guide](guide.md) if you are new, or with
[architecture](architecture.md) for the big picture. Words with a particular meaning here are
defined in the [glossary](glossary.md).

## Start here

| Document | Read it for |
| --- | --- |
| [Guide](guide.md) | Worked how-tos: making and releasing a mod, and composing a game on the engine |
| [Architecture](architecture.md) | The overview: who owns what, the repository's layout, the module graph, the split between simulation and presentation, a frame's order, threads and lifetimes |
| [Glossary](glossary.md) | Every term with a particular meaning in the engine, defined once |

## Features

One document per major feature: what it is for, its concepts, how it works, examples of using it,
its limits and the backlog items that would lift them.

| Document | Covers |
| --- | --- |
| [Simulation](simulation.md) | The fixed step, the world, commands, controllers, events, reasons, snapshots, quiet steps and fast-forward, definitions, the host session (`sim`, `ecs`, `time`, `host`) |
| [Scenes](scene.md) | Scene files, layers, proxies of the simulation, camera rigs and the spatial index (`scene`, `spatial`) |
| [Coordinate frames and units](coordinates.md) | Units, frames and local frames, render space, reversed-Z depth and precision |
| [Physics](physics.md) | Jolt bodies and shapes, frames and rails, gravity, the flight model, contacts, casts and picking (`physics`) |
| [Transit](transit.md) | Travel outside physics: passages, motion profiles, portals and slots (`transit`) |
| [Docks](docks.md) | Platforms, berths and vehicles: asking for a berth, the way in, the clamp and leaving (`dock`) |
| [AI](ai.md) | Pathfinding, courses, steering, utility AI, behaviour trees and the helm (`ai`) |
| [Persistence](persistence.md) | Reflection, archives, save games, format versions and migrations, durable writes, settings (`reflect`, `serialise`) |
| [Files, packs and mods](files-and-mods.md) | The virtual file system, mounts, packages, packs, mods, layered definitions and resources (`vfs`, `resource`) |
| [Scripts](scripts.md) | WebAssembly scripts: what they can see and do, their limits, building and hosting them (`script`) |
| [Networking](networking.md) | The server and client over ENet: handshake, tokens, bounds, snapshots and closing (`net`) |
| [Rendering](rendering.md) | The frame, clustered lights, sun shadows, the environment, atmospheres, materials, models, cameras and the Vulkan backend (`render`) |
| [Transparency, particles and post-processing](rendering-effects.md) | Glass, particles, ambient occlusion, bloom, masks, outlines and colour grading (`render`) |
| [UI, input and localisation](ui.md) | Input actions and bindings, the game UI (RmlUi), the console and dev tools (ImGui), string tables and languages (`ui`, `devtools`, `input`) |
| [Audio](audio.md) | Buses, sources, positioned and event sounds (`audio`) |
| [Animation and cinematics](animation.md) | Clips, rigs, clip files and events, and sequences (`animation`, `scene`) |
| [Runtime: core and platform](runtime.md) | Start-up, logging, assertions, configuration, handles, user directories, the CPU check, crash reports, the I/O worker and profiling (`core`, `platform`, `jobs`, `profile`) |
| [The app shell and tools](app-shell.md) | The rendered shell a game runs in, its command line and console, the render test, the editor and the packer (`app`) |

## Working on the engine

| Document | Read it for |
| --- | --- |
| [Scope](scope.md) | What is in the engine, what is out, and what belongs to a game, area by area |
| [Game integration](game-integration.md) | How the engine serves Manifest: the capability map, provisioning a capability, the boundary's checks |
| [Samples and test consumers](sample-game.md) | The engine's own consumers: the ground sample, the test content and the consumer project |
| [Coding standards](coding-standards.md) | Naming, comments, ownership, errors, determinism, dependencies, testing and commits |
| [Build and gates](build-and-gates.md) | Dependencies, building, presets and options, consuming the engine, the gate, tidy, sanitisers, the smoke test, reference frames, the benchmark, profiling, cross-review and CI |

## Plans and records

| Document | Read it for |
| --- | --- |
| [Roadmap](roadmap.md) | Open work only: the current delivery and the numbered backlog, J01 to J29, in priority order |
| [Decisions](decisions.md) | Every settled engine decision, by feature, with its date and why |
| [History](history.md) | The index of finished work: phases, the system catalogue, the neutral SDK work, archived plans and retired decisions |

A change that alters a decision, a contract or a behaviour described here updates its document in
the same commit.
