#include <jettison/animation/ClipFile.h>
#include <jettison/serialise/Archive.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "Fuzz.h"

using namespace Jettison;


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A clip file's YAML read and checked, and each track's node looked up by its name.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	Animation::ClipFile clips;
	if (Fuzz::Refuses<Serialise::FormatError>(
	        [&]
	        {
		        clips = Animation::ReadClipFile(
		            {reinterpret_cast<const char *>(pData), size}, "animations/fuzz.clip.yaml");
	        }))
		return 0;
	for (const std::string &node : clips.nodes)
		Animation::NodeIndex(clips, node);
	return 0;
}
