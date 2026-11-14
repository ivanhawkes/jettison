#pragma once

#include <cstdint>

namespace Jettison::Ai
{
// Why lane travel, a cruise or a gate refuses an entity, a gate lets one pass through unsent, or
// steering gives a destination up (Sim::Reason), each worded by the string tables under
// reason.ai.<name>; the arguments, where there are any, follow. Registered with Reflect as "Ai.Reason"
// by RegisterTypes.
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
	DestinationOtherFrame,
	NotController,
	AlreadyCruising,
	CannotCruise,
	SystemUndefined,
	// The system's name and the body's.
	NoSuchBody,
	// The station's name.
	NearStation,
	// The body's name.
	AlreadyThere,
	NoCourse,
	NotAGate,
	CannotTransit,
	// The gate's jump point, for each of these.
	NotAtGate,
	AlreadyCleared,
	AlreadyQueued,
	LeadsNowhere,
	NotCleared,
	// The gate's jump point, and its ring's radius in metres.
	HullTooLarge
};

// Registers the module's types with Reflect: lane travel's (Lanes.h), the gates' (Gates.h),
// cruising's (Cruise.h), steering's (Steering.h), the behaviour trees' (BehaviourTree.h) and the
// utility AI's (UtilityAi.h), and the simulation's (Sim::RegisterTypes) and physics'
// (Physics::RegisterTypes). Idempotent; LaneTravel::Attach, Steering::Attach and Behaviours::Attach
// call it. A game that loads decisions or
// trees before it attaches anything calls it first.
void RegisterTypes();
} // namespace Jettison::Ai
