# Coding standards

The house rules, taken from the code as it is and from what the work on it has taught. Where
today's code disagrees with a rule here, the rule is the target and the code is fixed when it is
next touched.

## Language and toolchain

- C++20, Clang 21, Linux on x86-64.
- Formatting is `std::format`. spdlog is configured to use it rather than its bundled fmt.
- The warnings set on `jettison_warnings` is linked privately by every project target and must
  stay clean. `-Werror` is on in the gate. A new warning is fixed, not suppressed. A warning
  that cannot be fixed is removed from the set with the measurement that justified it, as
  `-Wold-style-cast` was.
- clang-tidy uses the curated `.clang-tidy`. A check is added when it is clean over the sources
  or finds something that gets fixed. Exclusions carry their counts.
- clang-format uses the project `.clang-format`: Microsoft base, Allman braces, tabs for
  indentation, 104 columns. The pre-commit hook checks it.
- Every function and method declaration has blank lines above it, then its comment and `template`
  line if it has them, then the declaration, so each reads as its own block: one blank line in a
  header, two in a source file, where the bodies are. The first one after an opening brace or an access label
  (`public:`) has none, since clang-format removes them there. clang-format cannot add these
  lines (`SeparateDefinitionBlocks` skips declarations and collapses two blank lines to one), so
  `scripts/space-declarations.py` adds them and the gate runs it with `--check`.

## Naming

| Thing | Style | Example |
|---|---|---|
| Types, enums, enum values | PascalCase | `SceneUpdateSystem`, `State::Running` |
| Functions and methods | PascalCase | `LoadModel`, `Destroy`, `WaitOnWindowResized` |
| Locals and parameters | camelCase | `deltaTime`, `sceneRegistry` |
| Members | `m_` + camelCase | `m_generation` |
| Pointer members (raw and smart) | `m_p` + PascalCase | `m_pDeviceContext` |
| Component fields | camelCase, no prefix | `rotationSpeed`, `active` |
| Macros | `JETTISON_` + upper snake case | `JETTISON_ASSERT` |
| Namespaces | `Jettison::<Module>` | `Jettison::Render` |
| Files | PascalCase, one main type per file | `SmokeSequence.h` / `.cpp` |
| Asset files | kebab-case | `habitable-earthlike-a-normal.ktx2` |

Asset files under `assets/` are kebab-case: lower-case words and digits joined by hyphens, so a
path is spelt the same on Linux and Windows and needs no quoting. A texture is
`textures/<kind>/<asset>/<asset>-<map>.ktx2`, the asset's name repeated so a file is still
identified once it is out of its folder; the map is one of `color`, `normal`, `height`,
`roughness`, `metallic`, `emissive`, and `detail-` before one of them for a detail layer. A
material is `materials/<asset>.material.yaml`. Map names are the material slots' words, spelt as
the code spells them. Source packs keep their own names outside the repository, and an import
script maps them (`scripts/import-planet-textures.sh`). Shader programs (`mesh_triplanar`) predate the rule.

## Headers and includes

- `#pragma once`.
- Public headers: `#include <jettison/<module>/Name.h>`. Private headers in the same module use
  quotes.
- Order: the file's own header, third-party, standard library, then engine headers, as
  separate blocks.
- Forward-declare in headers where a pointer or reference is enough; `Application.h` is the
  model.
- Never configure a library with a macro in a header. Configuration goes on the library's CMake
  target. GLM's `GLM_FORCE_DEPTH_ZERO_TO_ONE` once sat in `Model.h` above the GLM includes, so
  it reached only the translation units that included that header first, and reordering an
  include changed the projection's depth range.

## Comments

**The test: a comment stays only if deleting it would lose something a competent reader cannot
get back from the code, its names and its types.** The code says what and how; a comment says
what the code cannot. Before writing one, try to say it in the code instead: a clearer name, a
type (`std::span`, a strong typedef, a concept), `[[nodiscard]]`, an assertion.

What a comment is for:

- **Why it has this shape**, when the obvious alternative was rejected: a flat vector over a map
  on a hot path, a float where a double looks right.
- **The evidence**: what was measured, with the number, when a decision rests on it. A measurement
  is the reason, not narration.
- **The hazard**: what breaks if someone tidies it. An ordering, a lifetime, a lock, a layout that
  is written to disk or the GPU, a refactor that would break determinism or the ABI.
- **The contract a public header promises** that the signature cannot: preconditions, what it
  throws or returns on failure, who owns what afterwards, units, thread safety.
- **In data files** (YAML, CMake): a pinned version or magic value and why, a key or value another
  tool parses, a constraint from the environment, and what a field means when nothing else
  documents it (`# rgb: tint, a: unused` for a packed shader parameter is the schema, so it stays).

What a comment is not for, deleted on sight:

- Restating the name, type or key below it (`// The shape, by name` over `std::string shape;`).
- Narrating the steps of a function, or explaining standard C++ (`std::move`, RAII, `<algorithm>`).
- The header's comment copied onto the definition in the `.cpp`.
- Section banners (`// ---- Reading ----`, `# ======`): split the file, or let the order and blank
  lines speak.
- Commented-out code or `#if 0`: history is in git.

A `TODO` names the question it asks, and is resolved where it was asked. The reference style is
`ModelCache` (`render/src/vulkan/ModelCache.h`), `RotationAnimationComponent::angle` and the GLM
block in `source/external/CMakeLists.txt`: each names the hazard, the evidence and the reason for
the shape. When code changes, a comment whose reason changed with it is updated or deleted in the
same edit. A reviewer strips any new comment that fails the test.

