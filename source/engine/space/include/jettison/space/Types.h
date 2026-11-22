#pragma once

#include <cstdint>

namespace Jettison::Space
{
// Why lane travel, a cruise or a gate refuses a ship, or a gate lets one pass through unsent
// (Sim::Reason), each worded by the string tables under reason.space.<name>; the arguments, where
// there are any, follow. Registered with Reflect as "Space.Reason" by RegisterTypes.
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

// Registers the module's types with Reflect: its reasons, lane travel's (Lanes.h), the gates'
// (Gates.h) and cruising's (Cruise.h), under the names they were saved by when they were the AI's
// ("Ai.OnLane"), and the AI's (Ai::RegisterTypes). Idempotent; LaneTravel::Attach, Gates::Attach and
// CruiseTravel::Attach call it.
void RegisterTypes();
} // namespace Jettison::Space
