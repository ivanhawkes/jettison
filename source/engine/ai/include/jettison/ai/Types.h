#pragma once

namespace Jettison::Ai
{
// Registers the module's types with Reflect: lane travel's (Lanes.h), steering's (Steering.h), the
// behaviour trees' (BehaviourTree.h) and the utility AI's (UtilityAi.h), and the simulation's
// (Sim::RegisterTypes) and physics' (Physics::RegisterTypes). Idempotent; LaneTravel::Attach,
// Steering::Attach and Behaviours::Attach call it. A game that loads decisions or trees before it
// attaches anything calls it first.
void RegisterTypes();
} // namespace Jettison::Ai
