// A body's atmosphere (docs/architecture.md, "Atmospheres"): a shell of air over a sphere that
// scatters the sun's light once on its way to the camera, by Rayleigh scattering (the air, which
// scatters blue the most) and Mie scattering (haze, which scatters forwards, as a glow about the sun),
// each of a density falling exponentially with height. What a ray from the camera picks up through
// it, the light scattered towards the camera and the share of what is behind that gets through, is
// integrated along the ray; the sunlight's way down to each point of it, and the ray's own column of
// air, are the Chapman function, closed-form, so nothing is baked into a table, and an atmosphere is
// its parameters and a sphere drawn about its body. Multiple
// scattering is left out: the sky of a body seen from the ground is darker than it would be, and from
// orbit, where the camera is, it is close.
//
// Lengths are metres in render space, the camera at its origin. The planet program applies it over
// the surface (the haze, and the sunlight reddened at the terminator), and the atmosphere program
// over everything behind the shell that is not the surface (the limb, and the sky from inside).
//
// Written in the subset of GLSL that GLM also compiles: render/Atmosphere.h is this source as C++,
// which the render suite checks against integration by brute force.

#ifndef JETTISON_ATMOSPHERE_GLSL
#define JETTISON_ATMOSPHERE_GLSL

// One atmosphere, std430: Render::GpuAtmosphere (render/FrameData.h), which PackAtmosphere fills.
struct Atmosphere
{
	// xyz: the body's centre in render space, w: its surface's radius.
	vec4 centreRadius;
	// rgb: Rayleigh scattering at the surface, per metre, a: its scale height.
	vec4 rayleigh;
	// x: Mie scattering at the surface, per metre, y: Mie extinction, scattering and absorption, z:
	// its scale height, w: the phase function's anisotropy, from -1 (back) to 1 (forwards).
	vec4 mie;
	// x: the top's radius, past which there is taken to be no air; y: 1 when the body has an
	// atmosphere at all.
	vec4 shell;
};

// What a ray picks up through an atmosphere: the light scattered towards the camera, per unit of the
// sun's irradiance, and the share of what is behind the segment that reaches the camera.
struct AtmosphereRay
{
	vec3 inscattered;
	vec3 transmittance;
};

const float kAtmospherePi = 3.14159265f;
// Samples along each half of a ray, the half down to its lowest point and the half up from it.
const int kAtmosphereSamples = 8;
// The most a density's exponent is allowed, which a ray through the ground would pass: e^50 keeps the
// column finite in a float, and its light is all but none.
const float kAtmosphereMaxExponent = 50.0f;

// The Chapman function for a ray from a point x scale heights from the body's centre, at the cosine
// mu of its angle from the zenith, upwards or level (mu >= 0): the column of air along the ray to
// space over the vertical column above the point. For the x of a planet's air, hundreds and more, it
// is sqrt(pi x / 2) erfcx(mu sqrt(x / 2)) to a part in x; erfcx(y) is taken as
// 2 / (sqrt(pi) (y + sqrt(y^2 + g))), which is exact for g 4 / pi at y = 0 and 2 as y grows, and with
// g between them as below is within 0.04% everywhere (fitted on 2026-11-12 against Python's erfc).
// Schüler's approximation (GPU Pro 3, 2012) was tried first and was 12% short at mu = 0.1.
float ChapmanUpward(float x, float mu)
{
	float y = mu * sqrt(0.5f * x);
	float g = 2.0f - (2.0f - 4.0f / kAtmospherePi) / (1.0f + 0.84f * y + 0.48f * y * y);
	return sqrt(2.0f * x) / (y + sqrt(y * y + g));
}

