#include <jettison/math/Intersect.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Jettison::Math
{
Aabb Transformed(const Aabb &box, const glm::mat4 &matrix)
{
	const glm::vec3 centre = (box.min + box.max) * 0.5f;
	const glm::vec3 halfExtents = (box.max - box.min) * 0.5f;
	const glm::vec3 newCentre{matrix * glm::vec4{centre, 1.0f}};
	glm::vec3 newHalfExtents{0.0f};
	for (int column = 0; column < 3; ++column)
		newHalfExtents += glm::abs(glm::vec3{matrix[column]}) * halfExtents[column];
	return {newCentre - newHalfExtents, newCentre + newHalfExtents};
}

template <typename T> bool Intersects(const BasicAabb<T> &a, const BasicAabb<T> &b)
{
	return glm::all(glm::lessThanEqual(a.min, b.max)) && glm::all(glm::lessThanEqual(b.min, a.max));
}

template <typename T> bool Intersects(const BasicSphere<T> &a, const BasicSphere<T> &b)
{
	const glm::vec<3, T> offset = b.centre - a.centre;
	const T reach = a.radius + b.radius;
	return glm::dot(offset, offset) <= reach * reach;
}

// The slab method. An axis the ray runs parallel to is a test of the origin alone: dividing by its
// zero component would give infinities, and a NaN where the origin lies on the slab's face.
template <typename T> std::optional<T> Intersect(const BasicRay<T> &ray, const BasicAabb<T> &box)
{
	T entry = T(0);
	T exit = std::numeric_limits<T>::infinity();
	for (int axis = 0; axis < 3; ++axis)
	{
		const T origin = ray.origin[axis];
		const T direction = ray.direction[axis];
		if (direction == T(0))
		{
			if (origin < box.min[axis] || origin > box.max[axis])
				return std::nullopt;
			continue;
		}
		T near = (box.min[axis] - origin) / direction;
		T far = (box.max[axis] - origin) / direction;
		if (near > far)
			std::swap(near, far);
		entry = std::max(entry, near);
		exit = std::min(exit, far);
		if (entry > exit)
			return std::nullopt;
	}
	return entry;
}

// In the box's own frame it is an axis-aligned box about the origin. The axes are a rotation, so
// the ray's direction keeps its length and the distance along it is unchanged.
template <typename T> std::optional<T> Intersect(const BasicRay<T> &ray, const BasicObb<T> &box)
{
	const glm::mat<3, 3, T> toBox = glm::transpose(box.axes);
	const BasicRay<T> local{toBox * (ray.origin - box.centre), toBox * ray.direction};
	return Intersect(local, BasicAabb<T>{-box.halfExtents, box.halfExtents});
}

template <typename T> std::optional<T> Intersect(const BasicRay<T> &ray, const BasicSphere<T> &sphere)
{
	const glm::vec<3, T> offset = ray.origin - sphere.centre;
	const T outside = glm::dot(offset, offset) - sphere.radius * sphere.radius;
	if (outside <= T(0))
		return T(0);
	const T along = glm::dot(offset, ray.direction);
	if (along >= T(0))
		return std::nullopt; // Outside, and heading away.
	const T squaredLength = glm::dot(ray.direction, ray.direction);
	const T discriminant = along * along - squaredLength * outside;
	if (discriminant < T(0))
		return std::nullopt;
	return (-along - std::sqrt(discriminant)) / squaredLength;
}

template <typename T> std::optional<T> Intersect(const BasicRay<T> &ray, const BasicPlane<T> &plane)
{
	const T approach = glm::dot(plane.normal, ray.direction);
	if (approach == T(0))
		return std::nullopt;
	const T distance = -(glm::dot(plane.normal, ray.origin) + plane.distance) / approach;
	if (distance < T(0))
		return std::nullopt;
	return distance;
}

template bool Intersects(const Aabb &, const Aabb &);
template bool Intersects(const DAabb &, const DAabb &);
template bool Intersects(const Sphere &, const Sphere &);
template bool Intersects(const DSphere &, const DSphere &);
template std::optional<float> Intersect(const Ray &, const Aabb &);
template std::optional<double> Intersect(const DRay &, const DAabb &);
template std::optional<float> Intersect(const Ray &, const Obb &);
template std::optional<double> Intersect(const DRay &, const DObb &);
template std::optional<float> Intersect(const Ray &, const Sphere &);
template std::optional<double> Intersect(const DRay &, const DSphere &);
template std::optional<float> Intersect(const Ray &, const Plane &);
template std::optional<double> Intersect(const DRay &, const DPlane &);

// Each plane is tested against the box's corner furthest along its normal: if even that corner is
// behind the plane, the whole box is.
bool Intersects(const Frustum &frustum, const Aabb &box)
{
	for (const Plane &plane : frustum.planes)
	{
		const glm::vec3 corner =
		    glm::mix(box.min, box.max, glm::greaterThanEqual(plane.normal, glm::vec3{0.0f}));
		if (glm::dot(plane.normal, corner) + plane.distance < 0.0f)
			return false;
	}
	return true;
}

bool Intersects(const Frustum &frustum, const Sphere &sphere)
{
	return std::ranges::all_of(
	    frustum.planes, [&](const Plane &plane)
	    { return glm::dot(plane.normal, sphere.centre) + plane.distance >= -sphere.radius; });
}

// Gribb and Hartmann: a point is inside when its clip coordinates satisfy -w <= x, y <= w and
// 0 <= z <= w, and each of those inequalities is a plane in the space the matrix maps from,
// whose coefficients are a sum or difference of the matrix's rows.
Frustum FrustumFromMatrix(const glm::mat4 &viewProjection)
{
	const glm::mat4 rows = glm::transpose(viewProjection);
	const std::array<glm::vec4, 6> coefficients{rows[3] + rows[0], rows[3] - rows[0],
	                                            rows[3] + rows[1], rows[3] - rows[1],
	                                            rows[2],           rows[3] - rows[2]};

	Frustum frustum;
	for (std::size_t index = 0; index < coefficients.size(); ++index)
	{
		const glm::vec4 &plane = coefficients[index];
		const glm::vec3 normal{plane};
		const float length = glm::length(normal);
		// Measured against the distance, because the coefficients carry the matrix's scale.
		if (length <= std::abs(plane.w) * 1e-6f)
			frustum.planes[index] = {glm::vec3{0.0f}, 1.0f};
		else
			frustum.planes[index] = {normal / length, plane.w / length};
	}
	return frustum;
}
} // namespace Jettison::Math
