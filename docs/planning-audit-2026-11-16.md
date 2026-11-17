# Planning audit, 2026-11-16

Read-only audit of Jettison and Manifest planning, per `docs/planning-audit-handoff.md`.
Jettison `feature-base` at `0564f19`, Manifest `feature-base` at `49a761b`, both as the handoff
recorded. Manifest pins `f850920`. **Cut short:** the user's weekly allowance ran to 3% during
the read, so this is a partial audit; "Method and limits" says what was and was not covered.

## Verdict

The plan is sound enough to start Stage 1 (H01–H07) and the Jettison extraction slices 1–2.
The two repositories agree on the extraction order, the CPU-only consumer, the pin-update
workflow and the docking seam, and every code citation checked holds at HEAD (the pin lag is
documentation-only: `826c4be`→`f850920` changed `.gitignore` alone, `f850920`→`0564f19` docs
alone). Three things want settling first: who provisions the dedicated server and transport
that Manifest orders before Stage 4 and Jettison still has as "deferred, nothing replicated"
(A01); the balance numbers copied into three Manifest documents against the ledger's own rule,
one of which has already drifted (A02); and the Jettison roadmap's standing decision that the
sample "plays Manifest's plan slice by slice", which the new contract contradicts but does not
mark superseded (A03).

## Capability map

Mechanic → public Jettison API → gap → owner → acceptance evidence. "Checked" means the API
or gap was confirmed in the source at `0564f19`.