## Ownership and lifetime

- `std::unique_ptr` by default, `std::shared_ptr` only when ownership is genuinely shared,
  raw pointers and references for non-owning access.
- GPU objects are released with an explicit `Destroy()`, called in dependency order by their
  owner. `Application::Shutdown` is the backstop. It is idempotent and `noexcept`, and
  `~Application` calls it. Non-GPU types use RAII.
- Never destroy a GPU resource a submitted command buffer can still reference. Wait on the
  frame's fence, and write down which fence it is where the destroy happens.
- Caches are keyed on handles with generations, not on addresses. An address is reused by the
  next allocation.
- Delete copy operations on any type that owns a resource.

## Components and state

- Components are plain structs with default member initialisers and no behaviour. Systems hold
  the behaviour.
- **Authored state and runtime state are separate fields.** The frame loop never writes to a
  field the scene file stores. Runtime phase lives in its own field, documented as "not written
  to the scene file". A saved scene must equal the loaded one.
- **One rule, one owner.** A constraint that a loader enforces lives in one function that the
  writer also uses. Every format has a round-trip test.

## Errors

- Exceptions for failures at boundaries: start-up, loading, file and format errors. They carry
  enough context to act on: the stage, the path, the version.
- No exceptions inside per-frame or per-step hot paths. Programming errors there are
  assertions.
- Outcomes that are expected and recoverable, such as a lookup that can miss or a command the
  simulation rejects, return `std::optional` or a small result type naming the reason. The
  project is C++20, so there is no `std::expected`, and no stand-in library for it until a hot
  path needs an error payload that a result enum cannot carry.
- `JETTISON_ASSERT` checks invariants in debug builds; `JETTISON_VERIFY` checks always.
  Neither has side effects in its condition.
- A stub that cannot do its job fails loudly with `JETTISON_NOT_IMPLEMENTED()`. It never
  silently succeeds.
- Reports go through `Log.h`: `Log(Severity::Warning, kCategory, "dock {} refused", id)`, with
  one `constexpr Category` per module or subsystem. Not `std::cerr`, and never spdlog directly.
  The exceptions are the few places with nowhere else to go: the shell's top-level failure report
  and usage line (`RenderApplication.cpp`, `AppArguments.cpp`), which the exit status pairs with,
  and the application's teardown, which runs in a destructor with an exception in flight.

## Simulation code

Code in `sim`, `celestial`, `spatial`, `physics`, `space`, `ai` and `dock` must be deterministic across
runs and across Debug and Release builds. `math` follows the same rules, because those modules call
it and its `Random` and `Noise` output is part of the save format, and so does `script`, because a
script answers a decision's inputs inside the step: its input calls are on a fresh instance each,
counted in fuel rather than timed, and WAMR's interpreter is compiled with `-ffp-contract=off` too
([architecture](architecture.md#scripts)). Its watchdog's wall clock stops a script and never decides
what a call returns:

- Fixed timestep only. Never read the wall clock; use `SimClock`.
- Random numbers come from the engine's `Random`, seeded from simulation state. Never use
  `std::uniform_*_distribution` or `std::rand`: their output differs between standard libraries.
- Never let iteration order of an unordered container decide an outcome. Sort, or use an ordered
  container.
- Numeric representations follow each API's explicit deterministic contract. The sample
  economy uses integer credits/crates; that is not a universal game requirement. Manifest
  uses Decimal per-unit prices, floating continuous quantities and whole-credit balances.
  Keep conversions checked and explicit rather than imposing the sample model on consumers.
- Positions are `double`, in metres, in the system frame.
- Simulation modules compile with `-ffp-contract=off`, as Jolt does for itself under
  `CROSS_PLATFORM_DETERMINISTIC`. Otherwise the compiler may fuse a multiply and an add into one
  FMA instruction where the instruction set allows it, and the rounding then depends on flags
  and optimisation level rather than on the source.
- No includes from `render`, `ui`, `audio` or `input`.

## Responsibility boundaries

Follow [game integration](game-integration.md): inspect Manifest's required mechanics,
provide reusable missing public APIs in the engine, and keep production game rules and
data in Manifest. Test gameplay belongs in separate sample/test targets. Generic modules
must not depend on sample code or optional space services. Prove a new shared capability
with an independent consumer as well as its intended Manifest integration.

## Dependencies

Every third-party library goes through `source/external/CMakeLists.txt`. It is pinned to a tag
or commit, fetched with `FetchContent`, declared `SYSTEM` so its warnings are not ours, and
given a comment saying why it was chosen over the alternative. A new dependency is judged on
the project's criteria: elegance, efficiency, modern C++ usage, interoperability and
maintenance. It is added to `dependency_probe` with at least one real call into it, and it
follows the integration rules in [build and gates](build-and-gates.md#integration-rules-the-set-follows):
static, one copy of each library, options scoped in a `block()`, and the project's language
features.

## Testing

- Every module has a doctest suite; a phase is not done until its suite covers its exit
  criterion.
- Serialisation has round-trip tests. Simulation has replay tests: the same seed and command
  stream must produce the same state hash.
- GPU paths are covered by the headless smoke test, judged on validation-layer output as well
  as exit status (see [build and gates](build-and-gates.md)).

## Commits

The subject is `Area: what changed, and what it was for`, in the style of the commit history
(`Scene round trip: the loader refused the file the writer produced`). The body records what
was measured and why the change takes the shape it does, in plain prose. Each commit leaves the
gate green. Commits, code, comments and documents use British spelling (serialise, colour).
