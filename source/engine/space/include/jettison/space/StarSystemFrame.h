#pragma once

#include <jettison/celestial/StarSystem.h>

namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::Space
{
// Defines a star system as the physics frame of its name (Sim::FrameId, Physics::FrameModel): its
// bodies are what rails and local frames name (Physics::OnRails, Physics::LocalFrame), each where
// its orbit has it and turned as it spins, or as a jump point faces (Celestial::BodyOrientation); and
// its gravity is each body with a gravitational parameter pulling as a point mass, and as one whose
// mass is all within the point's distance from its centre, inside its radius. The system has to be
// resolved (Celestial::Resolve), as reading one does. Throws std::invalid_argument for one that is
// not, or has no name or bodies, or whose frame is already defined.
void DefineStarSystem(Physics::PhysicsWorld &physics, Celestial::StarSystem system);
} // namespace Jettison::Space
