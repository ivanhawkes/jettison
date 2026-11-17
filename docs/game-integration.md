# Game integration and responsibility contract

Approved direction, 2026-11-15. This document sets the target boundary and implementation
workflow. Extraction and consumer checks described here are pending, but for the CPU lifecycle
consumer (H04); documentation does not establish that the current engine is already genre-neutral.

## Responsibilities

Jettison provides reusable game-engine capabilities for multiple game types. Manifest
owns its production gameplay and data: companies, civilisations, economic rules, prices,
progression, scenario content, travel policy, game UI and script permissions. Optional
space capabilities may live in Jettison, but generic simulation, physics, AI and scene
services must not require a space-game model or sample content.

The small space-trading example may remain in this repository to exercise engine APIs
and support automated testing. Put its rules, registration, scripts and content in
explicit sample/test targets outside `source/engine`. The neutral engine cannot depend
on them. The example is neither Manifest's production implementation nor a second
economy that must grow in parallel with it. Reusable renderer resources remain engine
assets; example markets, galaxies, ships, music and trading screens are sample assets.

## Check Manifest before implementing capabilities

For each engine integration task, inspect the sibling `../manifest` checkout and record
its branch/commit and relevant uncommitted requirements. Read its `AGENTS.md`, then:

1. `docs/development-stages.md` and the relevant `docs/stage-*-plan.md` for delivery order
   and acceptance criteria.
2. `docs/gameplay-and-server-plan.md`, `docs/architecture.md` and
   `docs/host-script-boundary.md` for ownership, commands, simulation and scripting.
3. `docs/engine-game-boundary-assessment.md` for the boundary inventory and planned splits.
4. `docs/market-prices.md`, `docs/model.md`, public interfaces under `source/game/`, and
   relevant `data/` definitions when the mechanic depends on units, schemas or existing rules.

Use current approved requirements rather than old sample-game notes or assumptions
about `main`. If the checkout is unavailable, obtain the relevant files at an identified
revision; do not invent requirements or make the engine build fetch Manifest implicitly.
Manifest is a design/acceptance input, not an engine source or build dependency.

Produce a capability map before changing an API:

| Required mechanic | Reusable Jettison provision | Manifest responsibility |
| --- | --- | --- |
| Validated layered content | VFS, reflected YAML archives, definition registry, migration and useful diagnostics | Game schemas, IDs, reference checks, balance and scenario definitions |
| Company-owned cargo and trading | ECS state, fixed-step commands, serialisation and spatial/docking mechanics | Ownership, units, inventory, reservations, prices, fees and settlement |
| Physical delivery | Physics spaces/frames, deterministic movement, reusable optional docking/transit | Destination choice, travel pacing, access policy and cargo eligibility |
| Economic AI and scripts | Generic utility scoring, behaviour trees, sandboxed execution | Offered actions, facts, trader policy and granted capabilities |
| Persistent worlds | World save/load/hash, migrations and durable I/O primitives | Content compatibility, accepted-command journal and acknowledgement/retry policy |

For each row needed by the current task, name the public API that exists, demonstrate
the missing capability, specify its smallest reusable contract, and give a Manifest
acceptance test plus an independent consumer/fixture test. The implementer must provision
required reusable gaps in Jettison code, not stop at telling the game to work around them.
Game-specific mechanics are implemented in Manifest using that provision. No OpenAI
implementation is authorised by this workflow; honour the user's model policy.

Avoid promoting a game rule merely by making its constants configurable. A pricing
formula is still game policy. Conversely, do not duplicate an existing archive, scheduler,
VFS or physics integration in Manifest because its public seam needs a bounded improvement.

## Current gaps and extraction order

The current `app` library contains Trading, Traders, Fleet and game UI code. AI combines
generic algorithms with cruise/gates/lanes; physics and scene depend on celestial
concepts; snapshots/save metadata carry star-system fields. Assets combine renderer
resources and sample gameplay. `JETTISON_PRESENTATION=OFF` configures the CPU engine without
graphics tools (H03, 2026-11-18).

Implement the boundary in reviewable slices:

1. Public genre-neutral lifecycle and explicit game composition; move sample code and
   registration out of the engine app library. Jetsam selects the small example explicitly;
   Flotsam may open that project but must not activate its economy by default.
2. Separate engine resources from sample packs, and make samples/editor/presentation
   optional. A CPU consumer must configure without ImGui, Vulkan/glslc or sample content.
3. Split generic AI/physics/scene facilities from optional space adapters. Resolve neutral
   spatial identity and game save metadata before Manifest binds durable state to them.
   Keep save/schema compatibility through explicit migrations rather than silent renames.
4. Provision the public capabilities required by Manifest's current stage, validate
   against its local SDK override, commit the engine revision, then update Manifest's
   immutable pin and matching development environment. Preserve dependency/ABI ownership.

Do not rewrite every subsystem at once or add speculative support for every genre.
Preserve current sample/render/movement tests during extraction. A small sample may
exercise gameplay, but must use the same public interfaces as an external game.

## Enforcement and completion

Add dependency checks for public headers and CMake target closures: no core-to-game,
core-to-sample or neutral-core-to-space-extension edges. Assert that neutral startup
registers no sample traders, markets or fleet systems. Inspect resource-pack contents
against explicit manifests; no sample asset fallback is allowed in a production consumer.

The consumer matrix must include a tiny CPU board-game-like simulation with no galaxy
or transforms, a non-space physics scene without celestial definitions, and the explicit
space example. Exercise definitions, stepping and save/load, plus coherent options with
samples/tests/editor/presentation disabled. These are small API tests, not new products.

The implementer runs relevant presets and required gates at the machine's own core count,
clang-tidy included, reading `MemAvailable` before a full-parallel run
([the gate](build-and-gates.md#the-gate)). Document actual evidence and
remaining limitations. The planned matrix does not pass merely because the existing
space sample works or a static linker discards unused sample symbols.
