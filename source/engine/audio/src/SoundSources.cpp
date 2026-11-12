#include <jettison/audio/SoundSources.h>

#include <vector>

#include <jettison/scene/Components.h>

namespace Jettison::Audio
{
void SoundSources::Update(const entt::registry &registry, AudioSystem &audio)
{
	using Scene::WorldTransformComponent;

	// Gone: the entity was destroyed, or lost its component or its transform.
	std::vector<entt::entity> gone;
	for (const auto &[entity, playing] : m_playing)
		if (!registry.valid(entity) ||
		    !registry.all_of<SoundComponent, WorldTransformComponent>(entity))
			gone.push_back(entity);
	for (const entt::entity entity : gone)
	{
		audio.Stop(m_playing.at(entity).source);
		m_playing.erase(entity);
	}

	for (const auto [entity, component, world] :
	     registry.view<const SoundComponent, const WorldTransformComponent>().each())
	{
		const glm::dvec3 position(world.matrix[3]);
		const auto found = m_playing.find(entity);
		if (found != m_playing.end() && found->second.sound == component.sound)
		{
			audio.SetPosition(found->second.source, position);
			audio.SetVolume(found->second.source, component.volume);
			continue;
		}
		if (found != m_playing.end())
			audio.Stop(found->second.source);
		const SourceHandle source =
		    audio.Play(component.sound, component.bus, position, true, component.falloff);
		audio.SetVolume(source, component.volume);
		// Kept when it would not play too (an empty handle), so a missing file is reported once and
		// not tried again every frame.
		m_playing[entity] = {source, component.sound};
	}
}

void SoundSources::Clear(AudioSystem &audio)
{
	for (const auto &[entity, playing] : m_playing)
		audio.Stop(playing.source);
	m_playing.clear();
}

std::size_t SoundSources::Size() const
{
	return m_playing.size();
}
} // namespace Jettison::Audio
