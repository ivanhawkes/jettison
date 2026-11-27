# History

What the engine's finished work was, as it was recorded when each part closed. Planning documents
hold only open work: when a phase, a delivery or a plan is done, its record moves here
([roadmap](roadmap.md) for what is open, [decisions](decisions.md) for what is settled). A history
file is a record of the past, so it is not updated as the code changes; its paths, commits and
numbers are as they were then. For how a system works today, read its feature document, listed
in the [documentation index](README.md).

## Files

<!-- The anchors in the first column keep older links to this file's sections working. -->

| File | Dates | What it holds |
| --- | --- | --- |
| [Phases](history/phases.md) | 2026-11-10 to 2026-11-14 | Phases 0 to 13 and their lettered sub-phases ("9c"), each row's work and exit criterion as met |
| <a id="system-catalogue-entries-131"></a><a id="shaders--phase-5"></a>[System catalogue, entries 1–31](history/catalogue.md) | 2026-11-10 to 2026-11-21 | The briefs the phases built each system to, with what each became; "entry 3" in older text means its entry 3 |
| [After phase 13](history/after-phase-13.md) | 2026-11-18 to 2026-11-20 | The comment review, reference frames that hold under load, the gate in the sandbox, the pre-review's fixes, frames drawn on the GPU offscreen |
| <a id="neutral-sdk-and-manifest-integration"></a>[Neutral SDK and Manifest integration](history/neutral-sdk.md) | 2026-11-15 to 2026-11-25 | Each row that made the engine a neutral SDK or provisioned a gap Manifest found, keyed to Manifest's task IDs |
| [Space extraction](history/space-extraction.md) | 2026-11-22 to 2026-11-23 | The plan that moved the space setting into Manifest and retired the space sample, in eight slices |
| [Planning audit, 2026-11-16](history/planning-audit-2026-11-16.md) | 2026-11-16 to 2026-11-18 | A read-only audit of both repositories' planning, the user's answers, and the brief it ran to |
| [Dev tools theme](history/devtools-theme.md) | 2026-11-17 | The handoff on the theme's last two cross-review findings and their answers |
| [Retired decisions](history/retired-decisions.md) | 2026-11-11 to 2026-11-25 | Decisions no longer in force (the space game's, moved to Manifest, and the superseded), each with why it was retired |
| [Architecture notes](history/architecture-notes.md) | 2026-11-10 to 2026-11-25 | How designs were reached and measured, moved out of the reference documents when they were restructured |

## Timeline

Newest first; each line links the file that records it.

| Date | Milestone |
| --- | --- |
| 2026-11-27 | A game's fonts for the scripts the engine's lack, the user's choice over the engine shipping a CJK face: every `.ttf` and `.otf` under a game's or a mod's `fonts/` a fallback face after the engine's (`UiSystem::LoadFallbackFonts`), and a character no face has logged once with its language and counted a problem; the ui suite draws a generated face's glyph Droid Sans lacks ([UI](ui.md#the-game-ui)) |
| 2026-11-27 | The engine's own text in French and Japanese beside English and German, its saves screen, number format and reasons, moved from Manifest's copies ([UI](ui.md#the-language)) |
| 2026-11-27 | A game's content mounted under its `mod.yaml`'s `identifier`, `base` when it has none, and a mod's `requires` able to name it, so an overlay of Manifest's that requires `manifest` mounts in the shell ([files, packs and mods](files-and-mods.md#the-shells-mounts)) |
| 2026-11-27 | `jettison_use_shared_assets` from a fetching game: the assets directory read from the `jettison_assets` target's `JETTISON_ASSET_DIRECTORY` in place of a variable of the engine's own scope, which Manifest worked around; the consumer project, with `CONSUMER_ASSETS=ON`, calls it and checks the staged assets ([build and gates](build-and-gates.md#the-consumer-check)) |
| 2026-11-27 | <a id="j68"></a>Bindless textures, J68: every material texture in one descriptor array, the texture table (set 1, bound with the frame's; 4096 sampled images, partially bound and updated after bind), a texture taking an element when it finishes and giving it back once no frame in flight reads it; a material's slot indices in its block of the frame's material buffer before its parameters, `material.glsl` sampling by slot; set 1's eight combined samplers and the descriptor pool per material gone; `kMaterialTextureSlots` the program format's limit, at 16; device choice refusing a device without the descriptor indexing it uses, naming the feature (`DescriptorIndexingUnsuitable`). The reference views unchanged on lavapipe, a sixteen-slot fixture program drawn in a new one (`slots`), and the smoke test's reload stage taking and giving back elements with synchronisation validation clean ([rendering](rendering.md#the-texture-table)) |
| 2026-11-27 | <a id="j36"></a>Vulkan 1.4 and the device's capabilities, J36: `kApiVersion` at 1.4, a 1.3 device refused with its reason (`ApiVersionUnsuitable`); one table of the optional features and limits (mesh and task shaders, ray queries, acceleration structures, the cluster extension, 64-bit atomics, indirect draws, `bufferDeviceAddress`, descriptor indexing, `samplerFilterMinmax`) read once and logged, each path switched off by `--no-meshlets`, `--no-ray-tracing`, `--no-mega-geometry` or `render.meshlets`, `render.rayTracing`, `render.megaGeometry`; no path uses it yet. The table logged on lavapipe matches the documented facts; the RTX 5060 Ti was not run ([rendering](rendering.md#the-device-and-its-capabilities)) |
| 2026-11-27 | Crash reports from the engine's other threads, and build IDs, two of J11's bullets: `Platform::ThreadCrashStack` on the I/O worker and the scripts' watchdog, every binary linked with a SHA-1 build ID, each module's in the report, and `scripts/symbolise-crash.sh` finding debug files by it ([runtime](runtime.md#crash-reports)) |
| 2026-11-27 | The server's pack, client-side mods and the content check, J63: `side: client` packages a server leaves out, `Host::WriteServerPack` (the packer's `--server`) for a dedicated server's bundle, and `Host::WorldContent`, which a client checks against its server's, naming the package that differs ([files, packs and mods](files-and-mods.md#the-servers-world-content)) |
| 2026-11-27 | A durable write that refuses to replace a file, J32: `WriteNewFileDurably` decides it in the rename itself (`renameat2`, `RENAME_NOREPLACE`) and throws `FileExistsError` naming the path ([persistence](persistence.md#a-write-that-refuses-to-replace)) |
| 2026-11-27 | A lock's holder named, J34: `FileLock` writes its process ID and program name into its file, and `FileLock::HolderOf` reads them for a refused caller's message ([persistence](persistence.md#a-locks-holder)) |
| 2026-11-27 | Comments better as names, J17: `kFullTurn` is `glm::two_pi<float>()` and `kBelowOne` is `kLargestBelowOne` ([after phase 13](history/after-phase-13.md#the-comment-review)) |
| 2026-11-27 | The player's language from the locale at the first start, J15's bullet, and the ground sample's ground named, J29's ([UI](ui.md#the-language), [samples](sample-game.md#the-hooks-it-uses)) |
| 2026-11-27 | The shell started through a session, J30: a game's world composed once, as a `Host::Game`, and started by its server, its tests and its client alike ([neutral SDK](history/neutral-sdk.md#j30-shell-session)) |
| 2026-11-26 | The way in to a berth, J09: a corridor down the berth's normal flown without stopping, and a reservation kept while its vehicle comes nearer ([neutral SDK](history/neutral-sdk.md#j09-way-in)) |
| 2026-11-26 | Overlap queries and casts that return every hit, J07: `Overlap`, `OverlapSphere`, `CastRayAll` and `CastShapeAll` ([neutral SDK](history/neutral-sdk.md#j07-queries)) |
| 2026-11-26 | Steering among a structure, J08: courses round boxes, a covered berth flown to among a platform's decks and piers, and steering measured for 256 ships in one lane ([neutral SDK](history/neutral-sdk.md#j08-steering)) |
| 2026-11-26 | Picking against the snapshots, J06: a click answered at once, paused or not, a hover outline every frame, and static bodies left out where a game asks ([neutral SDK](history/neutral-sdk.md#j06-picking)) |
| 2026-11-26 | Scripts, J04: a handler reads the snapshot, a script answers a tree's task, a rebuilt module is loaded again, the fuel a call used is counted, and a script's reasons are its own; `wamrc` in the devenv, AOT dropped ([neutral SDK](history/neutral-sdk.md#j04-scripts)) |
| 2026-11-26 | Packs and mods, J05: a patch takes elements out of a list, a pack is reopened when its file changes, and scenes and stylesheets layer ([neutral SDK](history/neutral-sdk.md#j05-packs-and-mods)) |
| 2026-11-26 | Behaviour trees, J03: reactive and parallel nodes, a task's halt, subtrees, a blackboard, a command giving an entity a tree, and trees ticked less often ([neutral SDK](history/neutral-sdk.md#j03-behaviour-trees)) |
| 2026-11-26 | Logging in the shell, J10: the run's log is `jettison.log` in the state directory, at `Info` and above, truncated each run ([runtime](runtime.md#logging)) |
| 2026-11-26 | Dock control a game decides, J35: a game's rules admit or refuse a request and rank a platform's queue ([neutral SDK](history/neutral-sdk.md#j35-dock-control)) |
| 2026-11-26 | Unused code deleted, as the user chose: `Scene::ProxyFactory`, `Ecs::StateMachine` and its tests, and the renderer's `SetClusterRange` and `SetShadowDistance`; each is written again if wanted |
| 2026-11-26 | A script's sender, J02: the game names it per event, and every controller in a command is stamped ([neutral SDK](history/neutral-sdk.md#j02-script-sender)) |
| 2026-11-26 | Encryption, J01: every message sealed to keys made with the server's pinned key ([neutral SDK](history/neutral-sdk.md#j01-encryption)) |
| 2026-11-25 | The documentation restructured: a document per feature, a glossary, history in this folder, the backlog renumbered J01–J29 ([architecture notes](history/architecture-notes.md)) |
| 2026-11-25 | Manifest's Stage 5 closed: a world lists what its save holds, and the Stage 5 review's four gaps provisioned ([neutral SDK](history/neutral-sdk.md)) |
| 2026-11-24 | Durable directories and a world lock, a message longer than a packet, ImGui's backends out of `Devtools` ([neutral SDK](history/neutral-sdk.md)) |
| 2026-11-23 | The space extraction done, all eight slices; the space sample retired ([space extraction](history/space-extraction.md)) |
| 2026-11-22 | The cruise coast phase; the space extraction selected ([neutral SDK](history/neutral-sdk.md)) |
| 2026-11-20 | Low-level networking (`net`), the movement order, a fast-forward over quiet steps ([neutral SDK](history/neutral-sdk.md)); frames drawn on the GPU offscreen ([after phase 13](history/after-phase-13.md)) |
| 2026-11-19 | Manifest's Stage 3 authority gaps closed; map fields, the fit rule and the package rules without a world ([neutral SDK](history/neutral-sdk.md)) |
| 2026-11-18 | The public host, the CPU-only build, the generic shell and the ground sample ([neutral SDK](history/neutral-sdk.md)); the comment review, reference frames under load, the gate in the sandbox ([after phase 13](history/after-phase-13.md)); the planning audit answered ([planning audit](history/planning-audit-2026-11-16.md)) |
| 2026-11-17 | The dev tools themed ([dev tools theme](history/devtools-theme.md)) |
| 2026-11-16 | The planning audit ([planning audit](history/planning-audit-2026-11-16.md)) |
| 2026-11-15 | The neutral SDK priority approved ([neutral SDK](history/neutral-sdk.md)) |
| 2026-11-14 | Phases 12 and 13: saves and the step's interface; crossing a system, its gates and the fleet ([phases](history/phases.md)) |
| 2026-11-13 | Phases 10 and 11: presentation polish; scripting and shipping ([phases](history/phases.md)) |
| 2026-11-12 | Phases 8 and 9: UI, audio and persistence; AI and docks ([phases](history/phases.md)) |
| 2026-11-11 | Phases 3 to 7: runtime architecture, coordinates and render foundations, space rendering, star systems, physics and flight ([phases](history/phases.md)) |
| 2026-11-10 | Phases 1 and 2: layout and scaffolding; core ([phases](history/phases.md)) |