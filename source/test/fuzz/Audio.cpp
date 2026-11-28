#include <jettison/audio/AudioSystem.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Fuzz.h"

using namespace Jettison;

namespace
{
// A tenth of a second of output a play, at the system's 48 kHz, in stereo.
constexpr std::size_t kMixFrames = 4800;
constexpr std::size_t kChannels = 2;
constexpr std::size_t kMagicBytes = 4;
} // namespace


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A sound file played as an effect, decoded whole, and as music, streamed, each mixed a while with no
// device, as the tests mix. Each input is a file of its own name, so no decode is shared with the one
// before, named by its magic number as a mod names its files, so miniaudio tries that decoder first.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	static const Fuzz::Scratch scratch{"audio"};
	static Audio::AudioSystem audio{scratch.Files(), {.output = Audio::Output::None}};
	static std::size_t inputs = 0;
	const char *extension = ".mp3";
	if (size >= kMagicBytes && std::memcmp(pData, "RIFF", kMagicBytes) == 0)
		extension = ".wav";
	else if (size >= kMagicBytes && std::memcmp(pData, "fLaC", kMagicBytes) == 0)
		extension = ".flac";
	const std::string path = "fuzz-" + std::to_string(inputs++) + extension;
	scratch.Put(path, Fuzz::Bytes(pData, size));
	std::vector<float> buffer(kMixFrames * kChannels);
	if (const Audio::SourceHandle effect = audio.PlayFlat(path, Audio::Bus::Effects);
	    effect != Audio::SourceHandle{})
	{
		audio.Mix(buffer);
		audio.Stop(effect);
	}
	if (audio.PlayMusic(path, false) != Audio::SourceHandle{})
	{
		audio.Mix(buffer);
		audio.StopMusic();
	}
	audio.Update();
	scratch.Remove(path);
	return 0;
}
