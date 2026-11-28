#include <jettison/script/ScriptDefinition.h>
#include <jettison/script/ScriptHost.h>

#include <cstddef>
#include <cstdint>

#include "Fuzz.h"

using namespace Jettison;


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A mod's script module loaded by a host of its own, as a session's scripts phase loads one:
// checked, loaded by WAMR, its imports and exports checked and its instances made.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	static const Fuzz::Scratch scratch{"script"};
	scratch.Put("fuzz.wasm", Fuzz::Bytes(pData, size));
	Script::ScriptDefinition definition;
	definition.module = "fuzz.wasm";
	definition.capabilities = {"log"};
	Script::ScriptHost host{scratch.Files()};
	Fuzz::Refuses<Script::ScriptError>([&] { host.Load("fuzz", definition); });
	return 0;
}
