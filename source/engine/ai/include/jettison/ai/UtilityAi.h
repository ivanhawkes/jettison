#pragma once

#include <functional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace Jettison::Ai
{
// Utility AI for economic choices: each action is scored by considerations, each a value in
// [0, 1] shaped by a response curve, and the best-scoring action wins. The game supplies the
// considerations; the engine scores and chooses, deterministically. Filled in phase 9.
struct Consideration
{
	std::string name;
	std::function<float(entt::registry &, entt::entity)> evaluate; // Raw input, in [0, 1].
	std::function<float(float)> curve;                             // Response curve.
};

struct ScoredAction
{
	std::string name;
	std::vector<Consideration> considerations;
};

// The index of the best action, or -1 when every action scores zero.
int Choose(const std::vector<ScoredAction> &actions, entt::registry &registry, entt::entity actor);
} // namespace Jettison::Ai
