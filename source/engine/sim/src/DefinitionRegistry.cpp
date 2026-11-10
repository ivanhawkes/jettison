#include <jettison/sim/DefinitionRegistry.h>

#include <jettison/core/Assert.h>

namespace Jettison::Sim
{
void DefinitionRegistry::Load(entt::meta_type /*unused*/, std::string_view /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
entt::meta_any DefinitionRegistry::Find(entt::meta_type /*unused*/, std::string_view /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Sim
