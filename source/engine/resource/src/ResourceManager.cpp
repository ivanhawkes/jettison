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

// What a file was when it was read: the mount it was found in, when it was written, and its size
// (Vfs::FileInfo). A change in any of them is a change worth a reload, so a mod's file appearing
// over the base's counts as one; nothing is a file that was in no mount.
using Stamp = std::optional<Vfs::FileInfo>;


Stamp StampOf(const Vfs::FileSystem &fileSystem, const std::string &virtualPath)
{
	return fileSystem.Find(virtualPath);
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

		// For a type made from another (RawLoader::source): the source's slot, which this one holds a
		// reference to, and whether this one has been made from the source's latest read.
		RawHandle source{};
		bool made{false};
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

	// A slot made from the file being read, by the read's own job.
	struct Dependent
	{
		std::type_index type;
		std::uint32_t index{0};
		std::uint32_t generation{0};
		std::uint64_t ticket{0};
		std::shared_ptr<const RawLoader> loader;

		// Made whatever the read finds; otherwise only if the file changed since the last read, so a
		// read for a type asked for late does not make the others again.
		bool always{false};
		bool skipped{false};
		Result result{};
	};

	struct Read
	{
		Result source;
		std::vector<Dependent> dependents;
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


	// Reads the slot's file and decodes it on the worker, and in the same job makes the slots of the
	// path made from it (State::Dependent).
	void Start(std::type_index type, Pool &pool, std::uint32_t index)
	{
		Slot &slot = pool.slots[index];
		slot.inFlight = true;
		slot.ticket = nextTicket++;
		auto read = std::make_shared<Read>();
		read->dependents = TakeDependents(type, slot.path);
		worker.Enqueue(
		    [&files = fileSystem, path = slot.path, loader = pool.loader, previous = slot.stamp, read]
		    {
			    Result &result = read->source;

			    // Stamped before the read, so a write that lands during it shows as a change at
			    // the next poll rather than being missed.
			    result.stamp = StampOf(files, path);
			    try
			    {
				    const std::optional<std::vector<std::byte>> bytes = files.Read(path);
				    if (!bytes)
				    {
					    result.error = std::format("'{}' is not in any mount", path);
					    return;
				    }
				    result.value = loader->decode(*bytes, path);
				    if (!result.value)
					    result.error = std::format("'{}' decoded to nothing", path);
			    }
			    catch (const std::exception &error)
			    {
				    result.error = error.what();
			    }
			    if (!result.value)
				    return;
			    const bool changed = result.stamp != previous;
			    for (Dependent &dependent : read->dependents)
			    {
				    dependent.skipped = !dependent.always && !changed;
				    if (!dependent.skipped)
					    dependent.result = Derive(*dependent.loader, result.value.get(), path);
			    }
		    },
		    [self = weak_from_this(), type, index, generation = slot.generation, ticket = slot.ticket,
		     read]
		    {
			    if (const std::shared_ptr<State> state = self.lock())
				    state->FinishRead(type, index, generation, ticket, std::move(*read));
		    });
	}


	// Makes a slot from its source's value on the worker, apart from any read of the source.
	void StartDerive(
	    std::type_index type, Pool &pool, std::uint32_t index, std::shared_ptr<void> source)
	{
		Slot &slot = pool.slots[index];
		slot.inFlight = true;
		slot.made = true;
		slot.ticket = nextTicket++;
		auto result = std::make_shared<Result>();
		worker.Enqueue(
		    [loader = pool.loader, source = std::move(source), path = slot.path, result]
		    { *result = Derive(*loader, source.get(), path); },
		    [self = weak_from_this(), type, index, generation = slot.generation, ticket = slot.ticket,
		     result]
		    {
			    if (const std::shared_ptr<State> state = self.lock())
				    state->Finish(type, index, generation, ticket, std::move(*result));
		    });
	}


	static Result Derive(const RawLoader &loader, const void *source, const std::string &path)
	{
		Result result;
		try
		{
			result.value = loader.derive(source, path);
			if (!result.value)
				result.error = std::format("'{}' decoded to nothing", path);
		}
		catch (const std::exception &error)
		{
			result.error = error.what();
		}
		return result;
	}


	template <typename Function>
	void ForEachDependent(std::type_index sourceType, const std::string &path, Function function)
	{
		for (auto &[type, pool] : pools)
		{
			if (pool.loader->source != sourceType)
				continue;
			if (const auto found = pool.byPath.find(path); found != pool.byPath.end())
				function(type, pool, found->second);
		}
	}


	// The slots of the path made from the type, for a read of it about to start, each given a new
	// ticket. One not made from the last read, or still being made from it, is made from this one
	// whatever it finds.
	std::vector<Dependent> TakeDependents(std::type_index sourceType, const std::string &path)
	{
		std::vector<Dependent> dependents;
		ForEachDependent(
		    sourceType, path,
		    [&](std::type_index type, Pool &pool, std::uint32_t index)
		    {
			    Slot &slot = pool.slots[index];
			    const bool always = !slot.made || slot.inFlight;
			    slot.made = true;
			    slot.inFlight = true;
			    slot.ticket = nextTicket++;
			    dependents.push_back({type, index, slot.generation, slot.ticket, pool.loader, always});
		    });
		return dependents;
	}


	// A slot made from another type that no job is making: made from the source's value if that is
	// held, failed with the source's error if it has one, or else the file read again, since the
	// source's value was not kept.
	void Supply(std::type_index type, Pool &pool, std::uint32_t index)
	{
		const Slot &slot = pool.slots[index];
		if (slot.made || slot.inFlight)
			return;
		Pool &sourcePool = pools.at(*pool.loader->source);
		const std::uint32_t sourceIndex = slot.source.index;
		const Slot &source = sourcePool.slots[sourceIndex];
		if (source.inFlight)
			return;
		if (source.value)
			StartDerive(type, pool, index, source.value);
		else if (!source.error.empty())
			Fail(pool.slots[index], source.error);
		else
			Start(*pool.loader->source, sourcePool, sourceIndex);
	}


	// The slot's source failed: it keeps a value it has, as a reload that fails does, and says why.
	static void Fail(Slot &slot, const std::string &error)
	{
		slot.inFlight = false;
		slot.error = error;
		if (slot.revision == 0)
			slot.status = Status::Failed;
	}


	static bool IsCurrent(const Slot &slot, std::uint32_t generation, std::uint64_t ticket)
	{
		return slot.generation == generation && slot.ticket == ticket && slot.references > 0;
	}


	static void Retire(const Pool &pool, Slot &slot) noexcept
	{
		if (slot.value && pool.loader->retire)
			pool.loader->retire(slot.value.get());
	}


	// False for a result that was overtaken, or whose slot was released while it was in flight.
	bool Finish(
	    std::type_index type, std::uint32_t index, std::uint32_t generation, std::uint64_t ticket,
	    Result result)
	{
		Pool &pool = pools.at(type);
		if (!IsCurrent(pool.slots[index], generation, ticket))
			return false;
		pool.slots[index].inFlight = false;
		pool.slots[index].stamp = result.stamp;

		if (result.error.empty() && pool.loader->finish)
		{
			// The path copied and the slot found again after: a finish that loads another resource
			// of this type can grow the pool, which moves its slots.
			const std::string path = pool.slots[index].path;
			try
			{
				pool.loader->finish(result.value.get(), path);
			}
			catch (const std::exception &error)
			{
				result.error = error.what();
			}
		}

		Slot &slot = pool.slots[index];
		if (result.error.empty())
		{
			Retire(pool, slot);
			slot.value = std::move(result.value);
			slot.status = Status::Ready;
			slot.error.clear();
			++slot.revision;
			return true;
		}

		// A reload that fails keeps the value it would have replaced: in development a half-saved
		// file should not take the resource away. A value that is not kept has had a revision.
		const bool kept = slot.revision > 0;
		Core::Log(
		    Core::Severity::Warning, kResource, "'{}' {}: {}", slot.path,
		    kept ? "did not reload, and keeps its last value" : "did not load", result.error);
		slot.error = std::move(result.error);
		if (!kept)
			slot.status = Status::Failed;
		return true;
	}


	void FinishRead(
	    std::type_index type, std::uint32_t index, std::uint32_t generation, std::uint64_t ticket,
	    Read read)
	{
		if (!Finish(type, index, generation, ticket, std::move(read.source)))
			return;

		// Empty when this read made the value.
		const std::string error = pools.at(type).slots[index].error;
		for (Dependent &dependent : read.dependents)
		{
			Slot &slot = pools.at(dependent.type).slots[dependent.index];
			if (!IsCurrent(slot, dependent.generation, dependent.ticket))
				continue;
			if (!error.empty())
				Fail(slot, error);
			else if (dependent.skipped)
				slot.inFlight = false;
			else
				Finish(
				    dependent.type, dependent.index, dependent.generation, dependent.ticket,
				    std::move(dependent.result));
		}

		// Those asked for while the read was in flight, and those ReloadAll marked since it began.
		Pool &pool = pools.at(type);
		const std::string path = pool.slots[index].path;
		ForEachDependent(
		    type, path,
		    [this](std::type_index dependentType, Pool &dependentPool, std::uint32_t dependentIndex)
		    { Supply(dependentType, dependentPool, dependentIndex); });
		if (!pool.loader->keep)
			pool.slots[index].value.reset();
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
	const std::optional<std::type_index> source = pool.loader->source;
	if (source && m_pState->PoolFor(*source).loader->source)
		throw std::logic_error{std::format(
		    "Resource: '{}' is made from '{}', which is made from another type rather than read",
		    type.name(), source->name())};

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
	if (!source)
	{
		m_pState->Start(type, pool, index);
		return {index, slot.generation};
	}

	// A read this starts makes the slot too; otherwise it is supplied now, or when the read in
	// flight lands.
	const RawHandle sourceHandle = LoadRaw(*source, virtualPath);
	pool.slots[index].source = sourceHandle;
	m_pState->Supply(type, pool, index);
	return {index, pool.slots[index].generation};
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
	State::Retire(pool, slot);
	const RawHandle source = slot.source;

	// A load still in flight finds the generation changed and drops its result.
	State::Slot freed;
	freed.generation = NextGeneration(slot.generation);
	slot = std::move(freed);
	pool.free.push_back(handle.index);
	if (pool.loader->source)
		ReleaseRaw(*pool.loader->source, source);
}


std::size_t ResourceManager::Loading() const
{
	std::size_t loading = 0;
	for (const auto &[type, pool] : m_pState->pools)
		for (const State::Slot &slot : pool.slots)
			loading += slot.references > 0 && slot.status == Status::Loading ? 1 : 0;
	return loading;
}


void ResourceManager::Update()
{
	if (m_pState->options.settleLoads)
	{
		// Until nothing is in flight: a finish that asks for another resource is settled here too,
		// however fast the worker decodes it.
		do
		{
			m_pState->worker.WaitIdle();
			m_pState->worker.Drain();
		} while (Loading() != 0);
	}
	else
		m_pState->worker.Drain();
	if (!m_pState->options.hotReload)
		return;
	const auto now = std::chrono::steady_clock::now();
	if (now - m_pState->lastPoll < m_pState->options.pollInterval)
		return;
	m_pState->lastPoll = now;
	CheckForChanges();
}


void ResourceManager::ReloadAll()
{
	// The types made from others first, so the reads below make them again whatever they find.
	for (auto &[type, pool] : m_pState->pools)
		if (pool.loader->source)
			for (State::Slot &slot : pool.slots)
				slot.made = false;
	for (auto &[type, pool] : m_pState->pools)
	{
		if (pool.loader->source)
			continue;
		for (std::uint32_t index = 0; index < pool.slots.size(); ++index)
			if (pool.slots[index].references > 0 && !pool.slots[index].inFlight)
				m_pState->Start(type, pool, index);
	}
}


void ResourceManager::CheckForChanges()
{
	// A pack rewritten on the disk is opened again first, so the stamps below are the new one's.
	m_pState->fileSystem.Refresh();
	for (auto &[type, pool] : m_pState->pools)
	{
		// Made from another type, with no file of its own: its source's reload makes it again.
		if (pool.loader->source)
			continue;
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
