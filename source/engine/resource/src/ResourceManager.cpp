#include <jettison/resource/ResourceManager.h>

#include <jettison/core/Log.h>
#include <jettison/jobs/IoWorker.h>
#include <jettison/vfs/FileSystem.h>

#include <exception>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Jettison::Resource
{
namespace
{
constexpr Core::Category kResource{"resource"};

// What a file was when it was read: where the mounts resolved it, when it was written, and its
// size. A change in any of them is a change worth a reload; an empty path is a file that was in
// no mount.
struct Stamp
{
	std::filesystem::path path;
	std::filesystem::file_time_type time{};
	std::uintmax_t size{0};

	bool operator==(const Stamp &) const = default;
};

Stamp StampOf(const Vfs::FileSystem &fileSystem, const std::string &virtualPath)
{
	Stamp stamp;
	if (std::optional<std::filesystem::path> path = fileSystem.Resolve(virtualPath))
	{
		std::error_code error;
		stamp.time = std::filesystem::last_write_time(*path, error);
		stamp.size = std::filesystem::file_size(*path, error);
		stamp.path = std::move(*path);
	}
	return stamp;
}

std::uint32_t NextGeneration(std::uint32_t generation)
{
	// 0 is the null handle's, so a slot reused 2^32 times skips it.
	return generation == std::numeric_limits<std::uint32_t>::max() ? 1 : generation + 1;
}
} // namespace

struct ResourceManager::State : std::enable_shared_from_this<State>
{
	struct Slot
	{
		std::string path;
		std::uint32_t generation{1};
		std::uint32_t references{0};
		Status status{Status::Invalid};
		std::shared_ptr<void> value;
		std::string error;
		std::uint32_t revision{0};
		Stamp stamp;
		// The latest load or reload started for the slot. A result carrying another ticket was
		// overtaken, or the slot was released while it was in flight, and is dropped.
		std::uint64_t ticket{0};
		bool inFlight{false};
	};

	struct Pool
	{
		std::shared_ptr<const RawLoader> loader;
		std::vector<Slot> slots;
		std::vector<std::uint32_t> free;
		std::unordered_map<std::string, std::uint32_t> byPath;
	};

	// Written on the worker, read by the completion on the main thread; IoWorker orders the two.
	struct Result
	{
		std::shared_ptr<void> value;
		std::string error;
		Stamp stamp;
	};

	State(const Vfs::FileSystem &files, Jobs::IoWorker &io, ResourceOptions settings)
	    : fileSystem{files}, worker{io}, options{settings}
	{
	}

	const Vfs::FileSystem &fileSystem;
	Jobs::IoWorker &worker;
	ResourceOptions options;
	std::unordered_map<std::type_index, Pool> pools;
	std::uint64_t nextTicket{1};
	std::chrono::steady_clock::time_point lastPoll{std::chrono::steady_clock::now()};

	Pool &PoolFor(std::type_index type)
	{
		const auto found = pools.find(type);
		if (found == pools.end())
			throw std::logic_error{
			    std::format("Resource: no loader is registered for '{}'", type.name())};
		return found->second;
	}

	const Slot *Find(std::type_index type, RawHandle handle) const
	{
		const auto pool = pools.find(type);
		if (handle.IsNull() || pool == pools.end() || handle.index >= pool->second.slots.size())
			return nullptr;
		const Slot &slot = pool->second.slots[handle.index];
		return (slot.generation == handle.generation && slot.references > 0) ? &slot : nullptr;
	}

	void Start(std::type_index type, Pool &pool, std::uint32_t index)
	{
		Slot &slot = pool.slots[index];
		slot.inFlight = true;
		slot.ticket = nextTicket++;
		auto result = std::make_shared<Result>();
		worker.Enqueue(
		    [&files = fileSystem, path = slot.path, loader = pool.loader, result]
		    {
			    // Stamped before the read, so a write that lands during it shows as a change at
			    // the next poll rather than being missed.
			    result->stamp = StampOf(files, path);
			    try
			    {
				    const std::optional<std::vector<std::byte>> bytes = files.Read(path);
				    if (!bytes)
				    {
					    result->error = std::format("'{}' is not in any mount", path);
					    return;
				    }
				    result->value = loader->decode(*bytes, path);
				    if (!result->value)
					    result->error = std::format("'{}' decoded to nothing", path);
			    }
			    catch (const std::exception &error)
			    {
				    result->error = error.what();
			    }
		    },
		    [self = weak_from_this(), type, index, generation = slot.generation, ticket = slot.ticket,
		     result]
		    {
			    if (const std::shared_ptr<State> state = self.lock())
				    state->Finish(type, index, generation, ticket, std::move(*result));
		    });
	}

	void Finish(
	    std::type_index type, std::uint32_t index, std::uint32_t generation, std::uint64_t ticket,
	    Result result)
	{
		Pool &pool = pools.at(type);
		Slot &slot = pool.slots[index];
		if (slot.generation != generation || slot.ticket != ticket || slot.references == 0)
			return;
		slot.inFlight = false;
		slot.stamp = std::move(result.stamp);

		if (result.error.empty() && pool.loader->finish)
		{
			try
			{
				pool.loader->finish(result.value.get(), slot.path);
			}
			catch (const std::exception &error)
			{
				result.error = error.what();
			}
		}

		if (result.error.empty())
		{
			slot.value = std::move(result.value);
			slot.status = Status::Ready;
			slot.error.clear();
			++slot.revision;
			return;
		}
		// A reload that fails keeps the value it would have replaced: in development a half-saved
		// file should not take the resource away.
		const bool kept = slot.value != nullptr;
		Core::Log(
		    Core::Severity::Warning, kResource, "'{}' {}: {}", slot.path,
		    kept ? "did not reload, and keeps its last value" : "did not load", result.error);
		slot.error = std::move(result.error);
		if (!kept)
			slot.status = Status::Failed;
	}
};

ResourceManager::ResourceManager(
    const Vfs::FileSystem &fileSystem, Jobs::IoWorker &ioWorker, ResourceOptions options)
    : m_pState{std::make_shared<State>(fileSystem, ioWorker, options)}
{
}

// Completions still queued on the worker hold a weak pointer to the state, which expires here.
ResourceManager::~ResourceManager() = default;

void ResourceManager::ThrowNoDecode(std::type_index type)
{
	throw std::logic_error{std::format("Resource: the loader for '{}' has no decode", type.name())};
}

void ResourceManager::RegisterRaw(std::type_index type, RawLoader loader)
{
	const auto [pool, added] = m_pState->pools.try_emplace(type);
	if (!added)
		throw std::logic_error{
		    std::format("Resource: a loader for '{}' is already registered", type.name())};
	pool->second.loader = std::make_shared<const RawLoader>(std::move(loader));
}

ResourceManager::RawHandle ResourceManager::LoadRaw(std::type_index type, std::string_view virtualPath)
{
	State::Pool &pool = m_pState->PoolFor(type);
	if (!Vfs::FileSystem::IsValidPath(virtualPath))
		throw std::invalid_argument{
		    std::format("Resource: '{}' is not a valid virtual path", virtualPath)};

	std::string path{virtualPath};
	if (const auto found = pool.byPath.find(path); found != pool.byPath.end())
	{
		State::Slot &slot = pool.slots[found->second];
		++slot.references;
		return {found->second, slot.generation};
	}

	std::uint32_t index = 0;
	if (pool.free.empty())
	{
		index = static_cast<std::uint32_t>(pool.slots.size());
		pool.slots.emplace_back();
	}
	else
	{
		index = pool.free.back();
		pool.free.pop_back();
	}
	State::Slot &slot = pool.slots[index];
	slot.path = path;
	slot.references = 1;
	slot.status = Status::Loading;
	pool.byPath.emplace(std::move(path), index);
	m_pState->Start(type, pool, index);
	return {index, slot.generation};
}

const void *ResourceManager::TryGetRaw(std::type_index type, RawHandle handle) const
{
	const State::Slot *slot = m_pState->Find(type, handle);
	return slot ? slot->value.get() : nullptr;
}

Status ResourceManager::StatusRaw(std::type_index type, RawHandle handle) const
{
	const State::Slot *slot = m_pState->Find(type, handle);
	return slot ? slot->status : Status::Invalid;
}

std::string_view ResourceManager::ErrorRaw(std::type_index type, RawHandle handle) const
{
	const State::Slot *slot = m_pState->Find(type, handle);
	return slot ? std::string_view{slot->error} : std::string_view{};
}

std::uint32_t ResourceManager::RevisionRaw(std::type_index type, RawHandle handle) const
{
	const State::Slot *slot = m_pState->Find(type, handle);
	return slot ? slot->revision : 0;
}

void ResourceManager::ReleaseRaw(std::type_index type, RawHandle handle)
{
	if (!m_pState->Find(type, handle))
		throw std::logic_error{std::format(
		    "Resource: released a stale handle ({}, generation {}) to a '{}'", handle.index,
		    handle.generation, type.name())};
	State::Pool &pool = m_pState->pools.at(type);
	State::Slot &slot = pool.slots[handle.index];
	if (--slot.references > 0)
		return;

	pool.byPath.erase(slot.path);
	// A load still in flight finds the generation changed and drops its result.
	State::Slot freed;
	freed.generation = NextGeneration(slot.generation);
	slot = std::move(freed);
	pool.free.push_back(handle.index);
}

void ResourceManager::Update()
{
	m_pState->worker.Drain();
	if (!m_pState->options.hotReload)
		return;
	const auto now = std::chrono::steady_clock::now();
	if (now - m_pState->lastPoll < m_pState->options.pollInterval)
		return;
	m_pState->lastPoll = now;
	CheckForChanges();
}

void ResourceManager::CheckForChanges()
{
	for (auto &[type, pool] : m_pState->pools)
	{
		for (std::uint32_t index = 0; index < pool.slots.size(); ++index)
		{
			const State::Slot &slot = pool.slots[index];
			if (slot.references > 0 && !slot.inFlight &&
			    StampOf(m_pState->fileSystem, slot.path) != slot.stamp)
				m_pState->Start(type, pool, index);
		}
	}
}
} // namespace Jettison::Resource
