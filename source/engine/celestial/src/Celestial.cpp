#include <jettison/celestial/Frames.h>
#include <jettison/celestial/Orbit.h>
#include <jettison/celestial/StarSystem.h>

#include <jettison/core/Assert.h>

namespace Jettison::Celestial
{
glm::vec3 ToRenderSpace(const SystemPosition & /*unused*/, const SystemPosition & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
SystemPosition ToSystem(const LocalPosition & /*unused*/, const SystemPosition & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
LocalPosition ToLocal(const SystemPosition & /*unused*/, const SystemPosition & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
glm::dvec3 Evaluate(const OrbitalElements & /*unused*/, double /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::vector<SystemPosition> EvaluateSystem(const StarSystem & /*unused*/, double /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Celestial
