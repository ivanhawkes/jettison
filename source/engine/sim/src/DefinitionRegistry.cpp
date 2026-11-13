#include <jettison/sim/DefinitionRegistry.h>

#include <jettison/core/Log.h>
#include <jettison/vfs/FileSystem.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Jettison::Sim
{
namespace
{
constexpr Core::Category kDefinitions{"definitions"};
constexpr std::string_view kExtension = ".yaml";
constexpr std::string_view kPatchExtension = ".patch.yaml";

// One mount's file for a definition: its whole file, or its patch.
struct Source
{
	std::size_t mount{0};
	bool patch{false};
	std::string path;
	std::vector<std::byte> bytes;
};

std::string_view Text(const std::vector<std::byte> &bytes)
{
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

// "patched by 'mod:a', 'mod:b'": the mounts named, in order.
std::string MountNames(const Vfs::FileSystem &fileSystem, std::span<const Source> sources)
{
	std::string names;
	for (const Source &source : sources)
		names +=
		    std::format("{}'{}'", names.empty() ? "" : ", ", fileSystem.Mounts()[source.mount].name);
	return names;
}
} // namespace

DefinitionRegistry::DefinitionRegistry(const Vfs::FileSystem &fileSystem) : m_fileSystem{fileSystem}
{
}

void DefinitionRegistry::Load(
    const entt::meta_type &type, std::string_view virtualDirectory, Serialise::FormatVersion version)
{
	LoadWith(
	    type, virtualDirectory, [&type, version](std::span<const Serialise::YamlLayer> layers)
	    { return Serialise::YamlArchive::ReadLayered(layers, type, version); });
}

void DefinitionRegistry::Load(
    const entt::meta_type &type, std::string_view virtualDirectory,
    const Serialise::YamlMigrations &migrations)
{
	LoadWith(
	    type, virtualDirectory, [&type, &migrations](std::span<const Serialise::YamlLayer> layers)
	    { return Serialise::YamlArchive::ReadLayered(layers, type, migrations); });
}

void DefinitionRegistry::LoadWith(
    const entt::meta_type &type, std::string_view virtualDirectory,
    const std::function<entt::meta_any(std::span<const Serialise::YamlLayer>)> &read)
{
	if (!type || !type.is_class() || !entt::resolve(type.info()))
		throw Serialise::UnsupportedTypeError{std::format(
		    "Definitions: '{}' is not a class registered with Reflect",
		    type ? type.info().name() : "(none)")};

	// Every definition the directory names, whole or patched, each once and in order.
	const std::string prefix =
	    virtualDirectory.empty() ? std::string{} : std::string{virtualDirectory} + '/';
	std::set<std::string> names;
	for (const std::string &path : m_fileSystem.Enumerate(virtualDirectory))
	{
		if (!path.ends_with(kExtension))
			continue;
		const std::size_t suffix =
		    path.ends_with(kPatchExtension) ? kPatchExtension.size() : kExtension.size();
		names.insert(path.substr(prefix.size(), path.size() - prefix.size() - suffix));
	}

	// Built aside and swapped in, so a bad file leaves the type's previous set in place.
	Definitions definitions;
	for (const std::string &name : names)
	{
		// Each mount's whole file, then its patch, the bottom mount's first. A whole file replaces
		// everything below it; the patches above the topmost one are merged into it in order.
		std::vector<Source> sources;
		for (const bool patch : {false, true})
		{
			std::string path = prefix + name + std::string{patch ? kPatchExtension : kExtension};
			for (Vfs::Layer &layer : m_fileSystem.ReadLayers(path))
				sources.push_back({layer.mount, patch, path, std::move(layer.bytes)});
		}
		std::ranges::stable_sort(
		    sources, {}, [](const Source &source) { return std::pair{source.mount, source.patch}; });
		const auto whole = std::ranges::find_if(
		    sources.rbegin(), sources.rend(), [](const Source &source) { return !source.patch; });
		if (whole == sources.rend())
			throw Serialise::FormatError{std::format(
			    "Definitions: {}{} patches a definition that no mount has a whole file for", prefix,
			    name + std::string{kPatchExtension})};
		const std::span<const Source> layered{std::prev(whole.base()), sources.end()};

		std::vector<Serialise::YamlLayer> layers;
		std::vector<std::string> layerNames;
		layers.reserve(layered.size());
		layerNames.reserve(layered.size());
		for (const Source &source : layered)
			layerNames.push_back(
			    std::format("{} in '{}'", source.path, m_fileSystem.Mounts()[source.mount].name));
		for (std::size_t index = 0; index < layered.size(); ++index)
			layers.push_back({layerNames[index], Text(layered[index].bytes)});
		try
		{
			definitions.emplace(name, read(layers));
		}
		// A definition of a version the type's migrations do not reach is refused like any file that
		// does not fit. The archive's errors name the layers, each a file and its mount.
		catch (const std::runtime_error &error)
		{
			throw Serialise::FormatError{std::format("Definitions: {}", error.what())};
		}

		// What a mod changes is said, so a player whose game differs can see which mod did it.
		if (layered.front().mount != sources.front().mount || layered.size() > 1)
		{
			const std::span<const Source> patches = layered.subspan(1);
			const bool replaced = layered.front().mount != sources.front().mount;
			Core::Log(
			    Core::Severity::Info, kDefinitions, "{}{}: {}{}{}", prefix, name,
			    replaced
			        ? std::format("replaced by '{}'", m_fileSystem.Mounts()[layered.front().mount].name)
			        : std::string{},
			    replaced && !patches.empty() ? " and " : "",
			    patches.empty() ? std::string{} : "patched by " + MountNames(m_fileSystem, patches));
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
