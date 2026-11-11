#include <jettison/sim/DefinitionRegistry.h>

#include <jettison/vfs/FileSystem.h>

#include <format>
#include <optional>
#include <string>
#include <utility>

namespace Jettison::Sim
{
namespace
{
constexpr std::string_view kExtension = ".yaml";
} // namespace

DefinitionRegistry::DefinitionRegistry(const Vfs::FileSystem &fileSystem) : m_fileSystem{fileSystem}
{
}

void DefinitionRegistry::Load(
    const entt::meta_type &type, std::string_view virtualDirectory, Serialise::FormatVersion version)
{
	LoadWith(
	    type, virtualDirectory, [&type, version](std::string_view text)
	    { return Serialise::YamlArchive::Read(text, type, version); });
}

void DefinitionRegistry::Load(
    const entt::meta_type &type, std::string_view virtualDirectory,
    const Serialise::YamlMigrations &migrations)
{
	LoadWith(
	    type, virtualDirectory, [&type, &migrations](std::string_view text)
	    { return Serialise::YamlArchive::Read(text, type, migrations); });
}

void DefinitionRegistry::LoadWith(
    const entt::meta_type &type, std::string_view virtualDirectory,
    const std::function<entt::meta_any(std::string_view)> &read)
{
	if (!type || !type.is_class() || !entt::resolve(type.info()))
		throw Serialise::UnsupportedTypeError{std::format(
		    "Definitions: '{}' is not a class registered with Reflect",
		    type ? type.info().name() : "(none)")};

	// Built aside and swapped in, so a bad file leaves the type's previous set in place.
	Definitions definitions;
	const std::string prefix =
	    virtualDirectory.empty() ? std::string{} : std::string{virtualDirectory} + '/';
	for (const std::string &path : m_fileSystem.Enumerate(virtualDirectory))
	{
		if (!path.ends_with(kExtension))
			continue;
		const std::optional<std::vector<std::byte>> bytes = m_fileSystem.Read(path);
		if (!bytes)
			throw Serialise::FormatError{
			    std::format("Definitions: {} was listed but could not be read", path)};
		const std::string_view text{reinterpret_cast<const char *>(bytes->data()), bytes->size()};
		try
		{
			std::string name =
			    path.substr(prefix.size(), path.size() - prefix.size() - kExtension.size());
			definitions.emplace(std::move(name), read(text));
		}
		// A definition of a version the type's migrations do not reach is refused like any file that
		// does not fit.
		catch (const std::runtime_error &error)
		{
			throw Serialise::FormatError{std::format("Definitions: {}: {}", path, error.what())};
		}
	}
	m_types[type.info().hash()] = std::move(definitions);
}

const entt::meta_any *DefinitionRegistry::Find(const entt::meta_type &type, std::string_view name) const
{
	const auto definitions = m_types.find(type.info().hash());
	if (definitions == m_types.end())
		return nullptr;
	const auto found = definitions->second.find(name);
	return found == definitions->second.end() ? nullptr : &found->second;
}

std::vector<std::string> DefinitionRegistry::Names(const entt::meta_type &type) const
{
	std::vector<std::string> names;
	if (const auto definitions = m_types.find(type.info().hash()); definitions != m_types.end())
	{
		for (const auto &[name, definition] : definitions->second)
			names.push_back(name);
	}
	return names;
}

std::size_t DefinitionRegistry::Count() const
{
	std::size_t count = 0;
	for (const auto &[type, definitions] : m_types)
		count += definitions.size();
	return count;
}
} // namespace Jettison::Sim
