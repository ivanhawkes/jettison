# Planning audit handoff

Written 2026-11-16 for a fresh session. The user approved the plan below; the session that
wrote it did no audit work beyond finding the documents and sizing them.

## The task

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

## State at handoff

| Repository | Branch | HEAD | Tree |
| --- | --- | --- | --- |
| Jettison | `feature-base` | `0564f19` | clean apart from this file |
| Manifest | `feature-base` | `49a761b` | clean |

Manifest pins Jettison at `f850920`, two commits behind the engine's HEAD. If either HEAD
has moved, record the new one in the findings and audit what is there.

## Scope

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

## What to check

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

## Method

1. Read the scope in this order: Manifest's `development-stages.md` and `delivery-order.md`,
   Jettison's `roadmap.md` (current priority) and `game-integration.md`, then the boundary
   documents, then the stage and system plans.
2. Build the capability map (check 2) before writing any findings. Most cross-repository
   findings come out of it.
3. Verify each finding against the text and, where it makes a claim about the code,
   against the code. Drop a finding you cannot support with evidence, or report it as a
   question.
4. Write findings as you go, so a session that runs short still leaves a usable file.

## Output

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

## Starting prompt

> Read `docs/planning-audit-handoff.md` and carry out the audit it describes. It is
> read-only apart from the findings file.
