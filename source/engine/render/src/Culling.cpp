#include <jettison/render/Culling.h>

#include <algorithm>
#include <cmath>

namespace Jettison::Render
{
BoundingSphere TransformBounds(const ModelBounds &bounds, const glm::dmat4 &renderTransform)
{
	const glm::dvec3 center{renderTransform * glm::dvec4{glm::dvec3{bounds.center}, 1.0}};
	return {center, double{bounds.radius} * MaxScale(renderTransform)};
}

double MaxScale(const glm::dmat4 &transform)
{
	return std::max(
	    {glm::length(glm::dvec3{transform[0]}), glm::length(glm::dvec3{transform[1]}),
	     glm::length(glm::dvec3{transform[2]})});
}

double MinScale(const glm::dmat4 &transform)
{
	return std::min(
	    {glm::length(glm::dvec3{transform[0]}), glm::length(glm::dvec3{transform[1]}),
	     glm::length(glm::dvec3{transform[2]})});
}

Frustum ViewFrustum(const CameraView &camera, double aspect)
{
	const glm::dvec3 right = camera.orientation * glm::dvec3{1.0, 0.0, 0.0};
	const glm::dvec3 up = camera.orientation * glm::dvec3{0.0, 1.0, 0.0};
	const glm::dvec3 forward = camera.orientation * glm::dvec3{0.0, 0.0, -1.0};
	const double tanY = std::tan(double{camera.verticalFov} * 0.5);
	const double tanX = tanY * aspect;
	// A point at depth d along the view is inside the left side when its offset to the right is more
	// than -d tan(x), which is a plane through the camera with normal right + tan(x) forward.
	const auto side = [](const glm::dvec3 &across, const glm::dvec3 &along, double slope)
	{ return glm::dvec4{glm::normalize(across + along * slope), 0.0}; };
	Frustum frustum;
	frustum.planes[0] = glm::dvec4{forward, -double{camera.nearPlane}};
	frustum.planes[1] = side(right, forward, tanX);
	frustum.planes[2] = side(-right, forward, tanX);
	frustum.planes[3] = side(up, forward, tanY);
	frustum.planes[4] = side(-up, forward, tanY);
	return frustum;
}

bool Intersects(const Frustum &frustum, const BoundingSphere &sphere)
{
	for (const glm::dvec4 &plane : frustum.planes)
		if (glm::dot(glm::dvec3{plane}, sphere.center) + plane.w < -sphere.radius)
			return false;
	return true;
}

bool Occludes(const HorizonOccluder &occluder, const BoundingSphere &sphere)
{
	const double distance = glm::length(occluder.sphere.center);
	const double radius = occluder.sphere.radius;
	if (distance <= radius || radius <= 0.0)
		return false;
	const glm::dvec3 axis = occluder.sphere.center / distance;
	// The horizon is the circle the lines of sight graze; its plane is this far along the axis.
	const double horizon = (distance * distance - radius * radius) / distance;
	const double along = glm::dot(sphere.center, axis);
	if (along - sphere.radius < horizon)
		return false;
	// Inside the cone: the sphere's angle off the axis, and its own angular radius, within the cone's.
	const double offAxis = std::atan2(glm::length(glm::cross(sphere.center, axis)), along);
	const double extent = std::asin(std::min(sphere.radius / glm::length(sphere.center), 1.0));
	const double cone = std::asin(radius / distance);
	return offAxis + extent <= cone;
}

bool CastsInto(const ShadowCascade &cascade, const BoundingSphere &sphere)
{
	// The map's x and y run from -1 to 1 and its depth from 1 on the sun's side to 0 on the far side
	// (Shadows.h); each row of the matrix is one of them, its length the units a metre is. What can
	// shadow the map is what reaches into the prism of its square, from its far side sunwards without
	// end, since the shadow pass clamps the depth of what is beyond its sun side.
	const glm::dmat4 matrix{cascade.matrix};
	const auto row = [&](int index)
	{ return glm::dvec3{matrix[0][index], matrix[1][index], matrix[2][index]}; };
	// How far outside the square the sphere's centre is across the sun, in metres.
	double outsideSquared = 0.0;
	for (int axis = 0; axis < 2; ++axis)
	{
		const double perMetre = glm::length(row(axis));
		const double value = glm::dot(row(axis), sphere.center) + matrix[3][axis];
		const double outside = std::max(std::abs(value) - 1.0, 0.0) / perMetre;
		outsideSquared += outside * outside;
	}
	const double radiusSquared = sphere.radius * sphere.radius;
	if (outsideSquared > radiusSquared)
		return false;
	// Over the square, the sphere reaches furthest sunwards where it is nearest the square, by the
	// half chord there; it shadows the map unless even that is beyond the far side.
	const double depth = glm::dot(row(2), sphere.center) + matrix[3][2];
	const double reach = std::sqrt(radiusSquared - outsideSquared) * glm::length(row(2));
	return depth + reach >= 0.0;
}

LodSelection MakeLodSelection(const CameraView &camera, double viewportHeight, double thresholdPixels)
{
	return {
	    viewportHeight / (2.0 * std::tan(double{camera.verticalFov} * 0.5)), thresholdPixels,
	    double{camera.nearPlane}};
}

double ProjectedError(double error, const BoundingSphere &sphere, const LodSelection &selection)
{
	const double distance = std::max(glm::length(sphere.center) - sphere.radius, selection.nearPlane);
	return error * selection.pixelsPerRadian / distance;
}

std::uint32_t SelectLod(
    std::span<const ModelLod> lods, double scale, const BoundingSphere &sphere,
    const LodSelection &selection)
{
	for (std::size_t level = lods.size(); level-- > 1;)
		if (ProjectedError(double{lods[level].error} * scale, sphere, selection) <=
		    selection.thresholdPixels)
			return static_cast<std::uint32_t>(level);
	return 0;
}
} // namespace Jettison::Render
