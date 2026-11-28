#include <jettison/host/Package.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace Jettison::Host
{
namespace
{
std::array<unsigned, 3> VersionNumbers(std::string_view version)
{
	std::array<unsigned, 3> numbers{};
	std::size_t index = 0;
	const char *pAt = version.data();
	const char *pEnd = version.data() + version.size();
	while (true)
	{
		if (index == numbers.size())
			throw std::invalid_argument{std::format("'{}' is not a version of three numbers", version)};
		const auto [pNext, error] = std::from_chars(pAt, pEnd, numbers[index++]);
		if (error != std::errc{} || pNext == pAt)
			throw std::invalid_argument{std::format("'{}' is not a version of numbers", version)};
		if (pNext == pEnd)
			return numbers;
		if (*pNext != '.')
			throw std::invalid_argument{std::format("'{}' is not a version of numbers", version)};
		pAt = pNext + 1;
	}
}
} // namespace


void RegisterPackageTypes()
{
	static const bool registered = []
	{
		using Reflect::Attribute;

		Reflect::RegisterEnum<Side>("Host.Side")
		    .Value<Side::Both>("both")
		    .Value<Side::Client>("client");
		// The name it had in the shell, kept so a package's tooling that names it still reads.
		Reflect::RegisterType<PackageDescription>("Shell.ModDescription")
		    .Field<&PackageDescription::name>("name", Attribute::Saved)
		    .Field<&PackageDescription::release>("release", Attribute::Saved)
		    .Field<&PackageDescription::engine>("engine", Attribute::Saved)
		    .Field<&PackageDescription::required>("requires", Attribute::Saved)
		    .Field<&PackageDescription::side>("side", Attribute::Saved)
		    .Field<&PackageDescription::identifier>("identifier", Attribute::Saved);
		return true;
	}();
	(void)registered;
}


std::optional<PackageDescription> DescribePackage(const std::filesystem::path &source)
{
	RegisterPackageTypes();
	Vfs::FileSystem alone;
	alone.AddMount({"package", source});
	const std::optional<std::vector<std::byte>> bytes = alone.Read(kPackageDescriptionFile);
	if (!bytes)
		return std::nullopt;
	const std::string_view text{reinterpret_cast<const char *>(bytes->data()), bytes->size()};
	PackageDescription description =
	    Serialise::YamlArchive::Read<PackageDescription>(text, {kPackageFormatVersion});
	for (const std::string &required : description.required)
		if (required.starts_with(kModMountPrefix))
			throw std::runtime_error{std::format(
			    "it requires '{}', a mod's mount name; a mod is required by its identifier, '{}'",
			    required, required.substr(kModMountPrefix.size()))};
	return description;
}


bool Fulfils(std::string_view mount, std::string_view required)
{
	return mount == required ||
	       (mount.starts_with(kModMountPrefix) && mount.substr(kModMountPrefix.size()) == required);
}


bool EngineAtLeast(std::string_view engine, std::string_view required)
{
	return VersionNumbers(engine) >= VersionNumbers(required);
}


bool HoldsWorld(const std::filesystem::path &source)
{
	Vfs::FileSystem alone;
	alone.AddMount({"package", source});
	return std::ranges::any_of(
	    kWorldDirectories,
	    [&](std::string_view directory) { return !alone.Enumerate(directory).empty(); });
}


Vfs::PackSummary WriteServerPack(
    const std::filesystem::path &directory, const std::filesystem::path &pack, int level)
{
	const std::optional<PackageDescription> description = DescribePackage(directory);
	if (description && description->side == Side::Client)
		throw std::runtime_error{std::format(
		    "{} is for the client alone (side: client): a server has no use for it",
		    directory.string())};
	Vfs::PackOptions options{.level = level, .paths = {}};
	std::error_code error;
	for (const std::string_view world : kWorldDirectories)
		if (std::filesystem::is_directory(directory / world, error))
			options.paths.emplace_back(world);
	if (options.paths.empty())
		throw std::runtime_error{std::format(
		    "{} holds no world content, under defs or scripts, for a server", directory.string())};
	if (description)
		options.paths.emplace_back(kPackageDescriptionFile);
	return Vfs::WritePack(directory, pack, std::move(options));
}


void CheckSide(const PackageDescription &description, const std::filesystem::path &source)
{
	if (description.side == Side::Client && HoldsWorld(source))
		throw std::runtime_error{
		    "it is for the client alone (side: client) yet holds world content, under defs or scripts"};
}
} // namespace Jettison::Host
