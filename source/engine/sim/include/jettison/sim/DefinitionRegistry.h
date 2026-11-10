#pragma once

#include <entt/entt.hpp>

#include <string>
#include <string_view>

namespace Jettison::Sim
{
// Typed, reflected definitions loaded from data and overridable by mods: a ship class, a good, a
// station module. The game declares the types with Reflect; the registry loads every definition
// file under a directory across the VFS mounts, later mounts overriding earlier ones by
// definition name. Filled in phase 3.
class DefinitionRegistry
{
  public:
	// Loads every definition of a registered type from "<directory>/*.yaml" through the VFS.
	void Load(entt::meta_type type, std::string_view virtualDirectory);
	// The definition with that name, or an empty meta_any.
	entt::meta_any Find(entt::meta_type type, std::string_view name) const;
	std::size_t Count() const
	{
		return m_count;
	}

  private:
	std::size_t m_count{0};
};
} // namespace Jettison::Sim
