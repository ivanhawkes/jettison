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