// The column of air of one kind, as metres of it at the surface's density, along a ray from a point
// at the radius given, at the cosine mu of its angle from the zenith, out to space. Below the level
// the ray passes a lowest point before it climbs out: the column is the whole line's, twice the level
// column from that point, less the part behind the point, which is upwards. A ray into the ground is
// for the caller to have refused; its column is finite and vast.
float AtmosphereColumn(float radius, float mu, float surfaceRadius, float scaleHeight)
{
	float density = exp(min((surfaceRadius - radius) / scaleHeight, kAtmosphereMaxExponent));
	if (mu >= 0.0f)
		return scaleHeight * density * ChapmanUpward(radius / scaleHeight, mu);
	float lowest = radius * sqrt(max(1.0f - mu * mu, 0.0f));
	float lowestDensity = exp(min((surfaceRadius - lowest) / scaleHeight, kAtmosphereMaxExponent));
	float whole = 2.0f * lowestDensity * ChapmanUpward(lowest / scaleHeight, 0.0f);
	return scaleHeight * max(whole - density * ChapmanUpward(radius / scaleHeight, -mu), 0.0f);
}

// The share of the sun's light that reaches a point through the atmosphere, by colour, the point
// given from the body's centre: none where the body is in the way, which is a ray to the sun below
// the horizon whose lowest point is underground.
vec3 AtmosphereSunlight(Atmosphere atmosphere, vec3 up, vec3 toSun)
{
	float radius = length(up);
	float mu = dot(up, toSun) / radius;
	float surfaceRadius = atmosphere.centreRadius.w;
	if (mu < 0.0f && radius * radius * (1.0f - mu * mu) < surfaceRadius * surfaceRadius)
		return vec3(0.0f);
	float rayleighColumn = AtmosphereColumn(radius, mu, surfaceRadius, atmosphere.rayleigh.w);
	float mieColumn = AtmosphereColumn(radius, mu, surfaceRadius, atmosphere.mie.z);
	return exp(-(vec3(atmosphere.rayleigh) * rayleighColumn + vec3(atmosphere.mie.y * mieColumn)));
}

// The same for a point in render space, as a surface's lighting asks.
vec3 AtmosphereSunTransmittance(Atmosphere atmosphere, vec3 position, vec3 toSun)
{
	return AtmosphereSunlight(atmosphere, position - vec3(atmosphere.centreRadius), toSun);
}

float RayleighPhase(float cosine)
{
	return 3.0f / (16.0f * kAtmospherePi) * (1.0f + cosine * cosine);
}

// Cornette and Shanks's form of Henyey and Greenstein's, which is Rayleigh's at no anisotropy.
float MiePhase(float cosine, float anisotropy)
{
	float g2 = anisotropy * anisotropy;
	float denominator = max(1.0f + g2 - 2.0f * anisotropy * cosine, 1e-6f);
	return 3.0f / (8.0f * kAtmospherePi) * (1.0f - g2) * (1.0f + cosine * cosine) /
	       ((2.0f + g2) * denominator * sqrt(denominator));
}

// A point on the ray from the camera, at a distance along it: its radius from the body's centre, and
// the columns of each kind of air from it to space along the ray (x Rayleigh, y Mie), one way or the
// other (way 1 onwards, -1 back), so the column between two points of the ray is the difference of
// theirs. The way is the one that never meets the ground from a point of the segment: back for a ray
// that ends at the ground, since back along it is up and out; onwards for one that does not, which
// back along it might, from a camera in the air looking up.
struct AtmospherePoint
{
	float radius;
	vec2 columns;
};

AtmospherePoint AtmosphereAt(
    Atmosphere atmosphere, vec3 fromCentre, vec3 direction, float distance, float way)
{
	vec3 position = fromCentre + direction * distance;
	float radius = length(position);
	float mu = way * dot(position, direction) / radius;
	float surfaceRadius = atmosphere.centreRadius.w;
	return AtmospherePoint(
	    radius,
	    vec2(
	        AtmosphereColumn(radius, mu, surfaceRadius, atmosphere.rayleigh.w),
	        AtmosphereColumn(radius, mu, surfaceRadius, atmosphere.mie.z)));
}

vec3 AtmosphereExtinction(Atmosphere atmosphere, vec2 columns)
{
	return vec3(atmosphere.rayleigh) * columns.x + vec3(atmosphere.mie.y * columns.y);
}

