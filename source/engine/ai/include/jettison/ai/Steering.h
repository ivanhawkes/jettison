#pragma once

#include <glm/glm.hpp>

#include <span>

namespace Jettison::Ai
{
// Steering for local flight: seek, arrive, obstacle avoidance and separation, each returning a
// desired acceleration in the system frame to be summed and clamped by the ship's flight model
// in the game. Filled in phase 9.
struct Agent
{
	glm::dvec3 position{0.0};
	glm::dvec3 velocity{0.0};
	double maxSpeed{0.0};
	double maxAcceleration{0.0};
	double radius{0.0};
};

struct Obstacle
{
	glm::dvec3 position{0.0};
	double radius{0.0};
};

glm::dvec3 Seek(const Agent &agent, const glm::dvec3 &target);
glm::dvec3 Arrive(const Agent &agent, const glm::dvec3 &target, double slowingDistance);
glm::dvec3 Avoid(const Agent &agent, std::span<const Obstacle> obstacles, double lookahead);
glm::dvec3 Separate(const Agent &agent, std::span<const Agent> neighbours, double desiredSeparation);
} // namespace Jettison::Ai
