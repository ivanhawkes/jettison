#include <jettison/audio/EventSounds.h>

#include <format>
#include <stdexcept>

#include <jettison/reflect/Reflect.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Events.h>

namespace Jettison::Audio
{
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterEnum<Bus>("Audio.Bus")
		    .Value<Bus::Master>("Master")
		    .Value<Bus::Music>("Music")
		    .Value<Bus::Effects>("Effects")
		    .Value<Bus::Ui>("Ui")
		    .Value<Bus::Voice>("Voice");
		RegisterType<EventSound>("Audio.EventSound", Attribute::Moddable)
		    .Field<&EventSound::event>("event", Attribute::Saved)
		    .Field<&EventSound::clip>("clip", Attribute::Saved)
		    .Field<&EventSound::sound>("sound", Attribute::Saved)
		    .Field<&EventSound::bus>("bus", Attribute::Saved)
		    .Field<&EventSound::at>("at", Attribute::Saved)
		    .Field<&EventSound::volume>("volume", Attribute::Saved)
		    .Field<&EventSound::minDistance>("minDistance", Attribute::Saved)
		    .Field<&EventSound::maxDistance>("maxDistance", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

EventSounds::EventSounds(const Sim::DefinitionRegistry &definitions)
{
	for (const std::string &name : definitions.Names<EventSound>())
	{
		const EventSound &sound = *definitions.Find<EventSound>(name);
		if (sound.event.empty() == sound.clip.empty())
			throw std::runtime_error(
			    std::format(
			        "{}/{}: a sound is for a simulation event or a clip's, and names one of them",
			        kEventSoundDirectory, name));
		if (!sound.clip.empty())
		{
			if (!sound.at.empty())
				throw std::runtime_error(
				    std::format(
				        "{}/{}: a clip's event is heard from its player, and is placed at no field",
				        kEventSoundDirectory, name));
			m_clipSounds[sound.clip].push_back(sound);
			continue;
		}
		const entt::meta_type type = Reflect::Resolve(sound.event);
		if (!type)
			throw std::runtime_error(
			    std::format(
			        "{}/{}: no event type {} is registered with Reflect", kEventSoundDirectory, name,
			        sound.event));
		entt::meta_data at;
		if (!sound.at.empty())
		{
			at = Reflect::FindField(type, sound.at);
			if (!at || at.type() != entt::resolve<entt::entity>())
				throw std::runtime_error(
				    std::format(
				        "{}/{}: {} has no entity field {} to place the sound at", kEventSoundDirectory,
				        name, sound.event, sound.at));
		}
		m_sounds[type.info().hash()].push_back({sound, at});
	}
}

std::size_t EventSounds::Play(
    const Sim::SimEvent &event, AudioSystem &audio, const PositionOf &positionOf) const
{
	const auto found = m_sounds.find(event.event.type().info().hash());
	if (found == m_sounds.end())
		return 0;
	std::size_t started = 0;
	for (const Mapped &mapped : found->second)
	{
		const EventSound &sound = mapped.sound;
		std::optional<glm::dvec3> position;
		if (mapped.at)
		{
			const entt::meta_any value = mapped.at.get(event.event);
			const auto *pEntity = value.try_cast<entt::entity>();
			if (pEntity != nullptr && *pEntity != entt::null && positionOf)
				position = positionOf(*pEntity);
		}
		started += Start(sound, position, audio) ? 1 : 0;
	}
	return started;
}

std::size_t EventSounds::PlayClipEvent(
    std::string_view name, const std::optional<glm::dvec3> &position, AudioSystem &audio) const
{
	const auto found = m_clipSounds.find(name);
	if (found == m_clipSounds.end())
		return 0;
	std::size_t started = 0;
	for (const EventSound &sound : found->second)
		started += Start(sound, position, audio) ? 1 : 0;
	return started;
}

std::size_t EventSounds::Events() const
{
	return m_sounds.size() + m_clipSounds.size();
}

std::size_t EventSounds::Sounds() const
{
	std::size_t count = 0;
	for (const auto &[type, sounds] : m_sounds)
		count += sounds.size();
	for (const auto &[clip, sounds] : m_clipSounds)
		count += sounds.size();
	return count;
}

bool EventSounds::Start(
    const EventSound &sound, const std::optional<glm::dvec3> &position, AudioSystem &audio)
{
	const SourceHandle source =
	    position ? audio.Play(
	                   sound.sound, sound.bus, *position, false,
	                   Falloff{sound.minDistance, sound.maxDistance, Falloff{}.rolloff})
	             : audio.PlayFlat(sound.sound, sound.bus);
	if (source == SourceHandle{})
		return false;
	audio.SetVolume(source, sound.volume);
	return true;
}
} // namespace Jettison::Audio
