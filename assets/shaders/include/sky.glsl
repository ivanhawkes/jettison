// The sky of a star system: a starfield, the galaxy's band and a nebula, all procedural from the
// scene's sky (Render::SkyComponent, packed by Render::PackSky). sky.frag draws it behind the
// scene, and the same function, built as sky_capture, renders it into the cube map the environment
// lighting is prefiltered from, so what a surface reflects is what is drawn behind it.
//
// Nothing here depends on the screen's resolution except through the pixel angle the caller
// passes: a star is a Gaussian at least a pixel wide whose integral is the star's flux, so it
// neither flickers as the camera turns nor brightens as the resolution falls.

#ifndef JETTISON_SKY_GLSL
#define JETTISON_SKY_GLSL

const float kSkyPi = 3.14159265;

// Render::GpuSky (render/vulkan/FrameData.h), std140 and std430 alike.
struct Sky
{
	// x: the radius, in lattice cells, of the shell the stars are on, y: the brightest star's flux,
	// z: the widest a star is drawn, in radians, w: one when there are stars.
	vec4 stars;
	// xyz: the galactic plane's normal, w: the band's angular half-width, in radians.
	vec4 band;
	// rgb: the band's radiance at its centre, a: unused.
	vec4 bandColor;
	// rgb: the nebula's radiance, a: how many times its pattern repeats around the sky.
	vec4 nebula;
	// x: the seed every hash is offset by.
	uvec4 seed;
};

uvec3 SkyHash(uvec3 value)
{
	// PCG3D (Jarzynski and Olano, "Hash Functions for GPU Rendering", 2020).
	value = value * 1664525u + 1013904223u;
	value.x += value.y * value.z;
	value.y += value.z * value.x;
	value.z += value.x * value.y;
	value ^= value >> 16u;
	value.x += value.y * value.z;
	value.y += value.z * value.x;
	value.z += value.x * value.y;
	return value;
}

vec3 SkyRandom(ivec3 cell, uint seed)
{
	return vec3(SkyHash(uvec3(cell) + uvec3(seed, seed * 7919u, seed * 104729u))) *
	       (1.0 / 4294967296.0);
}

float SkyValueNoise(vec3 position, uint seed)
{
	ivec3 cell = ivec3(floor(position));
	vec3 f = fract(position);
	vec3 u = f * f * (3.0 - 2.0 * f);
	float n000 = SkyRandom(cell, seed).x;
	float n100 = SkyRandom(cell + ivec3(1, 0, 0), seed).x;
	float n010 = SkyRandom(cell + ivec3(0, 1, 0), seed).x;
	float n110 = SkyRandom(cell + ivec3(1, 1, 0), seed).x;
	float n001 = SkyRandom(cell + ivec3(0, 0, 1), seed).x;
	float n101 = SkyRandom(cell + ivec3(1, 0, 1), seed).x;
	float n011 = SkyRandom(cell + ivec3(0, 1, 1), seed).x;
	float n111 = SkyRandom(cell + ivec3(1, 1, 1), seed).x;
	return mix(
	    mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
	    mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

// Four octaves, from 0 to about 1.
float SkyFbm(vec3 position, uint seed)
{
	float sum = 0.0;
	float amplitude = 0.5;
	for (int octave = 0; octave < 4; ++octave)
	{
		sum += amplitude * SkyValueNoise(position, seed + uint(octave));
		position = position * 2.03 + vec3(17.0, 31.0, 7.0);
		amplitude *= 0.5;
	}
	return sum / 0.9375;
}

// How far into the galaxy's band a direction is: one on its centre line, falling off over its width.
float SkyBandWeight(Sky sky, vec3 direction)
{
	float height = dot(direction, sky.band.xyz) / max(sky.band.w, 0.0001);
	return exp(-height * height);
}

// The stars sit one to a cell of an integer lattice, and the ones within a quarter cell of the
// sphere of the lattice's radius are the sky's: a direction finds them in the eight cells around
// the point it meets that sphere at. A quarter cell of shell and a footprint capped at a fifth of a
// cell keep every star a direction can see inside those eight.
vec3 SkyStars(Sky sky, vec3 direction, float pixelAngle)
{
	float radius = sky.stars.x;
	vec3 point = direction * radius;
	ivec3 base = ivec3(floor(point - 0.5));
	float sigma = clamp(pixelAngle * 0.7, 0.000001, sky.stars.z);
	float twoSigma2 = 2.0 * sigma * sigma;
	vec3 total = vec3(0.0);
	for (int corner = 0; corner < 8; ++corner)
	{
		ivec3 cell = base + ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
		vec3 star = vec3(cell) + SkyRandom(cell, sky.seed.x);
		float distanceToCentre = length(star);
		if (abs(distanceToCentre - radius) >= 0.25)
			continue;
		// The angle to the star, squared, from the chord: exact enough at a fraction of a degree.
		vec3 offset = star / distanceToCentre - direction;
		float angle2 = dot(offset, offset);
		vec3 traits = SkyRandom(cell, sky.seed.x + 0x9E3779B9u);
		// Many faint stars and a few bright ones, brighter in the band; cool stars to hot ones.
		float crowding = 0.6 + 1.4 * SkyBandWeight(sky, star / distanceToCentre);
		float flux = sky.stars.y * pow(traits.x, 14.0) * crowding;
		vec3 tint = mix(vec3(1.0, 0.72, 0.5), vec3(0.72, 0.84, 1.0), traits.y);
		total += tint * (flux * exp(-angle2 / twoSigma2) / (kSkyPi * twoSigma2));
	}
	return total;
}

// The radiance the sky sends along -direction towards the viewer, before the sun, which the caller
// adds where it draws one. pixelAngle is the angle one pixel of the target covers, in radians.
vec3 SkyRadiance(Sky sky, vec3 direction, float pixelAngle)
{
	vec3 radiance = vec3(0.0);
	float band = SkyBandWeight(sky, direction);
	if (band > 0.001)
	{
		// Dust lanes: the band is darkened where a noise field is high, most along its centre line.
		float dust = SkyFbm(direction * 3.0, sky.seed.x + 11u);
		float glow = SkyFbm(direction * 7.0, sky.seed.x + 23u);
		radiance += sky.bandColor.rgb * band * (0.45 + 0.9 * glow) *
		            (1.0 - 0.75 * smoothstep(0.45, 0.75, dust) * band);
	}
	if (sky.nebula.r + sky.nebula.g + sky.nebula.b > 0.0)
	{
		float cloud = SkyFbm(direction * sky.nebula.a, sky.seed.x + 41u);
		float density = smoothstep(0.42, 0.85, cloud);
		radiance += sky.nebula.rgb * density * density;
	}
	if (sky.stars.w != 0.0)
		radiance += SkyStars(sky, direction, pixelAngle);
	return radiance;
}

#endif
