#pragma once

#include <cstdint>

namespace Jettison::Ai
{
// Why steering, a helm or a behaviour refuses an entity or gives a destination up (Sim::Reason), each
// worded by the string tables under reason.ai.<name>. Registered with Reflect as "Ai.Reason" by
// RegisterTypes.
enum class Reason : std::uint8_t
{
	NoSuchEntity,
	DestinationGone,
	DestinationOtherFrame,
	DestinationOtherLocalFrame,
	NotController,
	NoPilot,
	HelmHeld,
	NotAtHelm,
	// The tree's name.
	NoSuchTree
};

// Registers the module's types with Reflect: steering's (Steering.h), the behaviour trees'
// (BehaviourTree.h) and the utility AI's (UtilityAi.h), and the simulation's (Sim::RegisterTypes) and
// physics' (Physics::RegisterTypes). Idempotent; Steering::Attach and Behaviours::Attach call it.
// A game that loads decisions or trees before it attaches anything calls it
// first.
void RegisterTypes();
} // namespace Jettison::Ai
