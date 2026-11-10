#pragma once

#include <string>

#include <jettison/resource/ResourceHandle.h>

namespace Jettison::Jobs
{
class IoWorker;
}
namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Resource
{
// Loads resources by virtual path through the VFS, on the I/O worker, reference-counted, with
// hot reload in development builds. A loader per resource type is registered by the module that
// owns the type (models by render, documents by ui), so this module depends on none of them.
// Filled in phase 3.
class ResourceManager
{
  public:
	ResourceManager(Vfs::FileSystem &fileSystem, Jobs::IoWorker &ioWorker);

	// Returns at once with a handle; the resource becomes available after a later Update().
	template <typename T> ResourceHandle<T> Load(const std::string &virtualPath);
	template <typename T> const T *TryGet(ResourceHandle<T> handle) const;
	template <typename T> void Release(ResourceHandle<T> handle);

	// Finishes loads the worker completed and, in development builds, reloads changed files.
	void Update();

  private:
	[[maybe_unused]] Vfs::FileSystem &m_fileSystem;
	[[maybe_unused]] Jobs::IoWorker &m_ioWorker;
};
} // namespace Jettison::Resource
