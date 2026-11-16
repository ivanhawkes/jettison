# Dev tools theme handoff

Written 2026-11-17 for a fresh session. The work is committed and the gate is green at it; what is
left is the author's side of a closed review thread — cross-review round 2 asked for changes, the
two findings are answered in the committed tree, and rounds are capped at two, so the user settles
what is open.

## The task

Decide what is owed on the last two findings, and do it if something is.

| Finding | Grade | Status in `79c87f8` |
| --- | --- | --- |
| F5 — the colour contract promised exactness the 8-bit vertex packing does not give | major | corrected to the measured distribution; the suite counts it and asserts the landings by name |
| F6 — the explanation claimed a recreated context keeps the themed style bytes | minor | claim removed; the cause is `ImGuiStyle`'s constructor writing stock values over the reused block, and the suite checks the stock start |

Two ways to close it:

- **Accept as it stands.** The thread is done. Nothing in the tree is left in a state the documents
  do not describe, and every claim in `docs/architecture.md`'s theme section is asserted by a test.
- **Ask for more.** The only substantive work F5 points at is exact darks, which is a carried-forward
  item, not a claim: the sRGB encoding in the UI's own shader over a UNORM attachment. F6 has no
  further work identified beyond the corrected prose, which is committed.

A third round needs the cap raised, because the default is two and round 3 is refused:

```bash
./scripts/cross-review.sh --max-rounds 3 --reply FILE --gate-log build/gate10.log
./scripts/cross-review.sh --new --brief FILE    # restart the thread from round 1
```

The rounds are in `build/reviews/feature-base/`: `brief.md`, `round-1.md` and `reply-1.md`,
`round-2.md` and `reply-2.md` (the answer to round 2, written by the author and not consumed by the
script, because the round was not run), the transcripts `round-1.log` and `round-2.log`, and
`scratch/` — the reviewer's probes, of which `round2-contracts.cpp`, `.log` and
`build-round2-contracts.py` are the F5 measurement and `probe.cpp` / `probe.log` the F6 one.
`build/` is not precious: a `rm -rf build` takes the thread with it, so copy the directory
somewhere if the thread matters after the next clean build directory.

## State at handoff

