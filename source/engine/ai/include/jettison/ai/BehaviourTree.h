#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace Jettison::Ai
{
// Behaviour trees for per-ship sequences (undock, fly to waypoint, request docking, dock),
// defined in data and ticked in the simulation step. Filled in phase 9.
enum class Status : std::uint8_t
{
	Running,
	Success,
	Failure
};

class Node
{
  public:
	virtual ~Node() = default;
	virtual Status Tick(entt::registry &registry, entt::entity actor) = 0;
};

class BehaviourTree
{
  public:
	explicit BehaviourTree(std::unique_ptr<Node> root);
	Status Tick(entt::registry &registry, entt::entity actor);

  private:
	std::unique_ptr<Node> m_pRoot;
};
} // namespace Jettison::Ai
