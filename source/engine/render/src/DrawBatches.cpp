#include <jettison/render/DrawBatches.h>

#include <algorithm>
#include <functional>
#include <tuple>

namespace Jettison::Render
{
namespace
{
// An order on keys, so equal keys sort together; which way round two keys go does not matter.
bool KeyLess(const BatchKey &left, const BatchKey &right)
{
	const std::less<const void *> pointerLess;
	if (left.model != right.model)
		return pointerLess(left.model, right.model);
	if (left.lod != right.lod)
		return left.lod < right.lod;
	return pointerLess(left.material, right.material);
}

bool NearerFirst(const BatchInput &left, const BatchInput &right)
{
	return std::tie(left.distance, left.draw) < std::tie(right.distance, right.draw);
}
} // namespace

void BuildBatches(
    std::span<const BatchInput> inputs, std::vector<DrawBatch> &batches,
    std::vector<std::uint32_t> &instances)
{
	std::vector<BatchInput> sorted{inputs.begin(), inputs.end()};
	// The instanced draws by key and then nearest first; the others after, each its own batch.
	std::sort(
	    sorted.begin(), sorted.end(),
	    [](const BatchInput &left, const BatchInput &right)
	    {
		    if (left.instanced != right.instanced)
			    return left.instanced;
		    if (left.instanced && !(left.key == right.key))
			    return KeyLess(left.key, right.key);
		    return NearerFirst(left, right);
	    });

	struct Group
	{
		std::size_t begin{0};
		std::size_t end{0};
	};
	std::vector<Group> groups;
	for (std::size_t index = 0; index < sorted.size(); ++index)
	{
		const bool joins = !groups.empty() && sorted[index].instanced &&
		                   sorted[groups.back().begin].instanced &&
		                   sorted[groups.back().begin].key == sorted[index].key;
		if (joins)
			groups.back().end = index + 1;
		else
			groups.push_back({index, index + 1});
	}
	// Each group's first member is its nearest.
	std::sort(
	    groups.begin(), groups.end(), [&](const Group &left, const Group &right)
	    { return NearerFirst(sorted[left.begin], sorted[right.begin]); });

	for (const Group &group : groups)
	{
		batches.push_back(
		    {sorted[group.begin].key, static_cast<std::uint32_t>(instances.size()),
		     static_cast<std::uint32_t>(group.end - group.begin)});
		for (std::size_t index = group.begin; index < group.end; ++index)
			instances.push_back(sorted[index].draw);
	}
}

void BuildBackToFront(
    std::span<const SortedInput> inputs, std::vector<SortedBatch> &batches,
    std::vector<std::uint32_t> &instances)
{
	std::vector<SortedInput> sorted{inputs.begin(), inputs.end()};
	std::sort(
	    sorted.begin(), sorted.end(),
	    [](const SortedInput &left, const SortedInput &right)
	    {
		    return std::tuple{-left.distance, left.particles, left.index} <
		           std::tuple{-right.distance, right.particles, right.index};
	    });
	for (std::size_t index = 0; index < sorted.size(); ++index)
	{
		const SortedInput &input = sorted[index];
		if (input.particles)
		{
			batches.push_back({true, input.index, {}});
			continue;
		}
		const bool joins = index > 0 && !batches.empty() && !batches.back().particles &&
		                   input.mergeable && sorted[index - 1].mergeable &&
		                   !sorted[index - 1].particles && batches.back().batch.key == input.key;
		if (joins)
			++batches.back().batch.instanceCount;
		else
			batches.push_back({false, 0, {input.key, static_cast<std::uint32_t>(instances.size()), 1}});
		instances.push_back(input.index);
	}
}
} // namespace Jettison::Render
