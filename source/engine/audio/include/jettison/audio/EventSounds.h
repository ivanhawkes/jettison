#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/audio/AudioSystem.h>

namespace Jettison::Sim
{
class DefinitionRegistry;
struct SimEvent;
} // namespace Jettison::Sim

namespace Jettison::Audio
{
// A sound a simulation event plays, as data: a definition (Sim::DefinitionRegistry) under
// "defs/sounds", so a mod replaces one by shipping a file at its path and adds one with a new path.
//
//   version: 1
//   event: Shell.ShipDocked        # the event's type, as Reflect names it
//   sound: audio/effects/docked.wav
//   bus: Effects
//   at: station                    # the event's entity field the sound is placed at; none: flat
//   minDistance: 200
struct EventSound
{
	std::string event;
	std::string sound;
	Bus bus{Bus::Effects};
	std::string at;
	float volume{1.0f};
	float minDistance{Falloff{}.minDistance};
	float maxDistance{Falloff{}.maxDistance};
};

inline constexpr std::string_view kEventSoundDirectory = "defs/sounds";
inline constexpr std::uint32_t kEventSoundVersion = 1;

// Registers Bus and EventSound with Reflect. Idempotent.
void RegisterTypes();

// Where presentation shows a simulation entity now, in the system frame; none for one it does not.
using PositionOf = std::function<std::optional<glm::dvec3>(entt::entity)>;

// The event sounds the definitions name, by event type: turns the events presentation takes from
// the simulation (Sim::EventQueue) into sounds. The game chooses which events reach it, the player's
// own trades and not every trader's, and the data chooses what each sounds like.
class EventSounds
{
  public:
	// Every EventSound loaded in the registry, checked against the types Reflect knows: throws
	// std::runtime_error naming the definition for an event type that is not registered, or an `at`
	// that is not one of its fields holding an entity. Several definitions may name one event; each
	// plays, in the order of their names.
	explicit EventSounds(const Sim::DefinitionRegistry &definitions);

	// Plays the sounds the event's type maps to, and returns how many started. A sound placed `at`
	// an entity presentation cannot place, or one null, plays flat rather than not at all: the
	// player still hears that the event happened.
	std::size_t Play(
	    const Sim::SimEvent &event, AudioSystem &audio, const PositionOf &positionOf) const;

	// The event types mapped, and the sounds in all.
	std::size_t Events() const;
	std::size_t Sounds() const;

  private:
	struct Mapped
	{
		EventSound sound;
		entt::meta_data at;
	};
	// Keyed by EnTT's type ID.
	std::map<entt::id_type, std::vector<Mapped>> m_sounds;
};
} // namespace Jettison::Audio
