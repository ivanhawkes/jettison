# Coding standards

The house rules, taken from the code and from what the work on it has taught. Where today's code
disagrees with a rule, the rule is the target, and the code is fixed when next touched. How the
rules are checked is in [build and gates](build-and-gates.md).

## Language and toolchain

- C++20, Clang 21, Linux on x86-64. Why the standard stays at C++20 is in
  [decisions](decisions.md#build-and-platform).
- Text is UTF-8 in `std::string`, with no wide-string types; a hashed `Core::StringId` names
  assets, definitions and tags.
- Formatting is `std::format`. spdlog is configured to use it rather than its bundled fmt.
- The project's warnings stay clean, so the gate's `--werror` can make them errors. A new warning
  is fixed, not suppressed. A warning that cannot be fixed leaves the set with the measurement that
  justified it ([build and gates](build-and-gates.md#warnings)).
- clang-tidy runs the curated `.clang-tidy`. A check is added when it is clean over the sources or
  finds something that gets fixed, and an exclusion says why, with its measurement where there is
  one ([build and gates](build-and-gates.md#clang-tidy)).
- clang-format runs the project's `.clang-format`: Microsoft base, Allman braces, tabs for
  indentation counted as 4 columns, 104 columns a line. `scripts/format-check.sh` checks it, run by
  the worktrunk pre-commit hook and by the gate ([build and gates](build-and-gates.md#formatting)).

### Declaration spacing

Every function and method declaration stands as its own block, so a reader sees where one ends:
blank lines above it (one in a header, two in a source file), then its comment and `template` line
if it has them, then the declaration. The first declaration after an opening brace or an access
label (`public:`) has no blank line, since clang-format removes it there.

Before:

```cpp
class ModelCache
{
  public:
	ModelCache(const ModelCache &) = delete;
	ModelCache &operator=(const ModelCache &) = delete;
	// Registers the model loader, so a renderer is made once per resource manager.
	void Init();
```

After (`source/engine/render/src/vulkan/ModelCache.h`):

```cpp
class ModelCache
{
  public:
	ModelCache(const ModelCache &) = delete;

	ModelCache &operator=(const ModelCache &) = delete;

	// Registers the model loader, so a renderer is made once per resource manager.
	void Init();
```

Inside a body, a class or a namespace, a comment that follows the end of a statement or a block
starts a new block of code, so it has one blank line above it: the code before it stands as its own
block. A comment right after an opening brace, a label, another comment, or within a statement that
goes on (an argument list, an initialiser) has none, and nor does the last block before a closing
brace.

Before:

```cpp
	m_pSwapchain->Recreate();
	// The pipelines were built for the swapchain's format, and the surface is the same one, so
	// the format is too; a surface that changed its mind would need them rebuilt.
	if (m_pSwapchain->GetImageFormat() != format)
```

After (`source/engine/render/src/vulkan/Renderer.cpp`):

```cpp
	m_pSwapchain->Recreate();

	// The pipelines were built for the swapchain's format, and the surface is the same one, so
	// the format is too; a surface that changed its mind would need them rebuilt.
	if (m_pSwapchain->GetImageFormat() != format)
```

clang-format cannot add these lines, so `scripts/space-declarations.py` does, run by hand rather
than by the gate ([build and gates](build-and-gates.md#declaration-spacing)). Manifest follows the
same rules, run with `--root`.

No formatting change, by hand or by a script, reaches inside a string or other literal: a raw string
holding generated code, a shader or a test's expected text is data, and spacing it changes what the
program says. The spacing script skips raw string literals for that reason.

## Naming

| Thing | Style | Example |
| --- | --- | --- |
| Types, enums, enum values | PascalCase | `SceneUpdateSystem`, `State::Running` |
| Functions and methods | PascalCase | `LoadModel`, `Destroy`, `WaitOnWindowResized` |
| Locals and parameters | camelCase | `deltaTime`, `sceneRegistry` |
| Members | `m_` + camelCase | `m_generation` |
| Pointer members (raw and smart) | `m_p` + PascalCase | `m_pDeviceContext` |
| Component fields | camelCase, no prefix | `rotationSpeed`, `active` |
| Constants | `k` + PascalCase | `kPickDistance`, `kSimStep`, `kAudio` |
| Macros | `JETTISON_` + upper snake case | `JETTISON_ASSERT` |
| Namespaces | `Jettison::<Module>` | `Jettison::Render` |
| Files | PascalCase, one main type per file | `SmokeSequence.h` / `.cpp` |
| Content files | kebab-case | `habitable-earthlike-a-normal.ktx2` |

A constant is a `constexpr` (or `inline constexpr`) variable at namespace scope, or a
`static constexpr` member. A log category is one too: `constexpr Core::Category kAudio{"audio"};`.

### Content files

Content files (textures, materials, models and sounds, under `source/test/content/` or a game's
content root) are kebab-case: lower-case words and digits joined by hyphens, so a path is spelt the
same on any file system and needs no quoting.

- A texture is `textures/<kind>/<asset>/<asset>-<map>.ktx2`. The asset's name is repeated, so a
  file is still identified once it is out of its folder.
- The map is one of `color`, `normal`, `orm`, `height`, `roughness`, `metallic` and `emissive`, with
  `detail-` before one of them for a detail layer. These are the material slots' words, spelt as the
  code spells them.
- A material is `materials/<asset>.material.yaml`.

Source packs keep their own names outside the repository, and an import script maps them
(`scripts/import-planet-textures.sh`). Shader programs in `assets/shaders/` predate the rule and are
snake_case (`mesh_triplanar`); fonts keep their upstream names (`Roboto-Medium.ttf`).

## Constants

**Every magic number in production code is a named constant, even when it is used once.** A magic
number is a chosen value: a size, a count, a limit, a tolerance, a duration, a factor. Its name says
what it is, and its comment says why it is that value, when that is not plain. One literal then
cannot quietly serve two meanings, and every use of a value can be found by its name.

Before:

```cpp
if (framesWaited > 60)
	Advance();
```

After (`source/engine/app/src/SmokeSequence.cpp`):

```cpp
// The frames a pick is given to be answered: the next simulation step answers it, which is a frame
// or two at most, unless the world is not stepping, and then the sequence moves on without it.
constexpr int kPickFrames = 60;
```

Not magic: the arithmetic identities of a formula, such as 0, 1 and the 2 of a midpoint, and a
conversion whose unit is named (10'000 basis points). Tests are exempt: their expected values and
fixture numbers stay literal, where names would only bulk them up.

## Headers and includes

- `#pragma once`.
- Public headers: `#include <jettison/<module>/Name.h>`. Private headers in the same module use
  quotes.
- Order: the file's own header, third-party, standard library, then engine headers, as separate
  blocks (`source/engine/dock/src/Docks.cpp` is the model).
- Forward-declare in headers where a pointer or reference is enough; `Application.h` is the model.
- Never configure a library with a macro in a header; configure it on the library's CMake target.
  A macro in a header reaches only the files that include that header first, so reordering an
  include changes the library's behaviour. GLM's `GLM_FORCE_DEPTH_ZERO_TO_ONE` once
  sat in `Model.h` and changed the projection's depth range in this way.

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

Before, a comment that restates the name:

```cpp
// The pick distance.
constexpr double kPickDistance = 50000.0;
```

After, a comment that says why the value is this one (`source/engine/app/src/Application.cpp`):

```cpp
// How far a pick reaches: past any platform a vehicle is flown by.
constexpr double kPickDistance = 50000.0;
```

A `TODO` names the question it asks, and is resolved where it was asked. When code changes, a
comment whose reason changed with it is updated or deleted in the same edit. A reviewer strips any
new comment that fails the test. Two more good comments: `ModelCache`
(`source/engine/render/src/vulkan/ModelCache.h`) names a hazard, keying models on paths because a
reload can make a new model where an old one was; the GLM block in `source/external/CMakeLists.txt`
gives evidence, the projection's values with and without the depth-range macro.

## Ownership and lifetime

- `std::unique_ptr` by default, `std::shared_ptr` only when ownership is genuinely shared, and
  raw pointers and references for non-owning access.
- GPU objects are released with an explicit `Destroy()`, called in dependency order by their
  owner. `Application::Shutdown` is the backstop: it is idempotent and `noexcept`, and
  `~Application` calls it. Non-GPU types use RAII.
- Never destroy a GPU resource a submitted command buffer can still reference. Wait on the frame's
  fence, and write down which fence it is where the destroy happens.
- Caches are keyed on something stable, such as a virtual path or a handle with a generation
  (`Core::Handle`), never on an address. An address is reused by the next allocation, so a key on
  it can name a different object.
- Delete copy operations on any type that owns a resource.

## Components and state

- Components are plain structs with default member initialisers and no behaviour. Systems hold the
  behaviour.
- **Authored state and runtime state are separate fields.** The frame loop never writes to a field
  the scene file stores; runtime state lives in its own field, documented as not written to the
  file. A saved scene must equal the loaded one.
- **One rule, one owner.** A constraint that a loader enforces lives in one function that the
  writer also uses. Every format has a round-trip test.

Before, the frame loop turns the authored transform, so saving the scene records wherever the
animation had got to:

```cpp
transform.rotation += animation.rotationAxis * animation.rotationSpeed * dt;
```

After, the phase is runtime state of its own, and the authored rotation is never written
(`source/engine/animation/include/jettison/animation/Components.h`):

```cpp
struct RotationAnimationComponent
{
	glm::vec3 rotationAxis{0.0f, 1.0f, 0.0f};
	float rotationSpeed{0.785398163f};

	// The phase of the rotation, owned by the frame loop and not written to the scene file. ...
	float angle{0.0f};
};
```

## Errors

- Exceptions for failures at boundaries (start-up, loading, file and format errors), carrying
  enough context to act on: the stage, the path, the version.
- No exceptions inside per-frame or per-step hot paths. Programming errors there are assertions.
- Outcomes that are expected and recoverable, such as a lookup that can miss or a command the
  simulation refuses, return `std::optional` or a small result type naming the
  [reason](glossary.md#reason). C++20 has no `std::expected`, and no stand-in library is added
  until a hot path needs an error payload that a result enum cannot carry.
- `JETTISON_ASSERT` checks invariants in debug builds; `JETTISON_VERIFY` checks in every build.
  Neither has side effects in its condition: in a release build an assertion's condition is not
  evaluated.
- A stub that cannot do its job fails loudly with `JETTISON_NOT_IMPLEMENTED()`, which throws. It
  never silently succeeds.
- Reports go through `Log.h`, with one `constexpr Core::Category` per module or subsystem:
  `Log(Severity::Info, kRender, "swapchain {}x{}", width, height)`. Not `std::cerr` or spdlog
  directly, except where there is nowhere else to go: the shell's top-level failure report and usage
  line, which the exit status pairs with (`RenderApplication.cpp`, `AppArguments.cpp`), and the
  teardown, which runs in a destructor with an exception in flight (`Application.cpp`).

## Simulation code

The [simulation](glossary.md#simulation) must give the same result on every run and in every build
type: saves, replays and networked play depend on the same seed and commands producing the same
state hash.

These modules are deterministic: `sim`, `spatial`, `physics`, `ai`, `transit`, `dock`, `time`,
`math` and `script`. `time` holds `SimClock`, which the simulation reads its time from. `math` is
in because the simulation calls it, and its `Random` and `Noise` output is part of the save format.
`script` is in because a script answers a decision inside the step
([scripts](scripts.md)).

Each such module says so in its CMake (`source/engine/dock/CMakeLists.txt`):

```cmake
jettison_add_module(dock
	DETERMINISTIC
	DEPENDS
		...
```

`DETERMINISTIC` compiles the module and its tests with `-ffp-contract=off`
(`cmake/JettisonModule.cmake`), as Jolt does for itself under `CROSS_PLATFORM_DETERMINISTIC`.
Otherwise the compiler may fuse a multiply and an add into one FMA instruction, and the rounding
then depends on the optimisation level rather than on the source. WAMR's interpreter is compiled
the same way (`source/external/wamr/CMakeLists.txt`).

The rules for code in those modules:

- Fixed step only. Never read the wall clock; read `Time::SimClock`. A script's watchdog uses the
  wall clock only to stop a script, never to decide what a call returns.
- Random numbers come from the engine's `Math::Random`, seeded from simulation state. Never use
  `std::uniform_*_distribution` or `std::rand`: their output differs between standard libraries.
- Never let the iteration order of an unordered container, or of an EnTT storage, decide an
  outcome. Sort, or use an ordered container. A save keeps a storage's packed order, but that order
  is a history nobody chose: a removal moves the last element into the gap, and a view of several
  components walks whichever storage is smallest, so an unrelated change reorders it.
- Each API states the numeric representation its determinism rests on. Convert between
  representations explicitly, with checks. The engine imposes no numeric model on a game.
- Positions are `double`, in metres, in their [frame](glossary.md#frame-coordinates)
  ([coordinate frames and units](coordinates.md)).
- No includes from `render`, `ui`, `audio` or `input`.

Before, the storage's order, a history of additions and removals, decides which berth is served
first:

```cpp
for (const entt::entity berth : registry.view<const Berth>())
	Serve(berth);
```

After, the entities are in identifier order, which no storage's history changes
(`source/engine/dock/src/Sorted.h`):

```cpp
std::vector<entt::entity> entities;
for (const entt::entity entity : registry.view<const Components...>())
	entities.push_back(entity);
std::ranges::sort(entities, {}, [](entt::entity entity) { return entt::to_integral(entity); });
```

## Responsibility boundaries

Engine code provides reusable capabilities; a game's rules, policy and content stay in the game.
Generic modules never depend on sample code or on a game's content. A new shared capability is
proved by an independent consumer of the engine's own as well as by its Manifest integration.
[Game integration](game-integration.md) says how a gap is found and provisioned, and
[scope](scope.md) gives the verdict for each area.

## Dependencies

Every third-party library goes through `source/external/CMakeLists.txt`. It is pinned to a tag or
commit, fetched with `FetchContent`, declared `SYSTEM` so its warnings are not ours, and given a
comment saying why it was chosen over the alternative. A new dependency is judged on the project's
criteria: elegance, efficiency, modern C++ usage, interoperability and maintenance. It is added to
`dependency_probe` with at least one real call into it, and it follows the
[integration rules](build-and-gates.md#integration-rules-the-set-follows).

Its licence must let the engine and a game on it be distributed as open source: MIT, BSD, zlib,
Apache-2.0 and the like. A licence that forbids that, or binaries with no source, rules a library
out however good it is (NVIDIA's RTX SDKs, FidelityFX 2; [decisions](decisions.md#build-and-platform)).
The licence is read in the repository's own file, not taken from a summary. Content committed to
the repository (test content, samples) is the project's own or generated at build time; a model or
sound under another's licence, even one as open as CC-BY, is not committed, since its attribution
would travel with every copy.

A fact a design rests on about an API, a driver, a device or a library (an extension's name and
operations, a limit, what a version requires, a licence) is checked against its primary source
before code relies on it: the headers, the registry, the library's source, or the device itself
(`vulkaninfo`). The document that uses it gives the date and the source. A claim from a search
summary or another assistant is a lead, not a fact; the plan for [dense geometry and ray
tracing](dense-geometry-and-ray-tracing.md#facts-checked) found several in one transcript that did not
hold.

## GPU code

- **Optional device features go through one capability table.** A path that needs a feature not
  every device has (mesh shaders, ray queries) asks the table, a setting or flag can turn it off,
  and the log says why it is off, as device choice logs a device's reasons. Its fallback is kept
  drawing: a reference view runs with the path forced off.
- **A struct the GPU reads has its layout checked.** Its size and each field's offset are
  `static_assert`ed against the GLSL declaration it must match, so a field added on one side fails
  the build rather than a frame.
- **What decides an image comes in a fixed order.** GPU work whose order depends on which thread
  finishes first (atomic appends) does not decide what is drawn where depths tie: compact in index
  order, or make the result order-independent. The reference frames depend on it.
- **One source, variants for features.** A program that differs by a device feature is a variant of
  the same source (`jettison_compile_shader_variant`), not a copy, so the paths cannot drift apart.

## Testing

- Every module has a doctest suite; work is not done until its suite covers its exit criterion.
- Serialisation has round-trip tests. Simulation has replay tests of the state hash
  ([simulation code](#simulation-code)).
- GPU paths are covered by the headless [smoke test](glossary.md#smoke-test), judged on
  validation-layer output as well as exit status ([build and gates](build-and-gates.md)). An
  optional path is covered both on and forced off ([GPU code](#gpu-code)).

## Commits

The subject is `Area: what changed, and what it was for`, in the style of the commit history
(`Scene round trip: the loader refused the file the writer produced`). The body records what was
measured and why the change takes the shape it does, in plain prose.

Each commit builds and passes the suites of the modules it touches, or a local run that shows it
working. Work is done in batches of a few pieces, each in its own lane
([batches](build-and-gates.md#batches)): clang-tidy, the format and whitespace checks and the full
[gate](build-and-gates.md#the-gate) run once, on `feature-base` after the batch's pieces are merged,
and their findings are fixed there together, before anything is pushed. A gate failure is fixed in
a further commit, not by rewriting history.

Commits, code, comments and documents use British spelling (serialise, colour).
