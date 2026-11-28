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


// An image stb_image reads (PNG, JPEG, BMP, TGA and the rest), decoded with its mip chain, as a
// development texture's loader and a UI image's do.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	Fuzz::Refuses<std::runtime_error>(
	    [&]
	    {
		    Render::DecodeTexture(Fuzz::Bytes(pData, size), "fuzz.png", Render::TranscodeTarget::Rgba8);
	    });
	return 0;
}
