// Instancing's grouping (render/DrawBatches.h): draws that share a model, a level and a material
// become one batch, a skinned draw stays a batch of its own, and the batches go front to back.

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <jettison/render/DrawBatches.h>

using namespace Jettison::Render;

namespace
{
// Stand-ins: the batching compares the addresses, and never reads through them.
const std::array<char, 4> kModels{};
const Model *ModelAt(std::size_t index)
{
	return reinterpret_cast<const Model *>(&kModels.at(index));
}
const int kMaterial = 0;
} // namespace

TEST_CASE("Draw batches: draws sharing a model, a level and a material are one instanced draw")
{
	const BatchKey crate{ModelAt(1), 0, nullptr};
	const BatchKey crateFar{ModelAt(1), 2, nullptr};
	const BatchKey crateNamed{ModelAt(1), 0, &kMaterial};
	const BatchKey hull{ModelAt(2), 0, &kMaterial};
	const std::vector<BatchInput> inputs{
	    {crate, 0, 30.0, true},      {crate, 1, 10.0, true}, {crateFar, 2, 900.0, true},
	    {crateNamed, 3, 40.0, true}, {crate, 4, 20.0, true}, {hull, 5, 5.0, true},
	    {hull, 6, 5.0, false},       {hull, 7, 1.0, false},
	};
	std::vector<DrawBatch> batches;
	std::vector<std::uint32_t> instances{99};
	BuildBatches(inputs, batches, instances);

	// The skinned draws (7 and 6) each alone, and the rest by key, ordered by each batch's nearest.
	REQUIRE(batches.size() == 6);
	CHECK(batches[0].key == hull);
	CHECK(batches[0].instanceCount == 1);
	CHECK(instances[batches[0].firstInstance] == 7);
	// A tie on distance goes to the lower draw index.
	CHECK(instances[batches[1].firstInstance] == 5);
	CHECK(instances[batches[2].firstInstance] == 6);
	CHECK(batches[3].key == crate);
	CHECK(batches[3].instanceCount == 3);
	CHECK(batches[4].key == crateNamed);
	CHECK(batches[5].key == crateFar);

	// The instances are appended after what was there, nearest first within their batch.
	CHECK(instances.front() == 99);
	CHECK(batches[0].firstInstance == 1);
	const std::uint32_t first = batches[3].firstInstance;
	CHECK(instances[first] == 1);
	CHECK(instances[first + 1] == 4);
	CHECK(instances[first + 2] == 0);
	std::uint32_t total = 0;
	for (const DrawBatch &batch : batches)
		total += batch.instanceCount;
	CHECK(total == inputs.size());
	CHECK(instances.size() == inputs.size() + 1);
}

TEST_CASE("Draw batches: the order is the same whatever order the draws come in")
{
	const BatchKey a{ModelAt(1), 0, nullptr};
	const BatchKey b{ModelAt(2), 0, nullptr};
	std::vector<BatchInput> inputs{
	    {a, 0, 5.0, true}, {b, 1, 5.0, true}, {a, 2, 7.0, true}, {b, 3, 1.0, true}};
	std::vector<DrawBatch> forwards;
	std::vector<std::uint32_t> forwardInstances;
	BuildBatches(inputs, forwards, forwardInstances);
	std::vector<BatchInput> reversed{inputs.rbegin(), inputs.rend()};
	std::vector<DrawBatch> backwards;
	std::vector<std::uint32_t> backwardInstances;
	BuildBatches(reversed, backwards, backwardInstances);
	CHECK(forwardInstances == backwardInstances);
	REQUIRE(forwards.size() == 2);
	CHECK(forwards[0].key == b);
	CHECK(forwardInstances == std::vector<std::uint32_t>{3, 1, 0, 2});
}
