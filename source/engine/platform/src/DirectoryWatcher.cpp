#include <jettison/platform/DirectoryWatcher.h>

#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace Jettison::Platform
{
namespace
{
namespace fs = std::filesystem;

constexpr std::uint32_t kEvents =
    IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ONLYDIR;

// Any size holding one event with the longest name (NAME_MAX) works; this one takes a burst of them,
// a tree's worth of saves, in one read.
constexpr std::size_t kReadBufferSize = std::size_t{64} * 1024;


std::runtime_error SystemError(std::string_view what, const fs::path &path)
{
	return std::runtime_error{std::format(
	    "Platform: cannot {} {}: {}", what, path.string(), std::generic_category().message(errno))};
}


// Whether path is directory or below it.
bool IsWithin(const fs::path &path, const fs::path &directory)
{
	const fs::path relative = path.lexically_relative(directory);
	return !relative.empty() && *relative.begin() != "..";
}
} // namespace


struct DirectoryWatcher::State
{
	int descriptor{-1};
	fs::path root;
	std::unordered_map<int, fs::path> directories;


	explicit State(fs::path watched) : root{std::move(watched)}
	{
	}


	State(const State &) = delete;


	State &operator=(const State &) = delete;


	~State()
	{
		if (descriptor >= 0)
			close(descriptor);
	}


	// The directory watched, and each below it, what is in them reported as changed. A directory that
	// has gone by the time it is reached is passed over: its deletion is an event of its own.
	void Watch(const fs::path &directory, std::vector<fs::path> &changed)
	{
		const int watch = inotify_add_watch(descriptor, directory.c_str(), kEvents);
		if (watch < 0)
		{
			if (directory != root && (errno == ENOENT || errno == ENOTDIR))
				return;
			throw SystemError("watch", directory);
		}
		directories[watch] = directory;
		std::error_code error;
		for (fs::directory_iterator entry{directory, error}, end; !error && entry != end;
		     entry.increment(error))
		{
			changed.push_back(entry->path());
			if (entry->is_directory(error))
				Watch(entry->path(), changed);
		}
	}


	void Forget(const fs::path &directory)
	{
		std::erase_if(
		    directories,
		    [&](const auto &watched)
		    {
			    if (!IsWithin(watched.second, directory))
				    return false;
			    inotify_rm_watch(descriptor, watched.first);
			    return true;
		    });
	}


	void Take(const inotify_event &event, std::string_view name, std::vector<fs::path> &changed)
	{
		if ((event.mask & IN_Q_OVERFLOW) != 0)
		{
			changed.push_back(root);
			Watch(root, changed);
			return;
		}
		const auto found = directories.find(event.wd);
		if (found == directories.end())
			return;
		if ((event.mask & IN_IGNORED) != 0)
		{
			directories.erase(found);
			return;
		}
		if (name.empty())
			return;
		const fs::path path = found->second / name;
		changed.push_back(path);
		if ((event.mask & IN_ISDIR) == 0)
			return;
		if ((event.mask & IN_MOVED_FROM) != 0)
			Forget(path);
		else if ((event.mask & (IN_CREATE | IN_MOVED_TO)) != 0)
			Watch(path, changed);
	}
};


DirectoryWatcher::DirectoryWatcher(const std::filesystem::path &root)
    : m_pState{std::make_unique<State>(root)}
{
	if (!fs::is_directory(root))
		throw std::runtime_error{
		    std::format("Platform: cannot watch {}: not a directory", root.string())};
	m_pState->descriptor = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (m_pState->descriptor < 0)
		throw SystemError("watch", root);
	std::vector<fs::path> existing;
	m_pState->Watch(root, existing);
}


DirectoryWatcher::~DirectoryWatcher() = default;


std::vector<std::filesystem::path> DirectoryWatcher::Wait(std::chrono::milliseconds timeout)
{
	State &state = *m_pState;
	std::vector<fs::path> changed;
	pollfd ready{.fd = state.descriptor, .events = POLLIN, .revents = 0};
	const int count = poll(&ready, 1, static_cast<int>(timeout.count()));
	if (count < 0 && errno != EINTR)
		throw SystemError("wait on", state.root);
	if (count <= 0)
		return changed;

	alignas(inotify_event) std::array<char, kReadBufferSize> buffer{};
	for (;;)
	{
		const ssize_t length = read(state.descriptor, buffer.data(), buffer.size());
		if (length < 0)
		{
			if (errno == EAGAIN || errno == EINTR)
				return changed;
			throw SystemError("read the changes to", state.root);
		}
		for (std::size_t offset = 0; offset < static_cast<std::size_t>(length);)
		{
			inotify_event event{};
			std::memcpy(&event, buffer.data() + offset, sizeof event);
			const char *pName = buffer.data() + offset + sizeof event;
			state.Take(event, {pName, strnlen(pName, event.len)}, changed);
			offset += sizeof event + event.len;
		}
	}
}
} // namespace Jettison::Platform
