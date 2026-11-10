#include <jettison/math/Easing.h>
#include <jettison/math/Intersect.h>
#include <jettison/math/Noise.h>
#include <jettison/math/Random.h>
#include <jettison/math/Spline.h>

#include <jettison/core/Assert.h>

namespace Jettison::Math
{
bool Intersects(const Aabb & /*unused*/, const Aabb & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
bool Intersects(const Sphere & /*unused*/, const Sphere & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
bool Intersects(const Frustum & /*unused*/, const Aabb & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
bool Intersects(const Frustum & /*unused*/, const Sphere & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<float> Intersect(const Ray & /*unused*/, const Aabb & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<float> Intersect(const Ray & /*unused*/, const Sphere & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::optional<float> Intersect(const Ray & /*unused*/, const Plane & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
Frustum FrustumFromMatrix(const glm::mat4 & /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}

glm::vec3 CubicBezier(
    const glm::vec3 & /*unused*/, const glm::vec3 & /*unused*/, const glm::vec3 & /*unused*/,
    const glm::vec3 & /*unused*/, float /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
glm::vec3 CatmullRom(std::span<const glm::vec3> /*unused*/, float /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}

float Ease(Easing /*unused*/, float /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}

Random::Random(std::uint64_t seed) : m_state{seed}
{
}
std::uint32_t Random::NextU32()
{
	JETTISON_NOT_IMPLEMENTED();
}
std::uint64_t Random::NextU64()
{
	JETTISON_NOT_IMPLEMENTED();
}
float Random::NextFloat()
{
	JETTISON_NOT_IMPLEMENTED();
}
double Random::NextDouble()
{
	JETTISON_NOT_IMPLEMENTED();
}
std::int32_t Random::Range(std::int32_t /*unused*/, std::int32_t /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
float Random::Range(float /*unused*/, float /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}

Noise::Noise(std::int32_t seed) : m_seed{seed}
{
}
float Noise::Sample(const glm::vec2 & /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
float Noise::Sample(const glm::vec3 & /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Math
