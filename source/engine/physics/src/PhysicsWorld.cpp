#include <jettison/physics/PhysicsWorld.h>

#include <jettison/core/Assert.h>

namespace Jettison::Physics
{
void PhysicsWorld::AddBody(
    entt::entity /*unused*/, BodyKind /*unused*/, const ShapeDescription & /*unused*/,
    const glm::dvec3 & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void PhysicsWorld::RemoveBody(entt::entity /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void PhysicsWorld::Step(double /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<RayHit> PhysicsWorld::CastRay(
    const glm::dvec3 & /*unused*/, const glm::dvec3 & /*unused*/, double /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Physics
