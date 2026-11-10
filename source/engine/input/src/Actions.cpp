#include <jettison/input/Actions.h>

#include <jettison/core/Assert.h>

#include <utility>

namespace Jettison::Input
{
void InputSystem::DefineAction(ActionDefinition action)
{
	m_actions.push_back(std::move(action));
}
void InputSystem::PushContext(std::string_view /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void InputSystem::PopContext()
{
	JETTISON_NOT_IMPLEMENTED();
}
void InputSystem::Poll()
{
	JETTISON_NOT_IMPLEMENTED();
}
bool InputSystem::IsPressed(std::string_view /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
bool InputSystem::WasPressedThisFrame(std::string_view /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
float InputSystem::Axis(std::string_view /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
glm::vec2 InputSystem::Vector(std::string_view /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Input
