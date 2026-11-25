#pragma once

#include <entt/entt.hpp>

#include <algorithm>
#include <vector>

namespace Jettison::Dock::Detail
{
// The entities with the components, in identifier order: the order the step works them in. A
// storage's own order is its history of additions and removals, and a view walks its smallest
// storage, so neither is an order to rely on (docs/coding-standards.md, "Simulation code").
template <typename... Components> std::vector<entt::entity> Sorted(const entt::registry &registry)
{
	std::vector<entt::entity> entities;
	for (const entt::entity entity : registry.view<const Components...>())
		entities.push_back(entity);
	std::ranges::sort(entities, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	return entities;
}
} // namespace Jettison::Dock::Detail
