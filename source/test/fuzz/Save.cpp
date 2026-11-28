#include <jettison/serialise/Archive.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/SimWorld.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "Fuzz.h"
#include "World.h"

using namespace Jettison;


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A save game as the shell loads one: its info read for the list of saves, then the whole save, and
// the world inside it loaded into a world of the shell's composition, physics and all.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	const auto bytes = Fuzz::Bytes(pData, size);
	Fuzz::Refuses<std::runtime_error>([&] { Sim::SaveGame::ReadInfo(bytes); });
	Sim::SaveGame save;
	if (Fuzz::Refuses<std::runtime_error>([&] { save = Sim::SaveGame::Read(bytes); }))
		return 0;
	Sim::SimWorld world{Fuzz::kStep, Fuzz::kSeed};
	Fuzz::Compose(world);
	Fuzz::Refuses<std::runtime_error>([&] { world.Load(save.world); });
	return 0;
}
