#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Jettison::Audio
{
// miniaudio behind an engine interface: buses, 3D sources attached to presentation entities,
// streamed music, and simulation events mapped to sounds by data. Filled in phase 8.
enum class Bus : std::uint8_t
{
	Master,
	Music,
	Effects,
	Ui,
	Voice
};

struct SourceHandle
{
	std::uint32_t index{0};
	std::uint32_t generation{0};
};

class AudioSystem
{
  public:
	void SetBusVolume(Bus bus, float volume);
	float BusVolume(Bus bus) const;

	// A one-shot or looping sound at a position, in the listener's frame.
	SourceHandle Play(const std::string &virtualPath, Bus bus, const glm::vec3 &position, bool loop);
	void Stop(SourceHandle source);
	void SetListener(const glm::vec3 &position, const glm::vec3 &forward, const glm::vec3 &up);

	// Once per frame, after the presentation phase moved the sources.
	void Update();
};
} // namespace Jettison::Audio
