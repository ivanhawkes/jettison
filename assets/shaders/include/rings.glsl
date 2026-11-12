// A body's rings (docs/architecture.md, "Atmospheres"): a flat annulus in the plane of its equator,
// of bands and gaps drawn from a seed, lit by the sun, in the body's shadow, and casting its own
// onto the body. The planet program composites them over the surface where they are in front of it
// and darkens the sunlight they stand in the way of; the atmosphere program composites them over
// space.
//
// Lengths are metres in render space, the camera at its origin. Written in the subset of GLSL that
// GLM also compiles, as atmosphere.glsl is: render/Atmosphere.h has this source as C++ too.

#ifndef JETTISON_RINGS_GLSL
#define JETTISON_RINGS_GLSL

// One body's rings, std430: Render::GpuRings (render/FrameData.h), which PackRings fills. The
// centre is the body's, which its Atmosphere entry carries whether or not it has air.
struct Rings
{
	// xyz: the plane's normal, the body's spin axis, in render space, w: the inner radius.
	vec4 normalInner;
	// rgb: the albedo, a: the outer radius.
	vec4 colorOuter;
	// x: the opacity of the densest band, seen face on, y: the seed, z: 1 when there are rings.
	vec4 shape;
};

struct RingHit
{
	// Along the ray, negative for a ray that does not meet the plane ahead of it.
	float distance;
	// From the body's centre, where it meets the plane.
	float radius;
	// The cosine of the ray's angle to the plane's normal.
	float cosine;
};

float RingHash(float x)
{
	return fract(sin(x * 12.9898f) * 43758.5453f);
}

// Value noise along a line, eased between whole numbers.
float RingNoise(float x)
{
	float cell = floor(x);
	float f = fract(x);
	f = f * f * (3.0f - 2.0f * f);
	return mix(RingHash(cell), RingHash(cell + 1.0f), f);
}

// The share of the light the rings stop face on, at a radius from the centre. Saturn's are three
// rings, and so are these: a faint inner one (its C ring) over the first fifth of the width, a dense
// one (its B ring) out to a wide gap where the seed puts it, two thirds of the way out (its Cassini
// Division), and a lesser one beyond (its A ring) with a narrow gap of its own (its Encke Gap).
// Over them are bands of five octaves of noise. The footprint is how much of the width a pixel
// covers there; an octave whose bands are narrower than that is faded to its mean, so the bands do
// not shimmer from afar, and the three rings, which are wide, are what is left to see.
float RingCoverageAt(Rings rings, float radius, float footprint)
{
	float inner = rings.normalInner.w;
	float outer = rings.colorOuter.w;
	if (rings.shape.z == 0.0f || radius <= inner || radius >= outer)
		return 0.0f;
	float t = (radius - inner) / (outer - inner);
	float seed = rings.shape.y;
	float bands = 0.0f;
	float weight = 0.0f;
	float frequency = 7.0f;
	float amplitude = 1.0f;
	for (int octave = 0; octave < 5; ++octave)
	{
		float detail = clamp(1.0f - 2.0f * footprint * frequency, 0.0f, 1.0f);
		float offset = seed * 17.31f + float(octave) * 101.7f;
		bands += amplitude * mix(0.5f, RingNoise(t * frequency + offset), detail);
		weight += amplitude;
		frequency *= 2.6f;
		amplitude *= 0.62f;
	}
	bands = smoothstep(0.25f, 0.75f, bands / weight);

	float gap = 0.58f + 0.1f * RingHash(seed + 0.37f);
	float narrow = gap + 0.12f + 0.12f * RingHash(seed + 0.71f);
	float profile = mix(0.22f, 1.0f, smoothstep(0.17f, 0.23f, t));
	profile = mix(profile, 0.6f, smoothstep(gap - 0.02f, gap + 0.02f, t));
	profile *= 0.03f + 0.97f * smoothstep(0.02f, 0.03f, abs(t - gap));
	profile *= 0.1f + 0.9f * smoothstep(0.003f, 0.008f, abs(t - narrow));
	float edges = smoothstep(0.0f, 0.04f, t) * smoothstep(0.0f, 0.01f, 1.0f - t);
	return rings.shape.x * profile * (0.45f + 0.55f * bands) * edges;
}

