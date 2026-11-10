#include <jettison/math/Noise.h>

#include <FastNoiseLite.h>

namespace Jettison::Math
{
namespace
{
std::unique_ptr<FastNoiseLite> MakeNoise(std::int32_t seed)
{
	auto noise = std::make_unique<FastNoiseLite>(seed);
	noise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
	noise->SetFrequency(1.0f);
	return noise;
}
} // namespace

Noise::Noise(std::int32_t seed) : m_seed{seed}, m_pNoise{MakeNoise(seed)}
{
}

Noise::~Noise() = default;

Noise::Noise(const Noise &other) : m_seed{other.m_seed}, m_pNoise{MakeNoise(other.m_seed)}
{
}

Noise &Noise::operator=(const Noise &other)
{
	if (this != &other)
	{
		m_seed = other.m_seed;
		m_pNoise = MakeNoise(other.m_seed);
	}
	return *this;
}

Noise::Noise(Noise &&) noexcept = default;
Noise &Noise::operator=(Noise &&) noexcept = default;

float Noise::Sample(const glm::vec2 &position) const
{
	return m_pNoise->GetNoise(position.x, position.y);
}

float Noise::Sample(const glm::vec3 &position) const
{
	return m_pNoise->GetNoise(position.x, position.y, position.z);
}
} // namespace Jettison::Math
