#include <jettison/vfs/FileSystem.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <jettison/core/File.h>

namespace Jettison::Vfs
{
namespace
{
void Validate(std::string_view virtualPath)
{
	if (!FileSystem::IsValidPath(virtualPath))
		throw std::invalid_argument{std::format("Vfs: '{}' is not a valid virtual path", virtualPath)};
}

bool IsRegularFile(const std::filesystem::path &path)
{
	std::error_code error;
	return std::filesystem::is_regular_file(path, error);
}
} // namespace

bool FileSystem::IsValidPath(std::string_view virtualPath)
{
	if (virtualPath.empty())
		return false;
	for (std::string_view rest = virtualPath;;)
	{
		const std::size_t slash = rest.find('/');
		const std::string_view segment = rest.substr(0, slash);
		if (segment.empty() || segment == "." || segment == "..")
			return false;
		for (const char character : segment)
		{
			if (character == '\\' || character == ':' || static_cast<unsigned char>(character) < 0x20 ||
			    character == 0x7f)
				return false;
		}
		if (slash == std::string_view::npos)
			return true;
		rest.remove_prefix(slash + 1);
	}
}

void FileSystem::AddMount(Mount mount)
{
	if (mount.name.empty())
		throw std::invalid_argument{"Vfs: a mount needs a name"};
	for (const Mount &existing : m_mounts)
	{
		if (existing.name == mount.name)
			throw std::invalid_argument{
			    std::format("Vfs: a mount named '{}' is already mounted", mount.name)};
		if (existing.writable)
			throw std::invalid_argument{std::format(
			    "Vfs: '{}' is mounted after the writable mount '{}', which must be last so a file "
			    "written is the file read",
			    mount.name, existing.name)};
	}

	mount.source = std::filesystem::absolute(mount.source).lexically_normal();
	std::error_code error;
	if (mount.writable)
	{
		std::filesystem::create_directories(mount.source, error);
		if (error)
			throw std::runtime_error{std::format(
			    "Vfs: the writable mount '{}' at {} could not be created: {}", mount.name,
			    mount.source.string(), error.message())};
	}
	if (!std::filesystem::is_directory(mount.source, error))
		throw std::runtime_error{std::format(
		    "Vfs: the mount '{}' at {} is not a directory", mount.name, mount.source.string())};
	m_mounts.push_back(std::move(mount));
}

bool FileSystem::Exists(std::string_view virtualPath) const
{
	return Resolve(virtualPath).has_value();
}

std::optional<std::filesystem::path> FileSystem::Resolve(std::string_view virtualPath) const
{
	Validate(virtualPath);
	for (const Mount &mount : std::ranges::reverse_view(m_mounts))
	{
		std::filesystem::path path = mount.source / virtualPath;
		if (IsRegularFile(path))
			return path;
	}
	return std::nullopt;
}

std::optional<std::vector<std::byte>> FileSystem::Read(std::string_view virtualPath) const
{
	const std::optional<std::filesystem::path> path = Resolve(virtualPath);
	if (!path)
		return std::nullopt;

	std::ifstream file{*path, std::ios::binary | std::ios::ate};
	const std::streamoff size = file ? static_cast<std::streamoff>(file.tellg()) : -1;
	if (size < 0)
		throw std::runtime_error{std::format("Vfs: {} could not be opened", path->string())};
	std::vector<std::byte> bytes(static_cast<std::size_t>(size));
	file.seekg(0);
	file.read(reinterpret_cast<char *>(bytes.data()), size);
	if (file.gcount() != size)
		throw std::runtime_error{std::format("Vfs: {} could not be read", path->string())};
	return bytes;
}

void FileSystem::Write(std::string_view virtualPath, std::span<const std::byte> bytes) const
{
	Validate(virtualPath);
	const auto mount =
	    std::ranges::find_if(m_mounts, [](const Mount &candidate) { return candidate.writable; });
	if (mount == m_mounts.end())
		throw std::logic_error{
		    std::format("Vfs: '{}' cannot be written: there is no writable mount", virtualPath)};

	const std::filesystem::path path = mount->source / virtualPath;
	try
	{
		Core::WriteFileDurably(path, bytes);
	}
	catch (const std::runtime_error &error)
	{
		throw std::runtime_error{std::format("Vfs: {}", error.what())};
	}
}

std::vector<std::string> FileSystem::Enumerate(std::string_view virtualDirectory) const
{
	if (!virtualDirectory.empty())
		Validate(virtualDirectory);

	std::vector<std::string> paths;
	for (const Mount &mount : m_mounts)
	{
		const std::filesystem::path root = mount.source / virtualDirectory;
		std::error_code error;
		if (!std::filesystem::is_directory(root, error))
			continue;
		for (auto entry =
		         std::filesystem::recursive_directory_iterator{
		             root, std::filesystem::directory_options::follow_directory_symlink, error};
		     !error && entry != std::filesystem::recursive_directory_iterator{}; entry.increment(error))
		{
			// Its own error code: a broken link is skipped, not a failure of the listing.
			std::error_code entryError;
			if (!entry->is_regular_file(entryError))
				continue;
			std::string path = entry->path().lexically_relative(mount.source).generic_string();
			if (IsValidPath(path))
				paths.push_back(std::move(path));
		}
		if (error)
			throw std::runtime_error{
			    std::format("Vfs: {} could not be listed: {}", root.string(), error.message())};
	}
	std::ranges::sort(paths);
	const auto duplicates = std::ranges::unique(paths);
	paths.erase(duplicates.begin(), duplicates.end());
	return paths;
}
} // namespace Jettison::Vfs