// The same, averaged over the footprint, edges and gaps and the empty space past the rings
// included, where a pixel spans more of the width than the bands' fading allows for. Without it a
// pixel takes the coverage at its centre: the rings' edges seen from afar step, and their shadow
// with the sun near their plane, which every band is crowded into a strip thinner than a pixel,
// is a line of dots.
float RingCoverage(Rings rings, float radius, float footprint)
{
	if (footprint < 0.02f)
		return RingCoverageAt(rings, radius, footprint);
	float width = rings.colorOuter.w - rings.normalInner.w;
	float sum = 0.0f;
	for (int tap = 0; tap < 6; ++tap)
	{
		float offset = (float(tap) + 0.5f) / 6.0f - 0.5f;
		sum += RingCoverageAt(rings, radius + offset * footprint * width, footprint);
	}
	return sum / 6.0f;
}

// The share of the light the rings stop along a ray at the cosine given to their normal: face on it
// is the coverage, and slantwise the ray crosses more of each band, as the rings seen edge on are a
// line drawn solid.
float RingOpacity(float coverage, float cosine)
{
	return 1.0f - pow(max(1.0f - coverage, 0.0f), 1.0f / max(abs(cosine), 0.02f));
}

// Where a ray from a point meets the plane of the rings about a centre.
RingHit RingIntersect(Rings rings, vec3 centre, vec3 origin, vec3 direction)
{
	vec3 normal = vec3(rings.normalInner);
	float cosine = dot(direction, normal);
	if (rings.shape.z == 0.0f || abs(cosine) < 1e-6f)
		return RingHit(-1.0f, 0.0f, cosine);
	vec3 fromCentre = origin - centre;
	float distance = -dot(fromCentre, normal) / cosine;
	if (distance <= 0.0f)
		return RingHit(-1.0f, 0.0f, cosine);
	return RingHit(distance, length(fromCentre + direction * distance), cosine);
}

// The share of the sun's light the rings let through to a point, which spans the footprint given in
// metres (a pixel's, on the surface). The shadow's bands are as soft as the sun is wide from the
// rings, about a hundredth of their width, or softer where a pixel's way to the sun sweeps across
// more of them: with the sun near their plane, every band is in a strip a few pixels wide, and
// unsoftened they alias into dots.
float RingSunlight(Rings rings, vec3 centre, vec3 position, vec3 toSun, float footprint)
{
	RingHit hit = RingIntersect(rings, centre, position, toSun);
	if (hit.distance < 0.0f)
		return 1.0f;
	float width = rings.colorOuter.w - rings.normalInner.w;
	float swept = footprint / max(abs(hit.cosine), 0.02f) / width;
	return 1.0f - RingOpacity(RingCoverage(rings, hit.radius, max(swept, 0.01f)), hit.cosine);
}

// The share of the sun's disc a sphere leaves uncovered from a point: the shadow's edge is a
// penumbra as wide as the sun is, seen from the point, at the sphere's distance.
float SphereSunlight(vec4 centreRadius, vec3 position, vec3 toSun, float sunAngularRadius)
{
	vec3 toCentre = vec3(centreRadius) - position;
	float along = dot(toCentre, toSun);
	if (along <= 0.0f)
		return 1.0f;
	float miss = length(toCentre - toSun * along);
	float penumbra = max(along * sunAngularRadius, 1.0f);
	return smoothstep(centreRadius.w - penumbra, centreRadius.w + penumbra, miss);
}

// The light the rings send towards the camera, per unit of the sun's irradiance, where a ray along
// the direction given meets them: their particles each lit whole, so the lit face is as bright at
// any angle to the sun, less what the body's shadow takes, and the face away from the sun lit by
// the light the particles scatter through, which is less.
vec3 RingRadiance(
    Rings rings, vec4 centreRadius, vec3 position, vec3 direction, vec3 toSun,
    float sunAngularRadius)
{
	vec3 normal = vec3(rings.normalInner);
	float sameFace = dot(normal, toSun) * dot(normal, direction) <= 0.0f ? 1.0f : 0.3f;
	float lit = SphereSunlight(centreRadius, position, toSun, sunAngularRadius);
	return vec3(rings.colorOuter) * (sameFace * lit / 3.14159265f);
}

#endif
