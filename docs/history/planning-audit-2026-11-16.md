# Planning audit, 2026-11-16

A read-only audit of Jettison's and Manifest's planning, run 2026-11-16 and cut short, with the
user's answers and a follow-up of 2026-11-18, when it closed. The brief it ran to, written the same
day, follows it as [Planning audit handoff](#planning-audit-handoff). Paths and commits in both are
as they were then.

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

### Follow-up, 2026-11-18

- A03: the 2026-11-12 decision is marked superseded in the roadmap; the assistant's memory is corrected.
- A04: D-044 now words the provision as pad classes and hull fit as data (Jettison W01).
- A05: lazygit was already in Jettison's shell (5ebfc9b) and the three environment files match
  Manifest's; recorded as a done maintenance row (M03). T5's parity check (M04) is Manifest's.
- A07: the current-priority rows carry Manifest's task IDs and stages; row 3 is split into A01
  (frame and save seams) and W01 (module split), and the first row into H02 and H05.
- A08: the open decisions are listed, each with what it blocks.
- A02: done 2026-11-18. Manifest's `development-stages.md` and `roadmap.md` link each balance
  value to its canonical plan, and the roadmap's region of 25,000 people is gone.

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

## Planning audit handoff

Written 2026-11-16 for a fresh session. The user approved the plan below; the session that
wrote it did no audit work beyond finding the documents and sizing them.

### The task

Audit the planning of Jettison and its sibling game Manifest (`../manifest`) for
**correctness** (claims that are wrong, stale or contradict each other or the code) and
**completeness** (work the plans need but do not schedule, own or accept). The output is
a findings list. The audit is read-only: it changes no planning document, code, test,
data or build file, and runs no build or gate.

- **Model:** Claude Fable 5.1, effort `xhigh` (`/model`, then `/effort xhigh`). The value is
  in reasoning across about 30 documents in two repositories, where a missed contradiction
  is built into months of work. Fable 5.1 costs about 2.5 times as much per token as Opus 5.5,
  and credits are tight, so keep to the scope below.
- **Afterwards:** Opus 5.5 at effort `high` applies the fixes the user accepts. That is a
  separate session and is not part of this one.
- **Policy:** Both repositories assign planning and review to read-only agents
  (`../manifest/docs/agent-workflow.md`, `CLAUDE.md`). This audit follows that rule: it
  writes one document, the findings file, and nothing else.

### State at handoff

| Repository | Branch | HEAD | Tree |
| --- | --- | --- | --- |
| Jettison | `feature-base` | `0564f19` | clean apart from this file |
| Manifest | `feature-base` | `49a761b` | clean |

Manifest pins Jettison at `f850920`, two commits behind the engine's HEAD. If either HEAD
has moved, record the new one in the findings and audit what is there.

### Scope

About 190k words of planning, roughly 250–280k tokens, which fits in context. The code
does not fit (Jettison alone is about 87k lines of C++), so read only the source files a
claim needs, through clangd (the LSP tool) or `grep`.

**Jettison** (`docs/`):

- `roadmap.md`: the current priority, the carried-forward tiers, the decided items and
  the open decisions. The system catalogue only where a row cites it.
- `game-integration.md`, `architecture.md`, `scope.md`, `sample-game.md`.
- `build-and-gates.md` and `coding-standards.md`: only to check plans against the rules.
- `history.md`: spot checks of "done" claims only, not a full read.

**Manifest** (`../manifest/docs/` unless stated):

- Sequence and status: `development-stages.md`, `delivery-order.md`, `roadmap.md`,
  `decision-ledger.md`, `stage-1-host-plan.md` to `stage-5-recovery-plan.md`.
- Boundary: `engine-game-boundary-assessment.md`, `host-script-boundary.md`,
  `gameplay-and-server-plan.md`, `standalone-mod-plan.md`.
- System plans: `economy-and-development-plan.md`, `planetary-development-plan.md`,
  `regional-economy-and-governance-plan.md`, `procurement-and-station-contracts-plan.md`,
  `market-information-plan.md`, `market-prices.md`, `stargate-expansion-plan.md`,
  `small-trader-and-docking-plan.md`.
- Context: `model.md`, `architecture.md`, `scope.md`, `agent-workflow.md`,
  `../manifest/idea.md`, `../manifest/AGENTS.md`.
- `project-analysis-2026-11-16.md`: an earlier assessment from today. Treat it as one more
  document to check, not as settled.
- `session-handoff.md`: only the sections dated 2026-11-16 (up to about line 335). The
  older sections are marked superseded.

**Out of scope:** dependency sources, `build/`, `.claude/` agent definitions, and the
licence and README files except where a plan cites them.

### What to check

1. **Each repository against itself.** Do the roadmap, stages, delivery order, decision
   ledger and current handoff agree on order, status and prerequisites? Does each selected
   decision have one canonical statement, as Manifest's ledger rules require, or are there
   copies that have drifted? Are task IDs (T1–T10, M01, the 68-row queue) and decision IDs
   (D-0xx) unique, and do the references resolve?
2. **The engine–game contract.** Build the capability map that `agent-workflow.md` asks
   for: each mechanic Manifest's Stages 1–5 need, the public Jettison API it consumes (or
   would consume), the gap, its owner and the acceptance evidence. Then check it in both
   directions:
   - Does Manifest need something that Jettison's roadmap and `game-integration.md` do not
     schedule?
   - Does Jettison plan to put game policy into the engine (rules, prices, ownership,
     content), against `game-integration.md`?
   - Do the two repositories describe the extraction order, the CPU-only consumer, the
     docking seams and the pin update the same way?
