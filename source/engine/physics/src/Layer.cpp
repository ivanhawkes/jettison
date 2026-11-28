#include <jettison/physics/Layer.h>

#include <algorithm>

namespace Jettison::Physics
{
bool LayersMeet(
    const LayerDefinitions &layers, const std::optional<std::string> &first,
    const std::optional<std::string> &second)
{
	if (!first || !second)
		return true;
	const auto names = [&layers](const std::string &layer, const std::string &other)
	{
		const auto found = layers.find(layer);
		return found != layers.end() &&
		       std::ranges::find(found->second.meets, other) != found->second.meets.end();
	};
	return names(*first, *second) || names(*second, *first);
}
} // namespace Jettison::Physics
