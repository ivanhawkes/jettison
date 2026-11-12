#pragma once

namespace Jettison::Ai
{
// Registers the module's types with Reflect, lane travel's (Lanes.h) and steering's (Steering.h),
// and the simulation's (Sim::RegisterTypes) and physics' (Physics::RegisterTypes). Idempotent;
// LaneTravel::Attach and Steering::Attach call it.
void RegisterTypes();
} // namespace Jettison::Ai
