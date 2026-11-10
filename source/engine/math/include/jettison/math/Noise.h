#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

class FastNoiseLite;

namespace Jettison::Math
{
// CPU noise for procedural generation: FastNoiseLite's OpenSimplex2 at frequency 1, so features
// are about a unit apart and the caller scales the position for other sizes. The library stays
// behind this type, so its header does not reach the engine's public headers. Its output feeds
// generated content that saves refer to, so the tests pin it with recorded values, and math
// compiles deterministically (DETERMINISTIC in its CMakeLists.txt).
class Noise
{
  public:
	explicit Noise(std::int32_t seed = 0);
	~Noise();
	Noise(const Noise &other);
	Noise &operator=(const Noise &other);
	Noise(Noise &&) noexcept;
	Noise &operator=(Noise &&) noexcept;

	// In [-1, 1].
	float Sample(const glm::vec2 &position) const;
	float Sample(const glm::vec3 &position) const;

	std::int32_t Seed() const
	{
		return m_seed;
	}

  private:
	std::int32_t m_seed{0};
	std::unique_ptr<FastNoiseLite> m_pNoise;
};
} // namespace Jettison::Math
