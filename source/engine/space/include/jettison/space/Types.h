#pragma once

namespace Jettison::Space
{
// Registers the module's types with Reflect: lane travel's (Lanes.h), the gates' (Gates.h) and
// cruising's (Cruise.h), under the names they were saved by when they were the AI's ("Ai.OnLane"),
// and the AI's (Ai::RegisterTypes). Idempotent; LaneTravel::Attach, Gates::Attach and
// CruiseTravel::Attach call it.
void RegisterTypes();
} // namespace Jettison::Space
