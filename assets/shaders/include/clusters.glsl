// The cluster grid and the local lights' reach (docs/architecture.md, "Rendering: clustered
// forward"): screen tiles times depth slices, exponential in view distance from the near plane to
// the cluster range, and the tests that decide which lights a cluster lists. The bounds program
// (cluster_bounds.comp) and the assignment program (cluster_lights.comp) compute through it, the
// lighting include finds a fragment's cluster and attenuates its lights through it, and the
// renderer's CPU reference (render/Clusters.h) checks the GPU's assignment against it.
//
// A light's falloff is here beside the tests on purpose: the falloff is windowed to reach zero at
// the light's range, which is what makes testing the range against a cluster exact rather than a
// guess at where the light stops mattering.
//
// It is written in the subset of GLSL that GLM also compiles as C++, as brdf.glsl is: no swizzles,
// no out parameters, no struct constructors, float literals with an f suffix.
//
// View space is the camera's: x right, y up, looking down -z. A view depth is the distance along
// -z, never the depth buffer's value, which the infinite reversed-Z projection spends on the far
// distances.

#ifndef JETTISON_CLUSTERS_GLSL
#define JETTISON_CLUSTERS_GLSL

// Tuned in 5e against a station with 265 lights (scripts/make-light-budget-scene.py): from 16 x 9 x
// 24, doubling the tiles each way and the slices took the opaque pass from 0.43 ms to 0.34 ms on an
// RTX 5060 Ti and from 47 ms to 36 ms on lavapipe, the assignment costing no more. At 1080p a tile is
// 60 pixels square, and at 160 m a slice is 28 m deep.
const uint kClusterTilesX = 32u;
const uint kClusterTilesY = 18u;
const uint kClusterSlices = 48u;
const uint kClusterCount = kClusterTilesX * kClusterTilesY * kClusterSlices;

// A light is treated as a bulb this wide: nearer its centre than this, the inverse square stops
// climbing. Without it a surface through a light's position divides by zero, and a floodlight of
// intensity 2000 a millimetre from the hull overflows the half-float HDR target.
const float kLightBulbRadius = 0.05f;

// The light types, as the light list's directionType.w carries them.
const float kLightPoint = 0.0f;
const float kLightSpot = 1.0f;

// A cluster's axis-aligned bounds in view space.
struct ClusterBounds
{
	vec3 minimum;
	vec3 maximum;
};

// A tile's size in pixels: the viewport divided into the grid's columns and rows, rounded up so
// the last tile reaches the edge.
vec2 ClusterTileSize(vec2 viewport)
{
	return vec2(
	    ceil(viewport.x / float(kClusterTilesX)), ceil(viewport.y / float(kClusterTilesY)));
}

// The view depth at which a slice begins; slice kClusterSlices begins at the cluster range.
float ClusterSliceDepth(uint slice, float nearPlane, float range)
{
	return nearPlane * pow(range / nearPlane, float(slice) / float(kClusterSlices));
}

// The slice a view depth falls in, or kClusterSlices when it is outside the grid: nearer than the
// near plane, or at or past the cluster range, where no local light is assigned.
uint ClusterSlice(float depth, float nearPlane, float range)
{
	if (depth < nearPlane || depth >= range)
		return kClusterSlices;
	float slice = log(depth / nearPlane) / log(range / nearPlane) * float(kClusterSlices);
	return min(uint(slice), kClusterSlices - 1u);
}

uint ClusterIndex(uint x, uint y, uint slice)
{
	return x + kClusterTilesX * (y + kClusterTilesY * slice);
}

// The cluster a point in a pixel at a view depth falls in, or kClusterCount when it is outside the
// grid. The pixel is in framebuffer coordinates, as gl_FragCoord has them.
uint ClusterAt(vec2 pixel, float depth, vec2 tileSize, float nearPlane, float range)
{
	uint slice = ClusterSlice(depth, nearPlane, range);
	if (slice >= kClusterSlices)
		return kClusterCount;
	uint x = min(uint(max(pixel.x, 0.0f) / tileSize.x), kClusterTilesX - 1u);
	uint y = min(uint(max(pixel.y, 0.0f) / tileSize.y), kClusterTilesY - 1u);
	return ClusterIndex(x, y, slice);
}

// The view-space point on the ray through a framebuffer position, at a view depth. The projection
// scale is the projection matrix's [0][0] and [1][1]: clip x is scale.x times view x, clip w is the
// depth, and [1][1] is negative because Vulkan's framebuffer y runs down.
vec3 ViewPointAtDepth(vec2 pixel, vec2 viewport, vec2 projectionScale, float depth)
{
	float ndcX = 2.0f * pixel.x / viewport.x - 1.0f;
	float ndcY = 2.0f * pixel.y / viewport.y - 1.0f;
	return vec3(ndcX * depth / projectionScale.x, ndcY * depth / projectionScale.y, -depth);
}

