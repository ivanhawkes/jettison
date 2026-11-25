#pragma once

namespace Jettison::Transit
{
// Registers the module's types with Reflect: the motion profile (Profile.h) as Transit.MotionProfile,
// the passage and its event (Passage.h), the portal and its (Portal.h), and the slot and its
// claim (Slot.h). Idempotent.
void RegisterTypes();
} // namespace Jettison::Transit
