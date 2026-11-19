# Game integration and responsibility contract

Approved direction, 2026-11-15. This document sets the boundary and the workflow. The
extraction it describes (H02–H05, A01, W01) is done and is in Manifest's SDK pin; the
[history](history.md#neutral-sdk-and-manifest-integration) records what each step built.

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
economy that must grow in parallel with it. It has one rule beyond trading, damage and its
repair in a dry dock (2026-11-18, the user's decision), there to exercise a pad's features. Reusable renderer resources remain engine
assets; example markets, galaxies, ships, music and trading screens are sample assets.

## Check Manifest before implementing capabilities

For each engine integration task, inspect the sibling `../manifest` checkout and record
its branch/commit and relevant uncommitted requirements. Read its `AGENTS.md`, then:

1. `docs/delivery-order.md` for the task queue and its status, then
   `docs/development-stages.md` and the relevant `docs/stage-*-plan.md` for acceptance criteria.
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

## Remaining gaps and extraction order

Slices 1–3 below are done ([history](history.md#neutral-sdk-and-manifest-integration)). What
remains of the boundary: the shell still names some content by path, each optional (the default
scene, the galaxy, the music, the station hum, a station's dressing), and several engine suites
read the sample's content as test data. Slice 4 recurs with every Manifest stage.

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

These checks hold the boundary, and each exists:

- **Dependency checks:** at every configure, `jettison_check_optional` refuses a core-to-sample
  edge, and a neutral-module-to-space edge in a target closure or a public header.
- **Neutral start-up:** it registers no sample traders, markets or fleet. The `app` suite's `Game`
  cases check this.
- **Engine packs:** their contents are held to the engine's own directories. No production
  consumer may fall back to a sample asset.
- **The consumer matrix:**
  - a CPU race with no galaxy or transforms, served over loopback with tokens (`source/test/consumer`);
  - a non-space physics room (the `physics` suite);
  - the explicit space sample;
  - a CPU configure with presentation off (`clang-cpu`).

  These are small API tests, not new products. A new capability adds to this list.

The implementer runs relevant presets and required gates at the machine's own core count,
clang-tidy included, reading `MemAvailable` before a full-parallel run
([the gate](build-and-gates.md#the-gate)). Document actual evidence and
remaining limitations. The planned matrix does not pass merely because the existing
space sample works or a static linker discards unused sample symbols.
