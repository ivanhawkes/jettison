#include <jettison/sim/CommandQueue.h>

#include <stdexcept>
#include <utility>

namespace Jettison::Sim
{
void CommandQueue::Push(entt::meta_any command)
{
	if (!command)
		throw std::logic_error{"CommandQueue: a command has no value"};
	m_pending.push_back(std::move(command));
}

std::vector<entt::meta_any> CommandQueue::Drain()
{
	return std::exchange(m_pending, {});
}
} // namespace Jettison::Sim
