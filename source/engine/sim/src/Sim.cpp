#include <jettison/sim/CommandQueue.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>

#include <jettison/core/Assert.h>

#include <utility>

namespace Jettison::Sim
{
void CommandQueue::Push(std::unique_ptr<Command> command)
{
	m_pending.push_back(std::move(command));
}
std::vector<std::unique_ptr<Command>> CommandQueue::Drain()
{
	return std::exchange(m_pending, {});
}

void SimWorld::Step(double /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::uint64_t SimWorld::StateHash() const
{
	JETTISON_NOT_IMPLEMENTED();
}

void DefinitionRegistry::Load(entt::meta_type /*unused*/, std::string_view /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
entt::meta_any DefinitionRegistry::Find(entt::meta_type /*unused*/, std::string_view /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Sim
