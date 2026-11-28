#pragma once

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>

#include <jettison/core/Log.h>
#include <jettison/vfs/FileSystem.h>

// What the fuzz targets share (docs/build-and-gates.md, "Fuzzing"). Each target is one format's
// loading path, the engine's own, given libFuzzer's input as a file of that format: it either refuses
// the input, by the exception its contract names, or reads it, and the sanitisers report anything
// else. Any other exception escapes, and libFuzzer reports it as a crash.
namespace Jettison::Fuzz
{
inline std::span<const std::byte> Bytes(const std::uint8_t *pData, std::size_t size)
{
	return std::as_bytes(std::span{pData, size});
}


// Whether the call refused its input by the exception its contract names, a Refusal; any other
// escapes to libFuzzer, which reports it.
template <typename Refusal, typename Call> bool Refuses(Call &&call)
{
	try
	{
		call();
		return false;
	}
	catch (const Refusal &)
	{
		return true;
	}
}


// Logging down to the reports of failed assertions: a refusal is the expected outcome for most
// inputs, and a line for each would be most of a run's time. From LLVMFuzzerInitialize.
inline void Quiet()
{
	Core::InitLogging(Core::Severity::Fatal);
}

// A directory of the process's own, mounted, for a loader that reads through the file system: an
// input is written into it as the file the loader reads. Removed when the process ends.
class Scratch
{
  public:
	explicit Scratch(const std::string &name)
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-fuzz-" + name + "-" + std::to_string(getpid()))}
	{
		std::filesystem::remove_all(m_directory);
		std::filesystem::create_directories(m_directory);
		m_files.AddMount({"scratch", m_directory, false});
	}


	~Scratch()
	{
		std::error_code error;
		std::filesystem::remove_all(m_directory, error);
	}


	Scratch(const Scratch &) = delete;


	Scratch &operator=(const Scratch &) = delete;


	// Writes the bytes as the file at the virtual path, replacing what was there.
	std::filesystem::path Put(const std::string &path, std::span<const std::byte> bytes) const
	{
		const std::filesystem::path file = m_directory / path;
		std::filesystem::create_directories(file.parent_path());
		std::ofstream out{file, std::ios::binary | std::ios::trunc};
		out.write(
		    reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return file;
	}


	void Remove(const std::string &path) const
	{
		std::error_code error;
		std::filesystem::remove(m_directory / path, error);
	}


	const std::filesystem::path &Directory() const
	{
		return m_directory;
	}


	// The scratch mount alone; a target that needs another mounts its own file system.
	const Vfs::FileSystem &Files() const
	{
		return m_files;
	}

  private:
	std::filesystem::path m_directory;
	Vfs::FileSystem m_files;
};
} // namespace Jettison::Fuzz
