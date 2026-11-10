#include <jettison/resource/ResourceManager.h>

#include <jettison/core/Assert.h>

namespace Jettison::Resource
{
ResourceManager::ResourceManager(Vfs::FileSystem &fileSystem, Jobs::IoWorker &ioWorker)
    : m_fileSystem{fileSystem}, m_ioWorker{ioWorker}
{
}
void ResourceManager::Update()
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Resource
