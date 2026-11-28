#include <jettison/vfs/FileSystem.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "Fuzz.h"

using namespace Jettison;


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A pack file mounted as a mod's is, then every file it lists read, which checks each one's bytes.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	static const Fuzz::Scratch scratch{"pack"};
	const std::filesystem::path pack = scratch.Put("fuzz.pack", Fuzz::Bytes(pData, size));
	Vfs::FileSystem files;
	if (Fuzz::Refuses<std::runtime_error>([&] { files.AddMount({"mod", pack, false}); }))
		return 0;
	for (const std::string &path : files.Enumerate(""))
		Fuzz::Refuses<std::runtime_error>([&] { files.Read(path); });
	return 0;
}