| Manifest need (stage) | Public Jettison provision today | Gap | Owner | Acceptance evidence |
| --- | --- | --- | --- | --- |
| Host lifecycle, explicit composition (1, H02) | `Sim::SimWorld`, `Sim::DefinitionRegistry`, `Vfs::FileSystem`; no host owner. `App::RunRenderApplication` takes no composition; `Application.cpp:700` registers trading, `:1189` loads trade definitions, `:1201` fleet, `:1228` traders (checked) | Public CPU host owner with ordered startup/failure cleanup | Jettison | Engine lifetime/failure tests; Manifest `--check-startup` matrix (stage-1 plan) |
| Package metadata and engine-version check (1, H02) | Private `app/src/Mounts.*` (checked present) | Extract reusable parsing/version check | Jettison; identity policy Manifest | Directory/pack metadata equivalence; contextual rejection |
| CPU-only configure (1, H03) | `source/CMakeLists.txt:10–13` returns when `PLUGIN_IMGUI` is off, before `add_subdirectory(external)`; top-level `find_package(Vulkan REQUIRED COMPONENTS glslc)` at `CMakeLists.txt:36`, `source/external/CMakeLists.txt:2` (checked) | Presentation/editor/samples optional; packer retained (`jettison_add_pack`, `source/CMakeLists.txt:33`, checked) | Jettison | Fresh configure without Vulkan/glslc/ImGui; target-closure inspection |
| Independent non-space consumer (1, H04) | `source/test/consumer` links `Jettison::Engine`, `GIT_TAG main` fallback (checked) | Tiny neutral consumer at an explicit revision | Jettison | Definitions, stepping, save round trip, teardown |
| Sample extraction (1, H05) | `app/src/{Trading,Traders,Fleet,MarketScreen}.*` in the engine app library (checked) | Explicit sample target and pack | Jettison | Trader-route regression; neutral startup registers no sample systems |
| Layered content, migration (2) | `YamlArchive::ReadLayered`, `DefinitionRegistry`, `YamlMigrations`, patches | None demonstrated | Manifest | Directory/pack identity, fingerprint, validator |
| Neutral frame identity and save metadata (3, A01) | `sim/Snapshot.h:36 InSystem`; `sim/SaveGame.h:52,54` `galaxy`, `system`; `SimWorld.h` includes `Snapshot.h` (checked) | Neutral partition/frame and game save sections, by migration | Jettison | Existing fixtures load; Manifest state binds to neutral names |
| Fixed-step commands with sender, retry, results (3) | `Sim::Controller` on every command (13d), `SimWorld::Apply`, `Sim::Reason` | Request IDs, retry history, structured results are game policy | Manifest | Authorisation/atomicity/replay tests with TUI off |
| Generic physics/AI/scene without celestial (4, W01) | `physics`, `ai`, `scene` CMake all depend on `celestial`; `ai` also on `physics` (checked) | Split targets; optional space adapters | Jettison | Non-space physics scene consumer; generic AI without transit |
| Docking, berths, admission (4, W05; later L04) | `dock/Definitions.h:25–26`: pad size and hull class, "a hull lands on a pad of its class or larger" (checked) | D-044's "smaller berth" gap depends on whether classes are a closed engine enum (A04); admission/queue hooks for L04 undemonstrated | both | Starter/compact berth fixture; policy in Manifest |
| Scripts: inputs, handlers, store (4–5) | `HostLimits` fuel 10,000,000, 256 pages, 250 ms watchdog (checked); no `get:`/`each:` snapshot imports in `script/` (checked, matches both documents' "future work") | Snapshot reads and handler player identity | Jettison, not scheduled by stage | Binding fixtures; replay without duplicate handlers |
| Persistence, durable I/O (5) | `SimWorld::Save/Load/StateHash`, migration chains, durable whole-file writes | Journal/catalogue, world lock: policy in Manifest; R02 may demonstrate a primitive | Manifest; Jettison on demonstration | Crash/fault tests, cross-build replay |
| Dedicated server, remote clients, filtered snapshots (N01–N03, before Stage 4) | Nothing. `scope.md:216–217`: replication "Deferred … nothing is replicated", local server "Reframed" as in-process | Whole capability unowned (A01) | **User decision** | None recorded |

## Findings

### A01 — Major: nobody schedules the network/dedicated-server provision Manifest orders before Stage 4

- Manifest: `../manifest/docs/delivery-order.md:105–118` (N01–N03, order 28–30, prerequisite of
  W10), `roadmap.md:82–101` ("provision demonstrated generic host/network gaps in Jettison"),
  `development-stages.md:329–333`, ledger D-011/D-012 (owner "both"/"Manifest").
- Jettison: `docs/scope.md:212–217` keeps replication deferred and the server reframed as the
  in-process split; `docs/roadmap.md:17–22` has no row for a headless host loop, transport,
  snapshot filtering or bounded queues; `roadmap.md:643–646` "no networking is built yet".
- Evidence: N02 depends on an engine provision no Jettison row produces (check 5: work blocked on
  an engine revision no task makes). D-011 is owned by "both" with no Jettison counterpart.
- Fix: N01 must produce the capability map before any code, so the minimum is a Jettison roadmap
  row "Networked host: pending Manifest N01 capability map" and a `scope.md` Network verdict
  that names it. Decide now whether transport/authentication is Manifest host code over a
  Jettison queue seam, or an engine module.
- Owner: decision for the user.

### A02 — Major: balance numbers are restated in three Manifest documents, and one copy has drifted

- `../manifest/docs/decision-ledger.md:6, 122–123` ("numeric values are never duplicated");
  `roadmap.md:29–30` ("no numeric balance value is added to prose").
- Copies: `development-stages.md:102–156, 163–175` and `roadmap.md:151–259` restate the
  consumption rates (500 kg, 3,000 l, 5 kg per 1,000 people per day), condition rates (−6/−12,
  +3/+6), the work-rate multiplier, 72 server-hours, 90 kg + 10 kg bronze inputs, 30 kg oxygen,
  30 occupant-days, 20/4 IBC holds; the ledger's canonical locations are the economy, contract
  and small-ship plans.
- Drift: `development-stages.md:174–175` says the 10% supplier bond "is selected", while D-042
  records "numeric defaults proposed, not accepted". `roadmap.md:180–183` adds a 25,000-person
  region that `development-stages.md:129–130` says is unselected.
- Fix: replace the restated values in both documents with ledger IDs and links (T2/T3), and settle
  the bond rate's status in D-042.
- Owner: Manifest (the bond status is the user's).

### A03 — Major: the Jettison roadmap still decides that the sample plays Manifest's plan

- `docs/roadmap.md:474–478` ("The sample game is Manifest's plan … slice by slice as the engine
  systems that each needs arrive"), `:783–801` (gates moved "to Manifest's places"), `:69–71`.
- Against: `docs/game-integration.md:15–20`, `docs/sample-game.md:62–72` ("Do not schedule
  importing them into this example"), `../manifest/docs/engine-game-boundary-assessment.md:121`
  ("Do not keep sample and Manifest gameplay in lockstep").
- `roadmap.md:291–294` says the 2026-11-15 contract supersedes conflicting decisions "where they
  conflict" but names none; a reader of the Decided list finds a live instruction to grow the
  sample from Manifest's plan. The auto-memory of this assistant carries the same stale fact.
- Fix: mark the 2026-11-12 entry superseded in place, pointing at game integration.
- Owner: Jettison.

### A04 — Minor: D-044's smaller-berth gap is real, but its shape is "classes as data", not one more pad

- `../manifest/docs/decision-ledger.md:69` (D-044 "gap demonstrated"), `development-stages.md:168–169`.
- `source/engine/dock/include/jettison/dock/Definitions.h:27` is a closed `enum class PadClass`
  with `LargestHull`, `PadSize` and `HatchAft` per class in engine code (checked). So the gap
  holds, and the policy-free provision is pad sizes and hull fit as definition data, so Manifest
  authors its compact berth without an engine enumerator per hull class (game-integration's
  "constants configurable" rule cuts the other way here: a size table is content, not a rule).
- Fix: word D-044's provision as "pad classes become data"; W01/W05 then need no engine edit per
  new hull.
- Owner: Manifest (wording); Jettison (provision).

### A05 — Minor: Manifest assigns Jettison work that Jettison does not record

- `../manifest/docs/delivery-order.md:44` (M03, commit `lazygit` to Jettison's `devenv.nix`,
  D-054); `lazygit` is in `../manifest/devenv.nix:15` and absent from Jettison's (checked).
- Jettison's roadmap and build documents have no maintenance row for it, nor for the three-file
  environment parity Manifest's D-006 requires of the pinned checkout.
- Fix: a one-line maintenance row in Jettison's current-priority table.
- Owner: Jettison.

### A06 — Minor: T7 is a hard gate on Stage 1 while the prose says it is not a dependency

- `../manifest/docs/delivery-order.md:14–15` versus the table at `:46, :54` (H01 must complete
  M05). With credits scarce, editor-example repair (M05) sits between the user and the first
  engine slice.
- Fix: either move M05 to "travels with the tasks" or keep it and say why it must precede H01.
- Owner: decision for the user.

### A07 — Minor: the Jettison current-priority rows are not keyed to Manifest's queue

- `docs/roadmap.md:17–22` names four deliveries with exit criteria but no owner, date, Manifest
  task ID (H02–H05, A01, W01) or named test target; `development-stages.md`/`delivery-order.md`
  have all of these. Row 3 bundles A01-type (frame/save) and W01-type (module split) work that
  Manifest wants at different stages.
- Fix: add the IDs and the stage each row unblocks; split row 3 in two.
- Owner: Jettison.

### A08 — Minor: Jettison's "Open decisions: none" is stale

- `docs/roadmap.md:852–856` says nothing is open; the current priority leaves neutral frame
  identity, save metadata sections, the host API shape and the network seam undecided
  (`game-integration.md:76–78`, `../manifest/docs/stage-1-host-plan.md:104–111`).
- Fix: list them, each with what it blocks (A01, H02) and when it re-enters.
- Owner: Jettison.

### Status of the project-analysis findings

Pin lag: current, harmless until H07 (documentation-only, see verdict). T1/T9: recorded done
in `session-handoff.md:180`. T10, T5, T7: selected, not implemented (D-052–D-055), ordered
M01–M05. T4 → C04. T2/T3/T6/T8 travel with the tasks. Not read within budget:
`project-analysis-2026-11-16.md` itself, so "now fixed" claims there were not checked.

### Checked and found consistent

Extraction order (`game-integration.md:69–81` ≡ H02–H05, A01, W01); CPU-only consumer wording;
pin workflow (slice 4 ≡ H07/stage-1 slice 5); all D-0xx references resolve to ledger rows; the
68-row queue IDs are unique; the `#t1t9-local-verification--2026-11-16` anchor resolves; cited
Manifest commits `f8014e5`, `f63a3a4`, `ee83ea5`, `2716380`, `728bb95` exist; `0564f19` is the
commit that added `game-integration.md`, as the stage-1 plan says; Manifest builds simulation
targets with `-ffp-contract=off` (`cmake/ManifestModule.cmake:33`) and has the suites D-001/D-002
name; `MarketplaceOptions` and `data/trade-items/` exist; the proposed grain IDs do not collide
with the existing seed IDs; real-elapsed-time rules (D-030, stargates) are recorded as host
inputs, not clock reads, which respects the determinism contract; `defs/scripts/` is the
engine's registry convention (`script/ScriptDefinition.h:65`), supplied by mods, so
`host-script-boundary.md:157` is right; Jettison `feature-base` is pushed (0 ahead of origin),
so H07's fetch from `github.com/ivanhawkes/jettison` can proceed.

## Questions for the user

1. Who owns the dedicated server's transport, authentication and snapshot filtering: Manifest
   host code over a Jettison queue/snapshot seam, or a Jettison module? (A01)
2. Is the 10% performance bond selected or still proposed? (A02)
3. Does editor-example repair (T7/M05) stay a hard gate before H01? (A06)

### Answers, 2026-11-18

1. Jettison provides the low-level networking common to a wide range of networked games;
   Manifest owns its dedicated server and the mechanics specific to it. Recorded as
   Manifest's D-057, Jettison's `scope.md` Network table and current-priority row.
2. Still proposed: D-042 stands, and `development-stages.md` now links to it instead of
   stating the rate.
3. Not a gate: H01 follows M04, and M05 travels alongside Stage 1 (`delivery-order.md`).

## Method and limits

Read in full: Manifest `development-stages.md`, `delivery-order.md`, `roadmap.md`,
`decision-ledger.md`, `stage-1-host-plan.md`, `engine-game-boundary-assessment.md`,
`host-script-boundary.md`; Jettison `game-integration.md`, `roadmap.md`, `scope.md`,
`sample-game.md`, `coding-standards.md`. Code checked by grep/sed at `0564f19` as listed above.

Not read (allowance exhausted): Jettison `architecture.md`, `build-and-gates.md`, `history.md`
(so "Phases 0–13 done" was not spot-checked beyond the roadmap's own 13a–13d entries); Manifest
stage 2–5 plans, `gameplay-and-server-plan.md`, `standalone-mod-plan.md`, every system plan,
`model.md`, `architecture.md`, `scope.md`, `agent-workflow.md`, `idea.md`, `AGENTS.md`,
`project-analysis-2026-11-16.md`, `session-handoff.md`. Checks 4 (stage completeness) and 6
(determinism of the economy plans, numbers in prose versus T4 data) were therefore done only
from the sequence documents and the ledger. No build or gate was run; nothing but this file
was written.
