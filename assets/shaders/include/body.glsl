// A celestial body drawn with an atmosphere or rings (docs/architecture.md, "Atmospheres"), as the
// frame's body list (frame.glsl) and the environment's capture (sky.frag's CAPTURE) both carry it,
// and what a ray along a direction from the camera picks up from it: its air, its rings, and, for
// the capture, which draws no mesh, its surface lit by the sun.

#ifndef JETTISON_BODY_GLSL
#define JETTISON_BODY_GLSL

#include "atmosphere.glsl"
#include "rings.glsl"

// Render::GpuBody. A body with rings and no air has an atmosphere whose shell.y is 0, which still
// carries its centre and radius.
struct Body
{
	Atmosphere atmosphere;
	Rings rings;
};

// What a ray picks up from a body, and the share of what is behind it that gets through.
struct BodyLight
{
	vec3 picked;
	vec3 through;
};

// Along a direction from the camera, which is at the origin, with the sun's direction, irradiance
// and angular radius, and the angle a pixel covers. With a surface albedo (alpha 1), the ray that
// meets the body ends at its surface, lit as a Lambertian sphere through the air and in the rings'
// shadow; without one (alpha 0), where a mesh is drawn instead, it ends there with nothing picked
// up from the ground, as the atmosphere program wants for the rays between a coarse mesh's
// silhouette and the sphere.
BodyLight LightAlong(
    Body body, vec4 albedo, vec3 direction, vec3 toSun, vec3 sunIrradiance, float sunAngularRadius,
    float pixelAngle)
{
	vec4 centreRadius = body.atmosphere.centreRadius;
	vec3 centre = vec3(centreRadius);
	bool hasAir = body.atmosphere.shell.y != 0.0;

	// Where the ray meets the ground, if it does.
	float along = dot(centre, direction);
	float miss = length(direction * along - centre);
	float ground = -1.0;
	if (miss < centreRadius.w && along > 0.0)
		ground = along - sqrt((centreRadius.w - miss) * (centreRadius.w + miss));

	AtmosphereRay air = AtmosphereRay(vec3(0.0), vec3(1.0));
	if (hasAir)
		air = TraceAtmosphere(body.atmosphere, direction, 1.0e30, toSun);
	air.inscattered *= sunIrradiance;

	vec3 surface = vec3(0.0);
	vec3 surfaceThrough = vec3(1.0);
	if (ground > 0.0 && albedo.a != 0.0)
	{
		vec3 point = direction * ground;
		vec3 up = normalize(point - centre);
		vec3 sunlight = vec3(RingSunlight(body.rings, centre, point, toSun, pixelAngle * ground));
		if (hasAir)
			sunlight *= AtmosphereSunTransmittance(body.atmosphere, point, toSun);
		surface = vec3(albedo) / 3.14159265 * sunIrradiance * sunlight * max(dot(up, toSun), 0.0);
		surfaceThrough = vec3(0.0);
	}

	vec3 ringColor = vec3(0.0);
	float ringOpacity = 0.0;
	bool ringInFront = false;
	RingHit hit = RingIntersect(body.rings, centre, vec3(0.0), direction);
	if (hit.distance > 0.0 && (ground <= 0.0 || hit.distance < ground))
	{
		float width = body.rings.colorOuter.w - body.rings.normalInner.w;
		float footprint = pixelAngle * hit.distance / max(abs(hit.cosine), 0.02) / width;
		ringOpacity = RingOpacity(RingCoverage(body.rings, hit.radius, footprint), hit.cosine);
		ringColor = RingRadiance(
		                body.rings, centreRadius, direction * hit.distance, direction, toSun,
		                sunAngularRadius) *
		            sunIrradiance;
		// In front of the air when it is nearer than the ray's closest approach to the body.
		ringInFront = hit.distance < along;
	}

	// The surface behind the air, then the rings in front of both or between the air and what is
	// behind it.
	vec3 beyond = surface;
	vec3 beyondThrough = surfaceThrough;
	BodyLight light;
	if (ringInFront)
	{
		light.picked = ringColor * ringOpacity +
		               (1.0 - ringOpacity) * (air.inscattered + air.transmittance * beyond);
		light.through = (1.0 - ringOpacity) * air.transmittance * beyondThrough;
	}
	else
	{
		light.picked = air.inscattered + air.transmittance * (ringColor * ringOpacity +
		                                                      (1.0 - ringOpacity) * beyond);
		light.through = air.transmittance * (1.0 - ringOpacity) * beyondThrough;
	}
	return light;
}

#endif
