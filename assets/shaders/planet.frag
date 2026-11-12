#version 450
#extension GL_GOOGLE_include_directive : require

#include "draw.glsl"
#include "frame.glsl"
#include "lighting.glsl"
#include "material.glsl"

// A planet's surface (planet.program.yaml): maps laid out by longitude and latitude, as a UV
// sphere's texture coordinates are (render/Procedural.h), with a detail layer repeated over them
// and kept to the land. Its vertex stage is mesh.vert built with PLANET, which hands on the
// direction out of the body's centre in the body's own axes (+Y the spin axis) instead of texture
// coordinates. They are worked out here per fragment: interpolated across a mesh's large triangles,
// they would pinch at the poles and lose the detail layer's precision, and the tangent frame the
// normal maps need comes from the same direction, with no tangents in the vertex.
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

const float kTwoPi = 6.28318531f;

layout(location = 3) in vec3 fragRenderPosition;
layout(location = 4) in vec3 fragBodyDirection;
layout(location = 5) flat in mat3 fragBodyToRender;

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

	Surface surface = Surface(
	    albedo, 0.0,
	    clamp(surfaceParameters.x * SampleMap(roughnessTexture, base).r, 0.0, 1.0));
	vec3 lit = ShadeSurface(surface, normalize(fragBodyToRender * bodyNormal), fragRenderPosition);
	outColor = vec4(lit, 1.0);
}
