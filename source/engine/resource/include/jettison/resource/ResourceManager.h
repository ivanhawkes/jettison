#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>

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
// How a resource type is made from a file's bytes. Registered by the module that owns the type
// (models by render, documents by ui), so this module depends on none of them.
template <typename T> struct Loader
{
	// On the I/O worker: the file's bytes to a value. Throws for data it cannot use; the message
	// becomes the resource's error. Called from one thread at a time, but not the main one, so it
	// touches nothing the main thread owns.
	std::function<std::unique_ptr<T>(std::span<const std::byte> bytes, std::string_view virtualPath)>
	    decode;
	// On the main thread, optional: what has to happen there before the value is visible, such as
	// a GPU upload. Throws like decode.
	std::function<void(T &value, std::string_view virtualPath)> finish;
};

enum class Status : std::uint8_t
{
	// A null or stale handle.
	Invalid,
	Loading,
	Ready,
	// The file is missing, or decode or finish threw: ErrorOf says which. A hot reload retries it.
	Failed,
};

struct ResourceOptions
{
	// Development builds (no NDEBUG) reload a resource whose file changed, polled from Update().
#ifdef NDEBUG
	bool hotReload{false};
#else
	bool hotReload{true};
#endif
	std::chrono::milliseconds pollInterval{500};
};

// Loads resources by virtual path through the VFS, on the I/O worker, reference-counted, with hot
// reload. Main thread only, apart from the loaders' decode.
//
// Each Load is paired with one Release. Loading a path already loaded for the same type returns
// the same handle and counts one more reference; the last Release destroys the value and frees
// the slot, which makes every handle to it stale. A handle is a plain value: copying one does not
// count a reference.
//
// The file system and the worker outlive the manager. Work in flight when the manager goes still
// reads its file, and its result is dropped.
class ResourceManager
{
  public:
	ResourceManager(
	    const Vfs::FileSystem &fileSystem, Jobs::IoWorker &ioWorker, ResourceOptions options = {});
	~ResourceManager();
	ResourceManager(const ResourceManager &) = delete;
	ResourceManager &operator=(const ResourceManager &) = delete;

	// Throws std::logic_error for a loader with no decode, or a type registered twice.
	template <typename T> void RegisterLoader(Loader<T> loader)
	{
		// Checked here: the wrapper below is never empty, whatever it wraps.
		if (!loader.decode)
			ThrowNoDecode(typeid(T));
		RegisterRaw(
		    typeid(T),
		    {[decode = std::move(loader.decode)](
		         std::span<const std::byte> bytes, std::string_view path) -> std::shared_ptr<void>
		     { return std::shared_ptr<T>{decode(bytes, path)}; },
		     loader.finish
		         ? FinishRaw{[finish = std::move(loader.finish)](void *value, std::string_view path)
		                     { finish(*static_cast<T *>(value), path); }}
		         : FinishRaw{}});
	}

	// Returns at once with a handle; the resource becomes available after a later Update().
	// Throws std::logic_error for a type with no loader and std::invalid_argument for a path that
	// is not a valid virtual path.
	template <typename T> ResourceHandle<T> Load(std::string_view virtualPath)
	{
		const RawHandle raw = LoadRaw(typeid(T), virtualPath);
		return {raw.index, raw.generation};
	}

	// The value, or null while it loads, after it failed, or for a stale handle. The pointer is
	// valid until the next Update(), which may swap in a reloaded value, or Release().
	template <typename T> const T *TryGet(ResourceHandle<T> handle) const
	{
		return static_cast<const T *>(TryGetRaw(typeid(T), Raw(handle)));
	}
	template <typename T> Status StatusOf(ResourceHandle<T> handle) const
	{
		return StatusRaw(typeid(T), Raw(handle));
	}
	// Why the resource failed, or why its last reload did; empty if it has not failed.
	template <typename T> std::string_view ErrorOf(ResourceHandle<T> handle) const
	{
		return ErrorRaw(typeid(T), Raw(handle));
	}
	// Starts at 1 when the resource is first ready and goes up with each reload, so a cache built
	// from the value knows to rebuild. 0 before it is ready and for a stale handle.
	template <typename T> std::uint32_t RevisionOf(ResourceHandle<T> handle) const
	{
		return RevisionRaw(typeid(T), Raw(handle));
	}

	// Throws std::logic_error for a stale handle: a second Release of the last reference.
	template <typename T> void Release(ResourceHandle<T> handle)
	{
		ReleaseRaw(typeid(T), Raw(handle));
	}

	// Drains the worker, finishing the loads it completed, and with hot reload on checks for
	// changed files once per poll interval.
	void Update();
	// Starts a reload of every resource whose file changed, was added or was removed since it was
	// read: a mod that shadows a base file counts. Update calls it; tests call it directly.
	void CheckForChanges();

  private:
	using RawHandle = Core::Handle<void>;
	using DecodeRaw =
	    std::function<std::shared_ptr<void>(std::span<const std::byte>, std::string_view)>;
	using FinishRaw = std::function<void(void *, std::string_view)>;
	struct RawLoader
	{
		DecodeRaw decode;
		FinishRaw finish;
	};

	template <typename T> static RawHandle Raw(ResourceHandle<T> handle)
	{
		return {handle.index, handle.generation};
	}

	[[noreturn]] static void ThrowNoDecode(std::type_index type);
	void RegisterRaw(std::type_index type, RawLoader loader);
	RawHandle LoadRaw(std::type_index type, std::string_view virtualPath);
	const void *TryGetRaw(std::type_index type, RawHandle handle) const;
	Status StatusRaw(std::type_index type, RawHandle handle) const;
	std::string_view ErrorRaw(std::type_index type, RawHandle handle) const;
	std::uint32_t RevisionRaw(std::type_index type, RawHandle handle) const;
	void ReleaseRaw(std::type_index type, RawHandle handle);

	struct State;
	// Shared so a completion can tell, through a weak pointer, that the manager has gone.
	std::shared_ptr<State> m_pState;
};
} // namespace Jettison::Resource
