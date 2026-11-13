#pragma once

#include <cstdint>

namespace Jettison::Ai
{
// Why lane travel refuses an entity or steering gives a destination up (Sim::Reason), each worded
// by the string tables under reason.ai.<name>; the arguments, where there are any, follow.
// Registered with Reflect as "Ai.Reason" by RegisterTypes.
enum class Reason : std::uint8_t
{
	NoSuchEntity,
	OnLane,
	InNoSystem,
	NoGalaxy,
	SystemNotInGalaxy,
	// The system's name and the jump point's.
	NoLane,
	// The system's name.
	ArrivalUndefined,
	DestinationGone,
	DestinationOtherSystem,
	DestinationOtherFrame
};

// Registers the module's types with Reflect: lane travel's (Lanes.h), steering's (Steering.h), the
// behaviour trees' (BehaviourTree.h) and the utility AI's (UtilityAi.h), and the simulation's
// (Sim::RegisterTypes) and physics' (Physics::RegisterTypes). Idempotent; LaneTravel::Attach,
// Steering::Attach and Behaviours::Attach call it. A game that loads decisions or trees before it
// attaches anything calls it first.
void RegisterTypes();
} // namespace Jettison::Ai