3. **Claims against the code.** Spot-check "done", "met", "implemented" and "complete" claims,
   and the file paths, targets, headers and line numbers the plans cite. Priorities: the
   Jettison phase 0–13 exit criteria that Manifest depends on; Manifest's T1/T9 completion
   record; and the `project-analysis` findings that say something is now fixed. Also check
   claims of absence, such as "the engine does not yet provide X".
4. **Completeness of each stage.** Does every stage and delivery have an exit criterion,
   an owner, and acceptance tests in both an independent engine consumer and Manifest, as
   `CLAUDE.md` requires? Do open decisions name what they block and when they re-enter?
   Does any later pillar (networked server, governance, procurement, stargates and the
   rest) displace Stage 1–4 work, against the scope funnel rule?
5. **Feasibility and order.** Look for dependency cycles, prerequisites scheduled after
   the work that needs them, and work blocked on an engine revision that no task produces.
6. **Plans against the rules.** Do the plans for simulation-side systems (decimal prices,
   fixed-step commands, replay, hashing, saves) respect the determinism contract in
   `coding-standards.md` and the save and schema migration rules? Do any plans put
   balance numbers in prose when they should be authored data (Manifest T4)?

Findings already recorded in `project-analysis-2026-11-16.md` (T1–T10, the pin lag) are
not new. Report their current status, and say when the record is wrong.

### Method

1. Read the scope in this order: Manifest's `development-stages.md` and `delivery-order.md`,
   Jettison's `roadmap.md` (current priority) and `game-integration.md`, then the boundary
   documents, then the stage and system plans.
2. Build the capability map (check 2) before writing any findings. Most cross-repository
   findings come out of it.
3. Verify each finding against the text and, where it makes a claim about the code,
   against the code. Drop a finding you cannot support with evidence, or report it as a
   question.
4. Write findings as you go, so a session that runs short still leaves a usable file.

### Output

Write `docs/planning-audit-2026-11-16.md` in Jettison (Manifest is not writable from a
Jettison session; give its paths as `../manifest/...`). Use British spelling, as both
repositories do. Do not commit; the user reviews it first.

Sections:

1. **Verdict:** a few sentences. Is the plan sound enough to start Stage 1 and the Jettison
   extraction? What most needs settling first?
2. **Capability map:** the table from check 2.
3. **Findings:** ranked by severity. Each finding gives:
   - its ID (`A01`, ...) and severity;
   - the repository and the `file:line` references;
   - the claim and the evidence against it;
   - the suggested fix;
   - its owner: Jettison, Manifest, or a decision for the user.
4. **Questions for the user:** decisions only the user can settle.
5. **Method and limits:** what was read in full, what was sampled, and what was not checked.

Severity:

- **Blocker:** wrong or missing in a way that would make Stage 1 or the engine extraction
  build the wrong thing.
- **Major:** a contradiction or gap that will cost rework if left until it is reached.
- **Minor:** stale facts, broken references, duplication.

### Starting prompt

> Read `docs/planning-audit-handoff.md` and carry out the audit it describes. It is
> read-only apart from the findings file.
