#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Audio
{
// A mixer bus. Every sound plays on one, under Master: a bus's volume scales everything on it, and
// Master's everything.
enum class Bus : std::uint8_t
{
	Master,
	Music,
	Effects,
	Ui,
	Voice
};
inline constexpr std::array kBuses{Bus::Master, Bus::Music, Bus::Effects, Bus::Ui, Bus::Voice};

// "master", "music", "effects", "ui", "voice": the names settings and the console use.
std::string_view BusName(Bus bus);
std::optional<Bus> BusFromName(std::string_view name);

// A sound playing; generational, so a handle to one that ended names nothing.
struct SourceHandle
{
	std::uint32_t index{0};
	std::uint32_t generation{0};

	friend bool operator==(const SourceHandle &, const SourceHandle &) = default;
};

// How a positioned sound falls off with distance, in metres: full volume within minDistance, then
// as minDistance / (minDistance + rolloff * (distance - minDistance)), held from maxDistance out.
struct Falloff
{
	float minDistance{100.0f};
	float maxDistance{20000.0f};
	float rolloff{1.0f};
};

// Where the mix goes.
enum class Output : std::uint8_t
{
	// The machine's playback device; miniaudio's null backend when it has none (a session with no
	// sound server, a runner with no sound card), which is said in the log.
	Device,
	// miniaudio's null backend: a device that plays to nowhere, at the pace a sound card would, so
	// sounds start and end as they would be heard. Headless runs use it, and touch no sound system.
	Null,
	// No device: nothing is heard, and the output is mixed only when Mix asks for it, on the
	// caller's thread, the same every run. The tests run so.
	None
};

struct AudioOptions
{
	Output output{Output::Device};
	std::uint32_t sampleRate{48000};
};

// Counts since the system was made, for the tests and the smoke test.
struct AudioCounters
{
	// Sounds started, by bus (indexed by Bus).
	std::array<std::uint64_t, kBuses.size()> started{};
	// Sounds that could not be played: a file missing or one miniaudio cannot decode.
	std::uint64_t failed{0};
};

namespace Detail
{
class AudioState;
}

// The game's sound (docs/architecture.md, "Audio"): miniaudio behind an engine interface, so nothing
// above it includes miniaudio. Sounds are read through the VFS, so a mod's mount replaces one by
// shadowing its file; miniaudio decodes WAV, FLAC and MP3.
//
// Positions are in the system frame in double precision, as the presentation world's are, and the
// listener is too. Each Update places every positioned source relative to the listener, in float,
// with the listener at the origin: audio is listener-relative as rendering is camera-relative, so a
// station a hundred million kilometres from the primary is placed as precisely as one beside it.
//
// Main thread only; miniaudio mixes on a thread of its own and decodes streamed music on another.
// Its messages go to the log under the "audio" category, and the warnings and errors among them are
// counted (Problems), which the smoke test holds at none.
class AudioSystem
{
  public:
	// Throws std::runtime_error when miniaudio cannot start. The file system outlives it.
	explicit AudioSystem(const Vfs::FileSystem &files, const AudioOptions &options = {});
	~AudioSystem();
	AudioSystem(const AudioSystem &) = delete;
	AudioSystem &operator=(const AudioSystem &) = delete;

	// Linear gain, from 0; 1 is as the sound was made. Clamped to [0, 4].
	void SetBusVolume(Bus bus, float volume);
	float BusVolume(Bus bus) const;

	// A sound at a position, falling off with its distance from the listener; once, or looping until
	// Stop. Decoded whole as it starts, and shared by every play of it at once; let go when the last
	// one ends. A positioned sound should be mono: miniaudio places a stereo one's channels about the
	// point. An empty handle (index 0) when the sound could not be played, which is logged and counted.
	SourceHandle Play(
	    std::string_view virtualPath, Bus bus, const glm::dvec3 &position, bool loop = false,
	    const Falloff &falloff = {});
	// A sound with no position, heard alike wherever the listener is: the UI's.
	SourceHandle PlayFlat(std::string_view virtualPath, Bus bus, bool loop = false);
	// Music, streamed: decoded a little at a time as it plays, never whole, on the music bus with no
	// position. One piece at a time; a second replaces the first.
	SourceHandle PlayMusic(std::string_view virtualPath, bool loop = true);
	void StopMusic();

	// Do nothing for a handle that names nothing.
	void Stop(SourceHandle source);
	void SetPosition(SourceHandle source, const glm::dvec3 &position);
	// Linear gain on the sound itself, under its bus's; from 0.
	void SetVolume(SourceHandle source, float volume);
	bool IsPlaying(SourceHandle source) const;
	// Sounds playing now, music included.
	std::size_t Playing() const;

	// Where the player hears from: the camera, as a rotation from the listener's frame (looking
	// down -Z with +Y up, as a camera does) to the system frame.
	void SetListener(const glm::dvec3 &position, const glm::dquat &orientation);

	// Once a frame, after the presentation phase has moved the sources and the listener: places the
	// positioned sources relative to the listener, and lets go of the ones that ended.
	void Update();

	// With no device, mixes the next frames of output into the buffer, interleaved stereo float, and
	// returns how many frames it wrote. Throws std::logic_error with a device, which mixes on its own.
	std::size_t Mix(std::span<float> interleaved);
	bool HasDevice() const;
	std::uint32_t SampleRate() const;

	const AudioCounters &Counters() const;
	std::size_t Problems() const;

  private:
	std::unique_ptr<Detail::AudioState> m_pState;
};
} // namespace Jettison::Audio
