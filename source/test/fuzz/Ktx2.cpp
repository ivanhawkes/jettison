#include <jettison/render/Texture.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "Fuzz.h"

using namespace Jettison;


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A KTX 2 texture decoded for either target a device is given, as a texture's loader does.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	for (const Render::TranscodeTarget target :
	     {Render::TranscodeTarget::Bc7, Render::TranscodeTarget::Rgba8})
	{
		try
		{
			Render::DecodeTexture(Fuzz::Bytes(pData, size), "fuzz.ktx2", target);
		}
		catch (const std::runtime_error &)
		{
			return 0;
		}
	}
	return 0;
}
