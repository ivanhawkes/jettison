#include <jettison/host/WorldContent.h>

#include <jettison/core/Hash.h>
#include <jettison/host/Package.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

#include <algorithm>
#include <format>
#include <string_view>

namespace Jettison::Host
{
namespace
{
std::string_view AsText(std::span<const std::byte> bytes)
{
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}


// The package's world, or none for a source that holds none.
std::optional<WorldPackage> Describe(const Vfs::Mount &mount)
{
	Vfs::FileSystem alone;
	alone.AddMount({mount.name, mount.source});
	std::uint64_t digest = Core::Fnv1a64("");
	bool holdsWorld = false;
	for (const std::string_view directory : kWorldDirectories)
		for (const std::string &path : alone.Enumerate(directory))
		{
			holdsWorld = true;
			digest = Core::HashCombine(digest, Core::Fnv1a64(path));
			digest = Core::HashCombine(digest, Core::Fnv1a64(AsText(*alone.Read(path))));
		}
	if (!holdsWorld)
		return std::nullopt;
	const std::optional<PackageDescription> description = DescribePackage(mount.source);
	return WorldPackage{mount.name, description ? description->release : std::string{}, digest};
}


std::string Named(const WorldPackage &package)
{
	return package.release.empty() ? std::format("'{}'", package.mount)
	                               : std::format("'{}' {}", package.mount, package.release);
}
} // namespace


void RegisterWorldContentTypes()
{
	static const bool registered = []
	{
		using Reflect::Attribute;
		Reflect::RegisterType<WorldPackage>("Host.WorldPackage")
		    .Field<&WorldPackage::mount>("mount", Attribute::Saved)
		    .Field<&WorldPackage::release>("release", Attribute::Saved)
		    .Field<&WorldPackage::digest>("digest", Attribute::Saved);
		Reflect::RegisterType<WorldContent>("Host.WorldContent")
		    .Field<&WorldContent::packages>("packages", Attribute::Saved);
		return true;
	}();
	(void)registered;
}


WorldContent DescribeWorldContent(const Vfs::FileSystem &files)
{
	RegisterPackageTypes();
	WorldContent content;
	for (const Vfs::Mount &mount : files.Mounts())
		if (std::optional<WorldPackage> package = Describe(mount))
			content.packages.push_back(std::move(*package));
	return content;
}


std::vector<std::byte> EncodeWorldContent(const WorldContent &content)
{
	RegisterWorldContentTypes();
	return Serialise::BinaryArchive::Write(content, {kWorldContentFormatVersion});
}


WorldContent DecodeWorldContent(std::span<const std::byte> bytes)
{
	RegisterWorldContentTypes();
	return Serialise::BinaryArchive::Read<WorldContent>(bytes, {kWorldContentFormatVersion});
}


std::optional<std::string> ContentDifference(const WorldContent &held, const WorldContent &served)
{
	const auto find = [](const WorldContent &content, const std::string &mount)
	{ return std::ranges::find(content.packages, mount, &WorldPackage::mount); };
	for (const WorldPackage &package : served.packages)
	{
		const auto ours = find(held, package.mount);
		if (ours == held.packages.end())
			return std::format(
			    "The server runs on {}, which this client does not hold", Named(package));
		if (ours->release != package.release)
			return std::format(
			    "This client holds {}, and the server runs on {}", Named(*ours), Named(package));
		if (ours->digest != package.digest)
			return std::format(
			    "This client's {} is not the server's: its world content differs", Named(*ours));
	}
	for (const WorldPackage &package : held.packages)
		if (find(served, package.mount) == served.packages.end())
			return std::format(
			    "This client holds {}, which changes the world and which the server does not run on",
			    Named(package));
	if (held != served)
		return std::string{"This client mounts the server's packages in another order"};
	return std::nullopt;
}
} // namespace Jettison::Host
