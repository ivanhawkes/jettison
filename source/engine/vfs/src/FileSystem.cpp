#include <jettison/vfs/FileSystem.h>

#include <jettison/core/Assert.h>

#include <utility>

namespace Jettison::Vfs
{
void FileSystem::AddMount(Mount mount)
{
	m_mounts.push_back(std::move(mount));
}
bool FileSystem::Exists(const std::string & /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<std::vector<std::byte>> FileSystem::Read(const std::string & /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
void FileSystem::Write(const std::string & /*unused*/, std::span<const std::byte> /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
std::vector<std::string> FileSystem::Enumerate(const std::string & /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<std::filesystem::path> FileSystem::Resolve(const std::string & /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Vfs