| Item | Value |
| --- | --- |
| Repository | `/home/ivan/projects/jettison`, branch `feature-base` |
| HEAD | `79c87f8` — *Dev tools: the ImGui style themed on Catppuccin, fed to the sRGB swapchain in linear light* |
| Tree | clean |
| Gate at HEAD | `gate passed`, 256 s, ten stages, `--werror --asan`; log `build/gate10.log` |
| Suites | devtools 16 cases / 1 578 assertions; render 98 cases; CTest 27 of 27 |
| Docs touched | `architecture.md` (the theme's contract), `build-and-gates.md` (the two flakes), `roadmap.md` (the carried-forward entry) |

`docs/planning-audit-2026-11-16.md` and the roadmap's carried-forward list are unchanged by this
handoff, which is untracked: keep it, commit it, or delete it once the thread is closed.

## F5 in detail — the colour contract

**What was claimed and is false:** that colours above mid tones land on the palette byte for byte.
The claim was read off the worst-case table, not off the distribution, so it hid the one-step cases.

**What is measured** (the theme's decode, then `ImGui::ColorConvertFloat4ToU32`, then the
attachment's encode, over the 264 channels of the 88 colours the four varieties give the 22 roles):

- 209 channels land on the palette byte; 33 are one step away; 15 two; 2 three; 5 four. Bound 4.
- Four steps, all darkest: Mocha crust `#11111b` → `#0d0d1c` (r, g), Latte `#04a5e5` → `#00a5e5` (r).
- Two steps, panel grounds at the dark end: Mocha base `#1e1e2e` → `#1c1c2e`, Macchiato `#24273a` →
  `#22263b`.
- One step, mid tones of the lighter varieties: Macchiato `#494d64` → `#494d63`, Frappé `#51576d` →
  `#51566d`, Latte text `#4c4f69` → `#4b4f69`.
- Mocha's mid and light tones land exact, and they are what frames count by the pixel: text
  `#cdd6f4` over 2 318 pixels, secondary button `#45475a` over 16 279; a live `dev.palette
  macchiato` draws `#cad3f5` over 2 318 and `#494d63` over 16 279 on the next frame — the packing's
  predicted step, not a rounding of the encode.

**Where it lives:** the contract is the theme section of `docs/architecture.md`; the assertions are
in `source/engine/ui/imgui/tests/ThemeTests.cpp` in the case *"Theme: the palette goes to an sRGB
attachment in linear light, so a hex reads as written"* — the bound `worst <= 4`, the histogram
`channels == 264` and `steps[0..4] == 209, 33, 15, 2, 5`, and the landings by name through a
`landed(palette, token, channel)` lambda. Read it with:

```bash
./build/clang-debug/source/engine/ui/imgui/jettison_devtools_tests \
  -tc="Theme: the palette goes to an sRGB attachment*" -s 2>&1 | grep -E "worst|Message|assert"
```

The histogram is counted from the palette tables, so it is a test over the data: a palette edited
moves those five numbers. That is deliberate — the documents quote them — and the failure names the
count that moved.

**If F5 is taken further** (exact darks), that is the carried-forward entry in
`docs/roadmap.md`, not a claim to fix: the UI's draw shader would encode, over a UNORM attachment,
the way `assets/shaders/overlay.frag` does for the game UI. The path to it runs through the ImGui
backend's pipeline (`source/engine/render/src/vulkan/imgui/ImGuiRenderer.*`, its format at
`Swapchain.cpp`), and the colour path's switch is `IRenderer::UiColoursLinearised`, answered by
`VulkanRenderer` from `Swapchain::EncodesSrgb()`. Nothing in the theme would change shape; the
linearise flag would go false on the path that encodes in the shader, and the palette tables would
then land as authored. It is a shader, pipeline and format change with the reference frames in it.

## F6 in detail — the context-reuse explanation

**What was false:** that a context recreated at the themed context's address reuses its *style
bytes*, so a new context can start themed. The observation came from the test fixture that destroyed
whichever context was current instead of the one it made, so the assertions read the themed context.

**What is now said:** the recreated context reuses the themed one's *address* and starts **stock**,
because `ImGuiStyle`'s constructor writes the stock padding, rounding and colours over the block.
That is what makes an address key answer "already themed" to a stock style, which is the standard's
rule (`docs/coding-standards.md`: caches key on handles with generations, not addresses).

**Where it lives:** the rationale is the comment above `StyleIsApplied` in
`source/engine/ui/imgui/src/Theme.cpp`; the check is in `ThemeTests.cpp`'s case *"Theme: a console
change is applied once, and a new context is never skipped"*, which after the themed context is
destroyed asserts the stock start at the reused address — `WindowPadding.x == 8.0f`,
`WindowRounding == 0.0f` — and prints the address reuse as a `MESSAGE`. The fixture that owns its
context by pointer, makes it current and restores the outer one is in all three suites
(`ThemeTests.cpp`, `ViewTests.cpp`, `FontFacesTests.cpp`), with the reason in the comments.

The reviewer's probe is `build/reviews/feature-base/scratch/probe.cpp` and `probe.log`.

## Facts not to re-derive

- **ImGui is 1.93.0**, pinned in `source/external/CMakeLists.txt`; sources at
  `build/clang-debug/_deps/imgui-src` are the authority, and the two facts that cost a round are in
  it: `GetDefaultFont()` is `io.FontDefault ? io.FontDefault : Fonts[0]`, and
  `AddFontFromMemoryTTF` leaves `ImFontConfig::Name` empty (only `AddFontFromFileTTF` derives one).
- **Faces** load in `source/engine/render/src/vulkan/imgui/FontFaces.{h,cpp}`: Roboto Medium is the
  UI face, named in `io.FontDefault`; Hack is the console's, found by `MonoFont()` reading
  `ImFont::GetDebugName()` for `hack`, `mono`, `cousine`; each face's file name is written into
  `ImFontConfig::Name` by the loader; the bytes outlive the context because
  `FontDataOwnedByAtlas` is false.
- **The scale band** is `ScaleFor` in `Theme.cpp`: not finite or not positive styles at 1, positive
  clamps to [0.25, 4.0]. Measured, 0.01 gives `WindowMinSize` {0.8, 0.6} and `NewFrame` asserts.
- **The applied probe** reads the style (`WindowPadding.x`, `WindowRounding`, `ImGuiCol_WindowBg`)
  against the same expressions `ApplyTheme` writes; nothing in the engine writes the style.
- **The font scopes** in `Views.cpp` are RAII and close before `ImGui::End()`, because ImGui polices
  the font stack per window; the gate caught a `Missing PopFont()` assertion when they did not.
- **`m_theme` is declared before `m_console`** in `Application.h`, so the console's references to the
  settings are destroyed first.
- **A `const` variable only read by an `IM_ASSERT` breaks the release build** under `-Werror`
  (`-Wunused-variable`): read it with `static_cast<void>`, as `FontFaces.cpp` does.
- **Reference frames are LFS.** For main's committed frames, in order to measure a delta:
  `git worktree add --detach /tmp/jettison-head HEAD`, compare, `git worktree remove --force`.
  `git show HEAD:path` hands back the LFS pointer, not the PNG.
- **The frames this change moves:** `docked` only, 10 050 pixels against main inside a 960×528 box at
  the top-left, the flight-HUD block; the other nine dev-tool-free views land at nil with a 2 % fuzz.
  `dock-sequence` reproduces main's frame byte for byte at idle.

## Two gate failures that are not this work

Both are measured and written into `docs/build-and-gates.md`. Recognise them before chasing them.

- `jobs`, *"a completion can enqueue more work"*, `CHECK(stage == 1)` at `JobsTests.cpp:92`: the
  completion enqueues a second item, and a starved worker can have run it before the
  `WaitIdle()`/`Drain()` pair returns. It failed one gate run; it passes alone, and 4 for 4 at full
  core count on this tree.
- The reference stage on `dock-sequence`: the view is a docking cinematic placed by the sequence's
  own progress, so the screenshot lands on a frame of an animation. Measured with the current build:
  three runs under twelve busy loops on twenty-four cores, one at 14 516 pixels from the reference
  and two at nil; five runs at idle, all nil, one byte for byte the reference. A tick costs about 28
  times the stage's 518-pixel allowance. The reading to trust is the one at idle.

## Commands

```bash
cmake --build --preset clang-debug
ctest --test-dir build/clang-debug -R '^(devtools|render|app)$' --output-on-failure
./scripts/format-check.sh
./scripts/tidy.sh --build-dir build/clang-debug          # the whole tree, one copy per core
clang-tidy -p build/clang-debug source/engine/ui/imgui/src/Theme.cpp          # one unit
./scripts/capture.sh out.png --scene assets/scene/orbit.yaml --run "orbit 0.8 -0.1 2.0e7 Ostra"
./scripts/capture.sh --references                                             # compare, at idle
./scripts/capture.sh --references --update docked                             # record a frame
nohup ./scripts/ci.sh --werror --asan --log build/gate.log > /tmp/gate-run.log 2>&1 &
```

Nothing is bounded to 2 any more, on CI or here (`JETTISON_TIDY_JOBS`, `CMAKE_BUILD_PARALLEL_LEVEL`
and `CTEST_PARALLEL_LEVEL` are overrides only). Before a full-parallel gate, read `MemAvailable`:
a clang copy peaks at 1 051 MiB on the widest unit and a clang-tidy copy at 1 151 MiB, so the builds
want about 27 GiB at the worst and tidy about 28 GiB, against a 62 GiB machine at rest. The gate
takes about five minutes warm at full parallelism. Run it as a background task and wait on the log;
`--log` prints the stages, and for a failure the errors, with the full output in the file.

## Carried forward, from `docs/roadmap.md`

- The game UI's own RmlUi stylesheet in the same palette, so the market screen and the tools read as
  one design: a stylesheet over the VFS, no engine work.
- The palette as a player setting on the game's own screen, which needs the settings screen the
  sample does not have.
- A light-attachment swap noticed mid-run: `ApplyThemeIfChanged` answers when a setting changes.
- Exact darks: the encoding in the UI's own shader over a UNORM attachment, F5's further work above.
