#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <jettison/vfs/FileSystem.h>

namespace Jettison::Render
{
enum class ShaderStage
{
	Vertex,
	Fragment,
	Compute,
};

// shaders/<program>.<stage>.spv, the stage's file of a program in the asset tree.
std::string ProgramFilePath(const std::string &program, ShaderStage stage);

// The SPIR-V a program's pipelines are built from, read through the asset tree, and each file as it
// was when it was read, so a file changed on the disk names the program to build again
// (PipelineCache::ReloadChanged). Main thread only.
class ProgramFiles
{
  public:
	explicit ProgramFiles(const Vfs::FileSystem &assets);

	// The stage's file (ProgramFilePath), checked to be SPIR-V with an entry point "main" for the
	// stage. Throws naming the file when no mount has it or it is not; either way the file is recorded
	// as it was, so a broken one is not tried again until it changes.
	std::vector<std::byte> Read(const std::string &program, ShaderStage stage);

	// The programs one of whose files has changed since it was read, or that another mount's file
	// now shadows or no longer does, by name, each with a file that changed. A change is given once
	// it has settled: the call before saw the file as it is now, so a file being written (glslc
	// writes in place) is never read half done, and one that keeps changing waits. Each change is
	// given once: what the files are now is what the next call compares with. Called once a poll.
	std::map<std::string, std::string> TakeChanged();

  private:
	struct File
	{
		std::string program;
		std::optional<Vfs::FileInfo> info;

		// What the last call saw where the file differed from info, waiting for a call to see it
		// again unchanged.
		bool changing{false};
		std::optional<Vfs::FileInfo> seen;
	};

	const Vfs::FileSystem &m_assets;
	std::map<std::string, File> m_files;
};
} // namespace Jettison::Render
