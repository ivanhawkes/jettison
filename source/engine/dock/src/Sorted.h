#pragma once

#include <entt/entt.hpp>

#include <algorithm>
#include <vector>

namespace Jettison::Dock::Detail
{
// The entities with the components, in identifier order: the order the step works them in, which a
// storage's own does not keep across a save.
template <typename... Components> std::vector<entt::entity> Sorted(const entt::registry &registry)
{
	std::vector<entt::entity> entities;
	for (const entt::entity entity : registry.view<const Components...>())
		entities.push_back(entity);
	std::ranges::sort(entities, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	return entities;
}
} // namespace Jettison::Dock::Detail
