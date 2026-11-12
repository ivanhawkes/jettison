#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>

#include <jettison/render/Camera.h>
#include <jettison/render/Mesh.h>
#include <jettison/render/Shadows.h>

namespace Jettison::Render
{
// What the frame leaves out before anything is recorded (docs/architecture.md, "The frame"): draws
// and lights outside the view, draws and lights behind a planet's horizon, and casters that cannot
// reach a shadow cascade's map; and which level of detail each draw takes. Everything here is in
// render space, metres from the camera, in double precision: a planet's centre is ten thousand
// kilometres off, and its horizon is tested against a station a hundred metres away.

struct BoundingSphere
{
	glm::dvec3 center{0.0};
	double radius{0.0};
};

// A model's bounds through a transform into render space: the centre transformed, the radius scaled
// by the transform's largest axis, so a sphere stretched to a hull's length still holds it.
BoundingSphere TransformBounds(const ModelBounds &bounds, const glm::dmat4 &renderTransform);
// The transform's largest, middle and smallest scale along its own axes.
double MaxScale(const glm::dmat4 &transform);
double MiddleScale(const glm::dmat4 &transform);
double MinScale(const glm::dmat4 &transform);

// The view's planes in render space, each a unit normal pointing in and a distance (a point p is
// inside when dot(normal, p) + w >= 0): the near plane and the four sides. There is no far plane;
// the projection's is at infinity (ReversedInfinitePerspective).
struct Frustum
{
	std::array<glm::dvec4, 5> planes{};
};

Frustum ViewFrustum(const CameraView &camera, double aspect);
// False only when the sphere is wholly outside one of the planes. A sphere outside the frustum near
// one of its edges, but inside every plane, is kept: the test is conservative, never wrong the other
// way.
bool Intersects(const Frustum &frustum, const BoundingSphere &sphere);

// A planet as something that hides what is behind it: a sphere wholly inside the surface it draws,
// so whatever it hides is hidden by the mesh too.
struct HorizonOccluder
{
	BoundingSphere sphere;
};

// Whether the occluder hides the whole sphere from the camera, at the origin. It does when the sphere
// is inside the cone the occluder's silhouette makes and beyond the plane of its horizon: the visible
// cap of the occluder lies wholly in front of that plane, so every line of sight in the cone meets the
// occluder before it reaches the plane. A camera inside the occluder is hidden from nothing.
bool Occludes(const HorizonOccluder &occluder, const BoundingSphere &sphere);

// Whether a caster can draw into a cascade's map: its sphere reaches into the prism the map's square
// makes from its far side towards the sun, without end, since the shadow pass clamps the depth of a
// caster further sunwards than the map to the map's sun side, and it still shadows the map. The test
// is exact for the sphere: a planet beside the station is left out unless some of it is sunwards of
// the map's far side over the square itself.
bool CastsInto(const ShadowCascade &cascade, const BoundingSphere &sphere);

// What a level of detail is chosen against: how many pixels an error at one metre's distance covers,
// at the centre of the view (the viewport's height over twice the tangent of half the vertical field
// of view), and how many it may cover.
struct LodSelection
{
	double pixelsPerRadian{1.0};
	double thresholdPixels{1.0};
	double nearPlane{0.1};
};

LodSelection MakeLodSelection(const CameraView &camera, double viewportHeight, double thresholdPixels);
// The pixels an error of `error` metres covers on the sphere's nearest point to the camera, or at
// the near plane for a camera inside it.
double ProjectedError(double error, const BoundingSphere &sphere, const LodSelection &selection);
// Whether a draw is too small to see: its breadth (a model's radius times its transform's middle
// scale, so a long thin stroke is as narrow as it is wide and a flat plate as wide as it is long),
// projected at the sphere's nearest point, covers less than the pixels. Something narrower than a
// pixel in two of its three directions draws nothing a viewer could tell from its absence, and on a
// software rasteriser each of its triangles still shades a whole block of pixels.
bool TooSmallToSee(
    double breadth, const BoundingSphere &sphere, const LodSelection &selection, double pixels);
// The coarsest level whose error, scaled by `scale` and projected, covers no more than the
// threshold; the finest when none does.
std::uint32_t SelectLod(
    std::span<const ModelLod> lods, double scale, const BoundingSphere &sphere,
    const LodSelection &selection);
} // namespace Jettison::Render