// What a ray from the camera picks up through the atmosphere on its way to whatever it meets at the
// distance given (a surface, or a long way off for none). The segment is clipped to the shell and
// to the ground, which a ray stops at whether or not a mesh was drawn there, so a ray between a
// coarse mesh's silhouette and the sphere sees the limb as one that reached it would. The samples
// are bunched towards the ray's lowest point, where the air is densest: each half of the segment, to
// that point and from it, takes kAtmosphereSamples at distances growing as the square from it.
AtmosphereRay TraceAtmosphere(Atmosphere atmosphere, vec3 direction, float limit, vec3 toSun)
{
	AtmosphereRay none = AtmosphereRay(vec3(0.0f), vec3(1.0f));
	if (atmosphere.shell.y == 0.0f)
		return none;
	vec3 fromCentre = -vec3(atmosphere.centreRadius);
	float along = dot(fromCentre, direction);
	// The ray's nearest approach to the centre, taken as a vector rather than from the square of the
	// distance less the square of along, which would cancel to nothing at a planet's size in a float.
	vec3 nearest = fromCentre - direction * along;
	float nearestRadius = length(nearest);
	float top = atmosphere.shell.x;
	if (nearestRadius >= top)
		return none;
	float halfChord = sqrt((top - nearestRadius) * (top + nearestRadius));
	float enter = max(-along - halfChord, 0.0f);
	float leave = min(-along + halfChord, limit);
	float surfaceRadius = atmosphere.centreRadius.w;
	float way = 1.0f;
	if (nearestRadius < surfaceRadius)
	{
		float ground = -along - sqrt((surfaceRadius - nearestRadius) * (surfaceRadius + nearestRadius));
		if (ground > 0.0f)
		{
			leave = min(leave, ground);
			way = -1.0f;
		}
	}
	if (leave <= enter)
		return none;

	float lowest = clamp(-along, enter, leave);
	float cosine = dot(direction, toSun);
	float rayleighPhase = RayleighPhase(cosine);
	float miePhase = MiePhase(cosine, atmosphere.mie.w);
	// Each point's columns less the entry's, the right way round for the way they are taken.
	vec2 enterColumns = AtmosphereAt(atmosphere, fromCentre, direction, enter, way).columns;
	vec3 rayleighSum = vec3(0.0f);
	vec3 mieSum = vec3(0.0f);
	for (int part = 0; part < 2; ++part)
	{
		float span = part == 0 ? lowest - enter : leave - lowest;
		if (span <= 0.0f)
			continue;
		float side = part == 0 ? -1.0f : 1.0f;
		for (int index = 0; index < kAtmosphereSamples; ++index)
		{
			float u = (float(index) + 0.5f) / float(kAtmosphereSamples);
			float distance = lowest + side * span * u * u;
			float step = 2.0f * span * u / float(kAtmosphereSamples);
			AtmospherePoint point = AtmosphereAt(atmosphere, fromCentre, direction, distance, way);
			vec2 columns = max((enterColumns - point.columns) * way, vec2(0.0f));
			vec3 toCamera = exp(-AtmosphereExtinction(atmosphere, columns));
			vec3 sunlight =
			    AtmosphereSunlight(atmosphere, fromCentre + direction * distance, toSun);
			vec3 lit = toCamera * sunlight * step;
			float height = point.radius - surfaceRadius;
			rayleighSum += lit * exp(-height / atmosphere.rayleigh.w);
			mieSum += lit * exp(-height / atmosphere.mie.z);
		}
	}
	vec2 leaveColumns = AtmosphereAt(atmosphere, fromCentre, direction, leave, way).columns;
	return AtmosphereRay(
	    vec3(atmosphere.rayleigh) * rayleighSum * rayleighPhase +
	        atmosphere.mie.x * mieSum * miePhase,
	    exp(-AtmosphereExtinction(atmosphere, max((enterColumns - leaveColumns) * way, vec2(0.0f)))));
}

#endif
