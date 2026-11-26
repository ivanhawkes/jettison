#include <jettison/host/Package.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <system_error>

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

		// The name it had in the shell, kept so a package's tooling that names it still reads.
		Reflect::RegisterType<PackageDescription>("Shell.ModDescription")
		    .Field<&PackageDescription::name>("name", Attribute::Saved)
		    .Field<&PackageDescription::release>("release", Attribute::Saved)
		    .Field<&PackageDescription::engine>("engine", Attribute::Saved)
		    .Field<&PackageDescription::required>("requires", Attribute::Saved);
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
	return Serialise::YamlArchive::Read<PackageDescription>(text, {kPackageFormatVersion});
}


bool EngineAtLeast(std::string_view engine, std::string_view required)
{
	return VersionNumbers(engine) >= VersionNumbers(required);
}
} // namespace Jettison::Host