// A cluster's bounds: the box around its tile's four corner rays between its slice's two depths.
// The cluster is the frustum those eight points span, which is convex, so their box holds it.
ClusterBounds ClusterBoundsOf(
    uint cluster, vec2 viewport, vec2 tileSize, vec2 projectionScale, float nearPlane, float range)
{
	uint x = cluster % kClusterTilesX;
	uint y = (cluster / kClusterTilesX) % kClusterTilesY;
	uint slice = cluster / (kClusterTilesX * kClusterTilesY);
	float lowX = float(x) * tileSize.x;
	float lowY = float(y) * tileSize.y;
	float nearDepth = ClusterSliceDepth(slice, nearPlane, range);
	float farDepth = ClusterSliceDepth(slice + 1u, nearPlane, range);

	ClusterBounds bounds;
	bounds.minimum = vec3(3.4e38f);
	bounds.maximum = vec3(-3.4e38f);
	for (uint corner = 0u; corner < 8u; ++corner)
	{
		vec2 pixel = vec2(
		    (corner & 1u) != 0u ? lowX + tileSize.x : lowX,
		    (corner & 2u) != 0u ? lowY + tileSize.y : lowY);
		float depth = (corner & 4u) != 0u ? farDepth : nearDepth;
		vec3 point = ViewPointAtDepth(pixel, viewport, projectionScale, depth);
		bounds.minimum = min(bounds.minimum, point);
		bounds.maximum = max(bounds.maximum, point);
	}
	return bounds;
}

bool SphereIntersectsBounds(vec3 center, float radius, ClusterBounds bounds)
{
	vec3 closest = clamp(center, bounds.minimum, bounds.maximum);
	vec3 offset = closest - center;
	return dot(offset, offset) <= radius * radius;
}

// Whether a cone (apex, unit axis, length, and its half-angle's cosine and sine, the angle at most a
// right angle) can reach a sphere. Conservative: the distance it measures is to the cone's side as
// an infinite line, which is never more than the distance to the cone itself, so a sphere it
// rejects is out of reach, and one it keeps may only nearly be (Wronski, "Cull that cone!", 2017).
bool ConeIntersectsSphere(
    vec3 apex, vec3 axis, float coneLength, float cosAngle, float sinAngle, vec3 center, float radius)
{
	vec3 toCenter = center - apex;
	float along = dot(toCenter, axis);
	float across = sqrt(max(dot(toCenter, toCenter) - along * along, 0.0f));
	bool outsideAngle = cosAngle * across - along * sinAngle > radius;
	bool beyondEnd = along > coneLength + radius;
	bool behindApex = along < -radius;
	return !(outsideAngle || beyondEnd || behindApex);
}

// Whether a light can reach any point of a cluster. The light is in view space: its position, its
// range, and for a spot the way it points and its outer cone's cosine and sine. The margin widens
// the light by that many metres, or narrows it when negative, which only the CPU reference uses, to
// tell a disagreement from a rounding difference on a cluster's edge; the GPU passes zero.
bool LightReachesCluster(
    vec3 position, float range, bool spot, vec3 direction, float cosOuter, float sinOuter,
    ClusterBounds bounds, float margin)
{
	if (!SphereIntersectsBounds(position, max(range + margin, 0.0f), bounds))
		return false;
	if (!spot)
		return true;
	vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
	float radius = max(length(bounds.maximum - bounds.minimum) * 0.5f + margin, 0.0f);
	return ConeIntersectsSphere(position, direction, range, cosOuter, sinOuter, center, radius);
}

// Inverse-square falloff, windowed to reach zero at the light's range and to stay smooth there
// (Karis, "Real Shading in Unreal Engine 4", 2013): (1 - (d/r)^4)^2 / d^2. Near the light the
// window is one and the light is physical; it is zero at and past the range, so a cluster the range
// does not reach is one the light does not light.
float RangeAttenuation(float distanceSquared, float range)
{
	float ratio = distanceSquared / (range * range);
	float window = clamp(1.0f - ratio * ratio, 0.0f, 1.0f);
	return window * window / max(distanceSquared, kLightBulbRadius * kLightBulbRadius);
}

// A spot's falloff across its cone, as glTF's KHR_lights_punctual has it: one inside the inner
// angle, zero outside the outer, and a smooth square between. The scale and offset are made from
// the angles' cosines on the CPU; a point light's are 0 and 1, which makes this one everywhere.
float SpotAttenuation(vec3 spotDirection, vec3 lightToSurface, float scale, float offset)
{
	float attenuation = clamp(dot(spotDirection, lightToSurface) * scale + offset, 0.0f, 1.0f);
	return attenuation * attenuation;
}

#endif
