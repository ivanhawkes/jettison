#include <jettison/sim/Snapshot.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/sim/SimWorld.h>

#include <algorithm>

namespace Jettison::Sim
{
namespace
{
auto Identifier(const TransformSample &sample)
{
	return entt::to_integral(sample.entity);
}
} // namespace

void RegisterTypes()
{
	// The tables are global, and a process may hold more than one world.
	static const bool registered = []
	{
		using Reflect::Attribute;
		Reflect::RegisterType<Transform>("Sim.Transform")
		    .Field<&Transform::position>("position", Attribute::Saved)
		    .Field<&Transform::orientation>("orientation", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

const Transform *Snapshot::Find(entt::entity entity) const
{
	const auto found = std::ranges::lower_bound(transforms, entt::to_integral(entity), {}, Identifier);
	return found != transforms.end() && found->entity == entity ? &found->transform : nullptr;
}

Snapshot Capture(const SimWorld &world)
{
	Snapshot snapshot;
	snapshot.step = world.Clock().Step();
	snapshot.time = world.Clock().Now();
	const auto view = world.Registry().view<const Transform>();
	snapshot.transforms.reserve(view.size());
	for (const auto [entity, transform] : view.each())
		snapshot.transforms.push_back({entity, transform});
	std::ranges::sort(snapshot.transforms, {}, Identifier);
	return snapshot;
}

std::optional<Transform> Interpolate(
    const Snapshot &previous, const Snapshot &current, entt::entity entity, double alpha)
{
	const Transform *to = current.Find(entity);
	if (!to)
		return std::nullopt;
	const Transform *from = previous.Find(entity);
	if (!from)
		return *to;
	return Transform{
	    glm::mix(from->position, to->position, alpha),
	    glm::slerp(from->orientation, to->orientation, alpha)};
}
} // namespace Jettison::Sim
