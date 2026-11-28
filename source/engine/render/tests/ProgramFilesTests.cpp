#include "vulkan/ProgramFiles.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

using namespace Jettison;
using namespace Jettison::Render;

namespace
{
// A SPIR-V header and one entry point, "main", for the execution model (0 vertex, 4 fragment),
// padded with OpNops so two versions of a file differ in size as well as time.
std::vector<std::uint32_t> Module(std::uint32_t executionModel, int nops = 0)
{
	std::vector<std::uint32_t> words{0x07230203, 0x00010000, 0, 2, 0};
	words.insert(words.end(), {(5u << 16) | 15u, executionModel, 1, 0x6e69616d, 0});
	words.insert(words.end(), static_cast<std::size_t>(nops), 1u << 16);
	return words;
}


struct Scratch
{
	Scratch()
	{
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root / "base" / "shaders");
		std::filesystem::create_directories(root / "mod" / "shaders");
		files.AddMount({"base", root / "base", false});
		files.AddMount({"mod", root / "mod", false});
	}


	~Scratch()
	{
		std::filesystem::remove_all(root);
	}


	Scratch(const Scratch &) = delete;


	Scratch &operator=(const Scratch &) = delete;


	void Put(const std::string &mount, const std::string &name, const std::vector<std::uint32_t> &words)
	{
		std::ofstream file{root / mount / "shaders" / name, std::ios::binary | std::ios::trunc};
		file.write(
		    reinterpret_cast<const char *>(words.data()),
		    static_cast<std::streamsize>(words.size() * sizeof(std::uint32_t)));
	}


	const std::filesystem::path root = std::filesystem::temp_directory_path() /
	                                   ("jettison-program-files-" + std::to_string(::getpid()));
	Vfs::FileSystem files;
};


std::string ErrorOf(ProgramFiles &programs, const std::string &program, ShaderStage stage)
{
	try
	{
		programs.Read(program, stage);
	}
	catch (const std::runtime_error &error)
	{
		return error.what();
	}
	return {};
}
} // namespace


TEST_CASE("Program files: a changed file names its program alone, once")
{
	Scratch scratch;
	scratch.Put("base", "lamp.vert.spv", Module(0));
	scratch.Put("base", "lamp.frag.spv", Module(4));
	scratch.Put("base", "hull.vert.spv", Module(0));
	scratch.Put("base", "hull.frag.spv", Module(4));
	ProgramFiles programs{scratch.files};
	CHECK(programs.Read("lamp", ShaderStage::Vertex).size() == 40);
	programs.Read("lamp", ShaderStage::Fragment);
	programs.Read("hull", ShaderStage::Vertex);
	programs.Read("hull", ShaderStage::Fragment);
	CHECK(programs.TakeChanged().empty());

	scratch.Put("base", "lamp.frag.spv", Module(4, 1));
	const auto changed = programs.TakeChanged();
	REQUIRE(changed.size() == 1);
	CHECK(changed.at("lamp") == "shaders/lamp.frag.spv");
	CHECK(programs.TakeChanged().empty());

	// A mod's file over the base's is a change, and so is its going.
	scratch.Put("mod", "hull.vert.spv", Module(0));
	CHECK(programs.TakeChanged().count("hull") == 1);
	std::filesystem::remove(scratch.root / "mod" / "shaders" / "hull.vert.spv");
	CHECK(programs.TakeChanged().count("hull") == 1);
	CHECK(programs.TakeChanged().empty());
}


TEST_CASE("Program files: a file that is not SPIR-V for its stage is refused, naming it, once")
{
	Scratch scratch;
	scratch.Put("base", "lamp.vert.spv", Module(0));
	scratch.Put("base", "lamp.frag.spv", Module(4));
	ProgramFiles programs{scratch.files};
	programs.Read("lamp", ShaderStage::Fragment);

	CHECK(
	    ErrorOf(programs, "lamp", ShaderStage::Compute)
	        .find("failed to open file: shaders/lamp.comp.spv") != std::string::npos);
	CHECK(ErrorOf(programs, "lamp", ShaderStage::Vertex).empty());

	// A fragment stage where the vertex stage should be has no entry point for it.
	scratch.Put("base", "lamp.vert.spv", Module(4, 1));
	CHECK(
	    ErrorOf(programs, "lamp", ShaderStage::Vertex) ==
	    "shader module 'shaders/lamp.vert.spv' has no entry point 'main' for its stage");

	// Not SPIR-V at all, and SPIR-V cut short in an instruction.
	scratch.Put("base", "lamp.frag.spv", {1, 2, 3, 4, 5, 6});
	CHECK(
	    ErrorOf(programs, "lamp", ShaderStage::Fragment) ==
	    "shader module 'shaders/lamp.frag.spv' is not SPIR-V");
	std::vector<std::uint32_t> cut = Module(4);
	cut.pop_back();
	scratch.Put("base", "lamp.frag.spv", cut);
	CHECK(ErrorOf(programs, "lamp", ShaderStage::Fragment).find("is not SPIR-V") != std::string::npos);

	// What was refused is what the next look compares with, so it is not tried again until it
	// changes, and a fixed file is.
	CHECK(programs.TakeChanged().empty());
	scratch.Put("base", "lamp.frag.spv", Module(4, 2));
	CHECK(programs.TakeChanged().count("lamp") == 1);
	CHECK(ErrorOf(programs, "lamp", ShaderStage::Fragment).empty());
}
