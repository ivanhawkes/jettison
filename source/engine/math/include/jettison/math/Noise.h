#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Jettison::Math
{
// CPU noise for procedural generation, over FastNoiseLite. The library stays behind this type so
// its headers do not reach the engine's public headers. Filled in phase 2.
class Noise
{
  public:
	explicit Noise(std::int32_t seed = 0);

	// In [-1, 1].
	float Sample(const glm::vec2 &position) const;
	float Sample(const glm::vec3 &position) const;

	std::int32_t Seed() const
	{
		return m_seed;
	}

  private:
	std::int32_t m_seed{0};
};
} // namespace Jettison::Math
