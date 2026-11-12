#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <map>
#include <string>

#include <jettison/audio/AudioSystem.h>

namespace Jettison::Audio
{
// A looping sound on a presentation entity, heard from where the entity is: a station's machinery,
// a ship's engines. A plain component; SoundSources plays it.
struct SoundComponent
{
	std::string sound;
	Bus bus{Bus::Effects};
	float volume{1.0f};
	Falloff falloff;
};

// Plays the presentation world's SoundComponents, each from its entity's world transform
// (Scene::WorldTransformComponent): a source starts when an entity gains the component, follows it
// each frame, and stops when the entity loses it or is destroyed. A component whose sound changed
// starts again with the new one. Presentation is rebuilt from the simulation at will, so nothing
// here is saved.
class SoundSources
{
  public:
	// Once a frame, after the world transforms and before AudioSystem::Update.
	void Update(const entt::registry &registry, AudioSystem &audio);
	// Stops every source, as a scene is torn down.
	void Clear(AudioSystem &audio);
	std::size_t Size() const;

  private:
	struct Playing
	{
		SourceHandle source;
		std::string sound;
	};
	// Ordered, so the sources start in the same order every run.
	std::map<entt::entity, Playing> m_playing;
};
} // namespace Jettison::Audio
