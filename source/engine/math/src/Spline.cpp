#include <jettison/math/Spline.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <jettison/core/Assert.h>

namespace Jettison::Math
{
namespace
{
// The knot spacing that makes a Catmull-Rom spline centripetal: the square root of the distance
// between neighbouring points. Coincident points are kept a hair apart, so a repeated point is a
// pause on the curve rather than a division by zero.
float KnotInterval(const glm::vec3 &from, const glm::vec3 &to)
{
	return std::max(std::sqrt(glm::length(to - from)), 1e-4f);
}

// The span from p1 to p2, by the Barry-Goldman pyramid of linear interpolations, u in [0, 1].
glm::vec3 CentripetalSpan(
    const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, float u)
{
	const float t0 = 0.0f;
	const float t1 = t0 + KnotInterval(p0, p1);
	const float t2 = t1 + KnotInterval(p1, p2);
	const float t3 = t2 + KnotInterval(p2, p3);
	const float t = t1 + (t2 - t1) * u;

	const auto blend = [t](const glm::vec3 &a, const glm::vec3 &b, float ta, float tb)
	{ return a * ((tb - t) / (tb - ta)) + b * ((t - ta) / (tb - ta)); };
	const glm::vec3 a1 = blend(p0, p1, t0, t1);
	const glm::vec3 a2 = blend(p1, p2, t1, t2);
	const glm::vec3 a3 = blend(p2, p3, t2, t3);
	const glm::vec3 b1 = blend(a1, a2, t0, t2);
	const glm::vec3 b2 = blend(a2, a3, t1, t3);
	return blend(b1, b2, t1, t2);
}
} // namespace

glm::vec3 CubicBezier(
    const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	const float u = 1.0f - t;
	return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
}

glm::vec3 CatmullRom(std::span<const glm::vec3> points, float t)
{
	JETTISON_ASSERT(!points.empty());
	if (points.size() == 1)
		return points.front();

	const std::size_t spans = points.size() - 1;
	const float position = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(spans);
	const std::size_t span = std::min(static_cast<std::size_t>(position), spans - 1);
	const float u = position - static_cast<float>(span);

	const glm::vec3 &p1 = points[span];
	const glm::vec3 &p2 = points[span + 1];
	const glm::vec3 p0 = span > 0 ? points[span - 1] : p1 * 2.0f - p2;
	const glm::vec3 p3 = span + 2 < points.size() ? points[span + 2] : p2 * 2.0f - p1;
	return CentripetalSpan(p0, p1, p2, p3, u);
}
} // namespace Jettison::Math
