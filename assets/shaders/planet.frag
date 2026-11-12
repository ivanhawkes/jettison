#version 450
#extension GL_GOOGLE_include_directive : require

#include "draw.glsl"
#include "frame.glsl"
#include "lighting.glsl"
#include "material.glsl"

// A planet's surface (planet.program.yaml): maps laid out by longitude and latitude, as a UV
// sphere's texture coordinates are (render/Procedural.h), with a detail layer repeated over them
// and kept to the land; clouds over it and the lights of its cities on its night side, both drawn
// from noise, since the pack has no maps of either; and, when the body has them (frame.glsl's body
// list), the sunlight reddened through its air at the terminator, the haze between the surface and
// the camera, its rings' shadow, and its rings where they are in front of the surface. Its vertex
// stage is mesh.vert built with PLANET, which hands on the direction out of the body's centre in
// the body's own axes (+Y the spin axis) instead of texture coordinates. They are worked out here
// per fragment: interpolated across a mesh's large triangles, they would pinch at the poles and
// lose the detail layer's precision, and the tangent frame the normal maps need comes from the
// same direction, with no tangents in the vertex.
layout(set = 1, binding = 0) uniform sampler2D colorTexture;
// Tangent space, green towards the north pole (scripts/import-planet-textures.sh).
layout(set = 1, binding = 1) uniform sampler2D normalTexture;
// Red: height, the sea floor flat at sea level.
layout(set = 1, binding = 2) uniform sampler2D heightTexture;
// Red: perceptual roughness.
layout(set = 1, binding = 3) uniform sampler2D roughnessTexture;
// Red: a factor the colour is multiplied by, about the detail's neutral value (kLand.z).
layout(set = 1, binding = 4) uniform sampler2D detailColorTexture;
layout(set = 1, binding = 5) uniform sampler2D detailNormalTexture;

// The parameters, in the descriptor's order.
const uint kBaseColor = 0u;
// x: multiplies the roughness map, y: the normal map's strength.
const uint kSurface = 1u;
// x: detail repeats around the planet, a whole number so the layer meets itself across the seam,
// y: repeats from pole to pole, z: the detail colour's strength, w: the detail normal's strength.
const uint kDetail = 2u;
// x: the height map at sea level, y: how far above it the detail is fully on, z: the detail colour
// map's mean, which leaves the colour as it is.
const uint kLand = 3u;
// x: the share of the sky clouded, 0 for none, y: how soft their edges are, z: how many of their
// largest features fit across the planet, w: how far they have drifted east over the surface, in
// turns (a Saw wave driven by time keeps them moving).
const uint kClouds = 4u;
// rgb: the colour of the night side's lights, a: their radiance at the brightest; none at 0.
const uint kNight = 5u;
// x: how many of the cities' clusters fit across the planet, y: the share of the land they cover.
const uint kCities = 6u;

const float kTwoPi = 6.28318531f;

layout(location = 3) in vec3 fragRenderPosition;
layout(location = 4) in vec3 fragBodyDirection;
layout(location = 5) flat in mat3 fragBodyToRender;
layout(location = 8) flat in uint fragBody;

layout(location = 0) out vec4 outColor;

// Where a map is read, and how fast that moves across the screen, for its mip and anisotropy.
struct MapLookup
{
	vec2 uv;
	vec2 uvDx;
	vec2 uvDy;
};

vec4 SampleMap(sampler2D map, MapLookup lookup)
{
	return textureGrad(map, lookup.uv, lookup.uvDx, lookup.uvDy);
}

// A tangent-space normal from a map whose red and green hold x and y, z rebuilt so that a
// block-compressed or filtered texel is still of unit length, x and y scaled by the strength.
vec3 SampleNormal(sampler2D map, MapLookup lookup, float strength)
{
	vec2 xy = (SampleMap(map, lookup).rg * 2.0 - 1.0) * strength;
	return vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}

float Hash(vec3 cell)
{
	cell = fract(cell * 0.3183099 + 0.1);
	cell *= 17.0;
	return fract(cell.x * cell.y * cell.z * (cell.x + cell.y + cell.z));
}

// Value noise in space, eased between the corners of a unit lattice.
float Noise(vec3 position)
{
	vec3 cell = floor(position);
	vec3 f = fract(position);
	f = f * f * (3.0 - 2.0 * f);
	return mix(
	    mix(mix(Hash(cell), Hash(cell + vec3(1, 0, 0)), f.x),
	        mix(Hash(cell + vec3(0, 1, 0)), Hash(cell + vec3(1, 1, 0)), f.x), f.y),
	    mix(mix(Hash(cell + vec3(0, 0, 1)), Hash(cell + vec3(1, 0, 1)), f.x),
	        mix(Hash(cell + vec3(0, 1, 1)), Hash(cell + vec3(1, 1, 1)), f.x), f.y),
	    f.z);
}

