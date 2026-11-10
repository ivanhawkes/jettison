#include <jettison/ecs/Scheduler.h>

#include <jettison/core/Assert.h>

#include <utility>

namespace Jettison::Ecs
{
void Scheduler::Register(SystemDescription system)
{
	m_systems.push_back(std::move(system));
}
void Scheduler::Run(TickPhase /*unused*/, entt::registry & /*unused*/, double /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Ecs
