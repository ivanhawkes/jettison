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
} // namespace Jettison::Render
