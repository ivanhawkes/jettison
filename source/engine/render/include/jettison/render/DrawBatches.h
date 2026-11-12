#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace Jettison::Render
{
class Model;

// Instancing (docs/architecture.md, "The frame"): the draws that share a model, a level of detail and
// a material become one instanced draw, each instance's transform read from the frame's instance
// buffer by gl_InstanceIndex. A skinned draw is a batch of its own, since its vertices are its own
// pose. Nothing here touches the device, so the render suite checks the grouping and the order.

struct BatchKey
{
	const Model *model{};
	std::uint32_t lod{0};
	// The draw's named material, or null for the model's own; the shadow pass, which binds none,
	// passes null for every draw.
	const void *material{};

	friend bool operator==(const BatchKey &, const BatchKey &) = default;
};

struct BatchInput
{
	BatchKey key;
	// The draw's index in the frame's draw list.
	std::uint32_t draw{0};
	// How far its bounds' nearest point is from the camera, in metres; what the batches are ordered by.
	double distance{0.0};
	bool instanced{true};
};

struct DrawBatch
{
	BatchKey key;
	// A run of the frame's instances, from this index of BuildBatches' `instances`.
	std::uint32_t firstInstance{0};
	std::uint32_t instanceCount{0};
};

// The transparent draws (docs/architecture.md, "Transparency"): each of a mesh's instances, or an
// emitter's particles, which the transparent pass blends over what is behind it, and so draws back to
// front. A particle draw has no key; its index is the emitter's in the frame's list of them.
struct SortedInput
{
	BatchKey key;
	// The draw's index in the frame's draw list, or the emitter's.
	std::uint32_t index{0};
	// How far its centre is from the camera, in metres.
	double distance{0.0};
	bool particles{false};
	// Whether it may share an instanced draw with its neighbours of the same key: a level of one
	// primitive, since an instanced draw lays down each primitive for every instance before the next,
	// and the far instance's second would land over the near one's first.
	bool mergeable{true};
};

struct SortedBatch
{
	// An emitter's particles, or a run of mesh instances.
	bool particles{false};
	std::uint32_t emitter{0};
	DrawBatch batch;
};

// Orders the transparent draws furthest first, ties broken by kind (meshes first) and index, so the
// order is the same every frame for the same scene, and makes each run of mergeable draws of one key
// that are neighbours in that order one instanced draw, its instances appended to `instances`, still
// furthest first, after what is there.
void BuildBackToFront(
    std::span<const SortedInput> inputs, std::vector<SortedBatch> &batches,
    std::vector<std::uint32_t> &instances);

// Groups the draws into batches, ordered front to back by each batch's nearest instance so the depth
// test rejects what is behind before it is shaded, and appends each instance's draw index to
// `instances`, nearest first within its batch: the frame's view and its shadow cascades share one
// list. Ties are broken by draw index, so the order is the same every frame for the same scene.
void BuildBatches(
    std::span<const BatchInput> inputs, std::vector<DrawBatch> &batches,
    std::vector<std::uint32_t> &instances);
} // namespace Jettison::Render
