# History

The first roadmap: the work that made the existing renderer trustworthy before any new system
was built on it. Its order was set by what endangered the engine. Each row is in the commit
history, and the narratives below record what each step found, because the findings became
rules in the [coding standards](coding-standards.md).

| # | Work | State |
|---|---|---|
| 1 | Repository hygiene: the abandoned submodule stores, Git LFS for binary assets, `.gitattributes` | done |
| 2 | Truthful gates: the format gate, the worktrunk hooks, `scripts/ci.sh`, the headless render test | local gate passes; GitHub run pending |
| 3 | Delete what the build does not need: the ImGui sample program, `RenderPass`, `ImGuiPipeline`, 667 lines | done |
| 4 | Diagnostics: a measured warning set that survives `-Werror`, a curated clang-tidy set, an ASan/UBSan preset | done |
| 5 | GPU lifetime: `VK_EXT_debug_utils` with named objects, scene-reload invalidation, the extent clamp, pool sizing derived from what is allocated | done |
| 6 | App shell: the lifecycle, the staged init, the frame loop and the smoke sequence, split out of `Loop`'s 200 lines | implemented; partial-startup failure coverage incomplete |
| 7 | Maintenance: GLM 0.9.9.8 → 1.0.1 with its configuration moved to the `glm` target, the swapchain-recreate TODOs on the command pool, the animation phase out of authored scene state | done |
| 8 | What row 7 found: `Renderer::Present()` was an empty stub nothing called, and `Scene::Save` wrote animation for an entity with no mesh, which `Scene::Load` refused | done |

Two items from this table carry into the new [roadmap](roadmap.md): the GitHub run of the
corrected smoke harness (row 2), and partial-startup failure coverage for the shell (row 6),
which phase 1 picks up when it moves `Application` into `engine/app`.

## Row 5: GPU lifetime

Step 5 settled the hazards in the plan, in the order the instrumentation found them. A scene
reload destroyed model buffers that a submitted command buffer still referenced
(`VUID-vkDestroyBuffer-buffer-00922`). The pipeline keyed model resources on the model's
address, which a reload reuses. The joint-palette resize turned out to be fenced by the frame
loop's per-image wait, which is now written down where the destroy happens. The frame pool's
unexplained `+100` was counting descriptor sets that live in pools of their own.

**Rules this produced:** destroy GPU resources only behind the fence that covers them, and key
caches on handles with generations, not on addresses.

## Row 6: App shell

`Loop`'s 200 lines became three files. Argument parsing is in `AppArguments`. The staged init,
the frame loop and the ordered teardown are in `Application`. The six stages are in
`SmokeSequence`, driven through callbacks. The behaviour that changed was the one the step was
for. A frame that throws now gets the same teardown as a frame loop that returns, because
`~Application` runs the `Destroy()` calls that unwinding used to skip. A headless run that fails
the gate is a throw, so the sequence's own failure is now reported by a process that has
released the device it was testing.

## Row 7: Maintenance

All three items turned out to be about state that was in the wrong place.

**GLM.** GLM 1.0.1 is byte-identical to 0.9.9.8 on the geometry the shaders and buffers agree
on: `sizeof(Vertex)` is 108, `sizeof(FrameUBO)` is 192, and every `offsetof` is the same. That
was measured by compiling one probe against both sources. What moved was GLM's configuration. It
sat in `Model.h` above the GLM includes, where it reached only the translation units that
included that header first.

`glm::perspective(radians(45), 1.25, 0.1, 10)` gives `[2][2]` = -1.010101 with
`GLM_FORCE_DEPTH_ZERO_TO_ONE` and -1.020202 without it, so reordering an include changed the
depth range. The macros are now compile definitions on the `glm` interface target. The two inert
ones are gone: GLM 1.0 has no degree mode, and GLM disables aligned gentypes on this compiler,
which is why `sizeof(glm::vec3)` has always been 12 here.

**Command pool.** The three command-pool TODOs asked whether the pool is recreated with the
swapchain. It is not, because a recreate changes the swapchain handle and not the device's queue
family. The descriptor set layout is part of the pipeline rebuild, not something left out of it.
Both answers are written where the code asked. The one substantive change: `Pipeline::Destroy()`
now clears `m_commandBuffers` after freeing them, so no recreate reads an image count off a list
of freed handles.

**Scene round trip.** The frame loop was writing authored state. After ten seconds of a
0.7854 rad/s spin, an entity whose authored `rotation.y` was 0.3 held 8.154025 in the graph,
and the saved file carried `time: 1.25`. The phase now lives in
`RotationAnimationComponent::angle`, composed into the world transform alongside the rotation.
The world pose is unchanged to within 2.2e-5 rad, and the saved file says 0.3 and has no time.

**Rule this produced:** authored state and runtime state are separate fields.

## Row 8: What row 7 found

Both halves were a rule with two owners.

**Present.** `Renderer::Present()` was an empty body holding a question, while the present call,
its out-of-date check and the frame-slot move sat at the end of `Render`. Lifting them out was
the move. The part that was not mechanical: "out of date" means two different things on the two
sides of a submit. Render's acquire leaves the frame slot alone, because that frame queued
nothing. Present moves it, because that frame queued everything.

**Scene round trip.** The loader and the writer each enforced a mesh rule the other did not know
about, and the file in the middle lost. A registry the engine saved came back as "Entity
transform animation needs a mesh and must be a mapping": the loader refused a document its own
writer had produced. A rotation animation runs on the transform, so the loader now accepts it
without a mesh. A clip player runs on a model's skin, so the writer no longer writes one with no
model to name.

**Rule this produced:** one rule, one owner, with a round-trip test for every format.
