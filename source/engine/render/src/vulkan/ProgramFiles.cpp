#include "vulkan/ProgramFiles.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Jettison::Render
{
namespace
{
// SPIR-V's own numbers (the SPIR-V specification, "Physical Layout" and "OpEntryPoint").
constexpr std::uint32_t kSpirvMagic = 0x07230203;
constexpr std::size_t kSpirvHeaderWords = 5;
constexpr std::uint32_t kOpEntryPoint = 15;
constexpr std::uint32_t kOpcodeMask = 0xffff;
constexpr unsigned kWordCountShift = 16;
constexpr std::uint32_t kExecutionModelVertex = 0;
constexpr std::uint32_t kExecutionModelFragment = 4;
constexpr std::uint32_t kExecutionModelGlCompute = 5;

// An entry point's operands before its name: the execution model and the function's id.
constexpr std::size_t kEntryPointNameWord = 3;

// The name the pipelines give every stage (PipelineCache's pName), with its terminator.
constexpr std::string_view kEntryPoint{"main", 5};


std::uint32_t ExecutionModel(ShaderStage stage)
{
	switch (stage)
	{
		case ShaderStage::Vertex: return kExecutionModelVertex;
		case ShaderStage::Fragment: return kExecutionModelFragment;
		case ShaderStage::Compute: return kExecutionModelGlCompute;
	}
	return kExecutionModelVertex;
}


// Walks every instruction, so a file cut short in an instruction is refused before a driver reads
// past its end; a driver is given only a module whose words are whole and which has the stage's
// entry point. More than that is spirv-val's, which the engine does not carry.
void CheckSpirv(std::span<const std::byte> code, ShaderStage stage, const std::string &path)
{
	const std::size_t words = code.size() / sizeof(std::uint32_t);
	const auto word = [&](std::size_t index)
	{
		std::uint32_t value = 0;
		std::memcpy(&value, code.data() + index * sizeof(std::uint32_t), sizeof(value));
		return value;
	};
	if (code.size() % sizeof(std::uint32_t) != 0 || words < kSpirvHeaderWords || word(0) != kSpirvMagic)
		throw std::runtime_error("shader module '" + path + "' is not SPIR-V");
	bool found = false;
	for (std::size_t index = kSpirvHeaderWords; index < words;)
	{
		const std::uint32_t count = word(index) >> kWordCountShift;
		if (count == 0 || index + count > words)
			throw std::runtime_error(
			    "shader module '" + path + "' is not SPIR-V: an instruction is cut short");
		const std::size_t nameBytes =
		    count > kEntryPointNameWord ? (count - kEntryPointNameWord) * sizeof(std::uint32_t) : 0;
		if ((word(index) & kOpcodeMask) == kOpEntryPoint && nameBytes >= kEntryPoint.size() &&
		    word(index + 1) == ExecutionModel(stage) &&
		    std::memcmp(
		        code.data() + (index + kEntryPointNameWord) * sizeof(std::uint32_t), kEntryPoint.data(),
		        kEntryPoint.size()) == 0)
			found = true;
		index += count;
	}
	if (!found)
		throw std::runtime_error(
		    "shader module '" + path + "' has no entry point 'main' for its stage");
}
} // namespace


std::string ProgramFilePath(const std::string &program, ShaderStage stage)
{
	switch (stage)
	{
		case ShaderStage::Vertex: return "shaders/" + program + ".vert.spv";
		case ShaderStage::Fragment: return "shaders/" + program + ".frag.spv";
		case ShaderStage::Compute: return "shaders/" + program + ".comp.spv";
	}
	return "shaders/" + program + ".vert.spv";
}


ProgramFiles::ProgramFiles(const Vfs::FileSystem &assets) : m_assets{assets}
{
}


std::vector<std::byte> ProgramFiles::Read(const std::string &program, ShaderStage stage)
{
	const std::string path = ProgramFilePath(program, stage);

	// Recorded before it is read: a file written between the two is newer than what was recorded,
	// and is read again on the next look.
	m_files.insert_or_assign(path, File{program, m_assets.Find(path)});

	// The words "failed to open file" are what the render test's start-up check reads for: a run
	// with no shaders has to stop here, before a frame, and say which file it wanted.
	auto code = m_assets.Read(path);
	if (!code)
		throw std::runtime_error("failed to open file: " + path + " (no asset mount has it)");
	CheckSpirv(*code, stage, path);
	return std::move(*code);
}


std::map<std::string, std::string> ProgramFiles::TakeChanged()
{
	std::map<std::string, std::string> changed;
	for (auto &[path, file] : m_files)
	{
		std::optional<Vfs::FileInfo> info = m_assets.Find(path);
		if (info == file.info)
			continue;
		file.info = std::move(info);
		changed.try_emplace(file.program, path);
	}
	return changed;
}
} // namespace Jettison::Render
