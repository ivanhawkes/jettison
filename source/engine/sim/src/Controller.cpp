#include <jettison/sim/Controller.h>

namespace Jettison::Sim
{
bool MayCommand(const entt::registry &registry, entt::entity entity, const Controller &sender)
{
	if (sender.kind == ControllerKind::None || !registry.valid(entity))
		return true;
	const auto *controlled = registry.try_get<ControlledBy>(entity);
	return !controlled || controlled->controller == sender;
}
} // namespace Jettison::Sim
