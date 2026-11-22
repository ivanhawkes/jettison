#pragma once

namespace Jettison::Transit
{
// Registers the module's types with Reflect: the motion profile (Profile.h), under the name it was
// saved by before the module was neutral, until saved names change (docs/space-extraction-plan.md,
// slice 7), the passage and its event (Passage.h), and the portal and its (Portal.h). Idempotent.
void RegisterTypes();
} // namespace Jettison::Transit