// Five octaves of it, from 0 to 1, each faded to its mean where its features are smaller than the
// footprint (how far across the lattice a pixel reaches), so the far side does not shimmer.
float Fbm(vec3 position, float footprint)
{
	float sum = 0.0;
	float weight = 0.0;
	float amplitude = 1.0;
	float frequency = 1.0;
	for (int octave = 0; octave < 5; ++octave)
	{
		float detail = clamp(1.5 - footprint * frequency, 0.0, 1.0);
		sum += amplitude * mix(0.5, Noise(position * frequency + float(octave) * 13.7), detail);
		weight += amplitude;
		amplitude *= 0.5;
		frequency *= 2.03;
	}
	return sum / weight;
}

// The clouds' cover at a direction out of the body's centre, in its own axes, from 0 to 1. The
// noise is warped by a coarser copy of itself, so the clouds swirl rather than sit in blobs.
float CloudCover(vec3 direction, vec4 clouds, float footprint)
{
	float turn = clouds.w * kTwoPi;
	vec3 drifted = vec3(
	    direction.x * cos(turn) + direction.z * sin(turn), direction.y,
	    direction.z * cos(turn) - direction.x * sin(turn));
	vec3 position = drifted * clouds.z;
	vec3 warp = vec3(
	    Noise(position * 0.5 + 3.1), Noise(position * 0.5 + 7.7), Noise(position * 0.5 + 11.3));
	float density = Fbm(position + warp * 1.5, footprint);
	float threshold = 1.0 - clouds.x;
	return smoothstep(threshold - clouds.y, threshold + clouds.y, density);
}

