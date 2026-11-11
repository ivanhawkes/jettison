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

// Groups the draws into batches, ordered front to back by each batch's nearest instance so the depth
// test rejects what is behind before it is shaded, and appends each instance's draw index to
// `instances`, nearest first within its batch: the frame's view and its shadow cascades share one
// list. Ties are broken by draw index, so the order is the same every frame for the same scene.
void BuildBatches(
    std::span<const BatchInput> inputs, std::vector<DrawBatch> &batches,
    std::vector<std::uint32_t> &instances);
} // namespace Jettison::Render
