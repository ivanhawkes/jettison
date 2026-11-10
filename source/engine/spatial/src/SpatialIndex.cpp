#include <jettison/spatial/SpatialIndex.h>

#include <jettison/core/Assert.h>

namespace Jettison::Spatial
{
void SpatialIndex::Insert(entt::entity /*unused*/, const glm::dvec3 & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void SpatialIndex::Update(entt::entity /*unused*/, const glm::dvec3 & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void SpatialIndex::Remove(entt::entity /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::vector<entt::entity> SpatialIndex::QuerySphere(
    const glm::dvec3 & /*unused*/, double /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
std::vector<entt::entity> SpatialIndex::QueryBox(
    const glm::dvec3 & /*unused*/, const glm::dvec3 & /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Spatial
