#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Pathfinding.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/UtilityAi.h>

#include <jettison/core/Assert.h>

#include <utility>

namespace Jettison::Ai
{
int Choose(
    const std::vector<ScoredAction> & /*unused*/, entt::registry & /*unused*/, entt::entity /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}

BehaviourTree::BehaviourTree(std::unique_ptr<Node> root) : m_pRoot{std::move(root)}
{
}
Status BehaviourTree::Tick(entt::registry & /*unused*/, entt::entity /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}

std::optional<std::vector<NodeId>> FindPath(
    const Graph & /*unused*/, NodeId /*unused*/, NodeId /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}

glm::dvec3 Seek(const Agent & /*unused*/, const glm::dvec3 & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
glm::dvec3 Arrive(const Agent & /*unused*/, const glm::dvec3 & /*unused*/, double /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
glm::dvec3 Avoid(const Agent & /*unused*/, std::span<const Obstacle> /*unused*/, double /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
glm::dvec3 Separate(const Agent & /*unused*/, std::span<const Agent> /*unused*/, double /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Ai