void main()
{
	vec3 direction = normalize(fragBodyDirection);

	// Longitude increases east about +Y from +X, latitude from the north pole: the UV sphere's u and
	// v. Longitude jumps a whole turn across -X, so its screen derivatives have the jump taken out
	// and the maps are read with them, or the fragments along that meridian would take the smallest
	// mip. Choosing per fragment between it and fract(longitude), whichever changes less, did not
	// work: away from the seam the two tie but for rounding, and where lavapipe's per-fragment
	// derivatives broke the tie differently within a quad, scattered fragments over the whole
	// western hemisphere read the smallest mip, the map's average colour.
	float longitude =
	    dot(direction.xz, direction.xz) > 0.0 ? atan(-direction.z, direction.x) / kTwoPi : 0.0;
	vec2 coordinates = vec2(longitude, acos(clamp(direction.y, -1.0, 1.0)) / (0.5 * kTwoPi));
	vec2 coordinatesDx = dFdx(coordinates);
	vec2 coordinatesDy = dFdy(coordinates);
	coordinatesDx.x -= round(coordinatesDx.x);
	coordinatesDy.x -= round(coordinatesDy.x);
	MapLookup base = MapLookup(coordinates, coordinatesDx, coordinatesDy);

	vec4 surfaceParameters = MaterialParameter(kSurface);
	vec4 detail = MaterialParameter(kDetail);
	vec4 land = MaterialParameter(kLand);
	MapLookup detailLookup =
	    MapLookup(coordinates * detail.xy, coordinatesDx * detail.xy, coordinatesDy * detail.xy);

	// The detail is the land's: the sea is flat at sea level in the height map.
	float height = SampleMap(heightTexture, base).r;
	float onLand = smoothstep(land.x, land.x + max(land.y, 0.0001), height);

	vec3 albedo = SampleMap(colorTexture, base).rgb * MaterialParameter(kBaseColor).rgb;
	float detailShade = SampleMap(detailColorTexture, detailLookup).r / max(land.z, 0.0001);
	albedo *= mix(1.0, detailShade, detail.z * onLand);

	// The detail normal is added to the base one's slope ("whiteout" blending), so it follows the
	// terrain under it rather than replacing it.
	vec3 normal = SampleNormal(normalTexture, base, surfaceParameters.y);
	vec3 detailNormal = SampleNormal(detailNormalTexture, detailLookup, detail.w * onLand);
	normal = normalize(vec3(normal.xy + detailNormal.xy, normal.z * detailNormal.z));

	// East, the way u increases, and north, the way v decreases. At a pole east is any direction
	// round it; the normal there is the base map's, pinched as the map is.
	vec3 east = vec3(direction.z, 0.0, -direction.x);
	east = dot(east, east) > 0.0 ? normalize(east) : vec3(0.0, 0.0, -1.0);
	vec3 north = cross(direction, east);
	vec3 bodyNormal = east * normal.x + north * normal.y + direction * normal.z;

	float roughness = clamp(surfaceParameters.x * SampleMap(roughnessTexture, base).r, 0.0, 1.0);

	// The body's centre and radius in render space, and the fragment put back on its sphere: the
	// mesh's flat triangles dip inside it, and the air's sunlight would take a point under the
	// ground to be in the planet's shadow before the terminator.
	vec3 toSun = frame.sunDirection.xyz;
	bool hasBody = fragBody != 0u;
	Body body;
	if (hasBody)
		body = bodies[fragBody - 1u];
	vec3 centre = hasBody ? vec3(body.atmosphere.centreRadius) : vec3(0.0);
	vec3 surfacePoint = fragRenderPosition;
	if (hasBody)
		surfacePoint = centre + normalize(fragRenderPosition - centre) * body.atmosphere.centreRadius.w;
	vec3 renderUp = normalize(fragBodyToRender * direction);

	// The clouds: white over the surface, rough, and smooth in shape; and their shadow, where the
	// cover is between the ground and the sun, a little way towards it.
	vec4 clouds = MaterialParameter(kClouds);
	float footprint = length(fwidth(direction)) * clouds.z;
	float cover = 0.0;
	float cloudShadow = 1.0;
	if (clouds.x > 0.0)
	{
		cover = CloudCover(direction, clouds, footprint);
		vec3 sunInBody = transpose(fragBodyToRender) * toSun;
		vec3 shadowDirection = normalize(direction + sunInBody * 0.004);
		cloudShadow = 1.0 - 0.6 * CloudCover(shadowDirection, clouds, footprint);
	}
	albedo = mix(albedo, vec3(0.85), cover);
	roughness = mix(roughness, 1.0, cover);
	vec3 renderNormal = normalize(mix(fragBodyToRender * bodyNormal, renderUp, cover));

	vec3 sunlight = vec3(cloudShadow * (1.0 - cover) + cover);
	if (hasBody)
	{
		if (body.atmosphere.shell.y != 0.0)
			sunlight *= AtmosphereSunTransmittance(body.atmosphere, surfacePoint, toSun);
		sunlight *= RingSunlight(
		    body.rings, centre, surfacePoint, toSun, length(fwidth(fragRenderPosition)));
	}

	Surface surface = Surface(albedo, 0.0, roughness);
	vec3 lit = ShadeSurfaceSunlit(surface, renderNormal, fragRenderPosition, sunlight);

	// The cities' lights: clusters on the land, on the side turned from the sun, coming on over the
	// terminator's last few degrees and dimmed by the cloud over them.
	vec4 night = MaterialParameter(kNight);
	if (night.a > 0.0)
	{
		vec4 cities = MaterialParameter(kCities);
		float cityFootprint = length(fwidth(direction)) * cities.x;
		// The regions people live in, fading out at their edges; within them, towns at six times the
		// scale, and within those, streets of light at thirty, each faded to its mean from afar, where
		// a region is a glow.
		float region = smoothstep(
		    1.0 - cities.y - 0.1, 1.0 - cities.y + 0.15, Fbm(direction * cities.x, cityFootprint));
		float towns = smoothstep(
		    0.42, 0.72, Fbm(direction * cities.x * 6.0 + 5.3, cityFootprint * 6.0));
		float streets =
		    smoothstep(0.35, 0.8, Noise(direction * cities.x * 30.0 + 2.1)) * 0.8 + 0.2;
		float lights = region * towns * mix(streets, 0.6, clamp(cityFootprint * 30.0 - 1.0, 0.0, 1.0));
		// A glow about each town, a little wider than its lights and a tenth as bright, as the air
		// over a city scatters its light: until the post chain's bloom (phase 10c), it is also what
		// makes the lights read as bright.
		float glow = region * smoothstep(
		    0.35, 0.72, Fbm(direction * cities.x * 6.0 + 5.3, cityFootprint * 6.0 + 1.5));
		lights += 0.1 * glow;
		float dark = smoothstep(0.05, -0.15, dot(renderUp, toSun));
		lit += night.rgb * night.a * lights * onLand * dark * (1.0 - 0.85 * cover);
	}

	if (hasBody && frame.sunDirection.w != 0.0)
	{
		vec3 sunIrradiance = frame.sunColorIntensity.rgb * frame.sunColorIntensity.a;
		float distance = length(fragRenderPosition);
		vec3 view = fragRenderPosition / distance;
		if (body.atmosphere.shell.y != 0.0)
		{
			AtmosphereRay ray = TraceAtmosphere(body.atmosphere, view, distance, toSun);
			lit = lit * ray.transmittance + ray.inscattered * sunIrradiance;
		}
		// The rings, where the camera's ray meets them before the surface.
		RingHit hit = RingIntersect(body.rings, centre, vec3(0.0), view);
		if (hit.distance > 0.0 && hit.distance < distance)
		{
			float width = body.rings.colorOuter.w - body.rings.normalInner.w;
			float ringFootprint =
			    frame.environment.y * hit.distance / max(abs(hit.cosine), 0.02) / width;
			float opacity = RingOpacity(RingCoverage(body.rings, hit.radius, ringFootprint), hit.cosine);
			vec3 ring = RingRadiance(
			    body.rings, body.atmosphere.centreRadius, view * hit.distance, view, toSun,
			    frame.sunDisc.y);
			lit = mix(lit, ring * sunIrradiance, opacity);
		}
	}
	outColor = vec4(lit, 1.0);
}
