#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/serialise/Archive.h>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace YAML
{
class Node;
}

namespace Jettison::Serialise
{
template <typename Data> class Migrations;
using YamlMigrations = Migrations<YAML::Node>;
} // namespace Jettison::Serialise

namespace Jettison::Sim
{
// Typed, reflected definitions loaded from data and overridable by mods: a ship class, a good, a
// station module. The game declares each type with Reflect, its Saved fields what a file sets.
//
// A definition is one YAML file under a directory, in YamlArchive's form, and its name is the
// file's path under the directory without ".yaml": "defs/ships/military/corvette.yaml" loaded
// from "defs/ships" is "military/corvette". The VFS gives the topmost file at each path, so a mod
// overrides a definition by shipping a file at the same path, and adds one with a new path. A
// whole definition is replaced, not patched field by field; layering finer than that is phase
// 11's. Saves refer to definitions by name, so a save holds none, and a definition's format can
// change without one.
//
// Definitions migrate (docs/roadmap.md, "Decided"): a type's files are read through its YAML
// migrations when it has them, so a mod written against an earlier version of a definition still
// loads after the type gains a field or renames one, without its author shipping it again.
//
// Definitions are loaded at start-up and read by the simulation, so they are kept in ordered maps:
// Names() and any walk over them is the same on every run.
class DefinitionRegistry
{
  public:
	explicit DefinitionRegistry(const Vfs::FileSystem &fileSystem);

	// Loads every definition of the type under the directory, replacing the type's previous set.
	// A file that does not fit the type, or is of another version, throws FormatError naming the
	// file, and the previous set stays. Throws UnsupportedTypeError for a type Reflect does not
	// know as a class.
	void Load(
	    const entt::meta_type &type, std::string_view virtualDirectory,
	    Serialise::FormatVersion version);
	template <typename T> void Load(std::string_view virtualDirectory, Serialise::FormatVersion version)
	{
		Load(entt::resolve<T>(), virtualDirectory, version);
	}
	// The same, with a file of an earlier version migrated first; one of a version the chain does
	// not reach is refused as above. The chain's format is the type's name, as Reflect has it.
	void Load(
	    const entt::meta_type &type, std::string_view virtualDirectory,
	    const Serialise::YamlMigrations &migrations);
	template <typename T>
	void Load(std::string_view virtualDirectory, const Serialise::YamlMigrations &migrations)
	{
		Load(entt::resolve<T>(), virtualDirectory, migrations);
	}

	// The definition of that name, or null. Valid until the type is loaded again.
	const entt::meta_any *Find(const entt::meta_type &type, std::string_view name) const;
	template <typename T> const T *Find(std::string_view name) const
	{
		const entt::meta_any *definition = Find(entt::resolve<T>(), name);
		return definition ? definition->try_cast<T>() : nullptr;
	}

	// The type's definition names, sorted.
	std::vector<std::string> Names(const entt::meta_type &type) const;
	template <typename T> std::vector<std::string> Names() const
	{
		return Names(entt::resolve<T>());
	}

	// Definitions of every type.
	std::size_t Count() const;

  private:
	using Definitions = std::map<std::string, entt::meta_any, std::less<>>;

	void LoadWith(
	    const entt::meta_type &type, std::string_view virtualDirectory,
	    const std::function<entt::meta_any(std::string_view)> &read);

	const Vfs::FileSystem &m_fileSystem;
	// Keyed by EnTT's type ID.
	std::map<entt::id_type, Definitions> m_types;
};
} // namespace Jettison::Sim
