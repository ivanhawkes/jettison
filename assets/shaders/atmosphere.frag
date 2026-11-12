#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame.glsl"

// The bodies' air and rings over the sky (docs/architecture.md, "Atmospheres"): drawn on
// fullscreen.vert's triangle after the sky, at the far end of the depth range, so only where no
// surface was drawn. The limb about a planet, the sky from inside its air and its rings over space
// are here; the planet program hazes its own surface and draws the rings in front of it. What a
// pixel shows is what it picks up, plus what is behind it times what gets through, which is blended
// as premultiplied alpha: the colour picked up, and one minus the mean of what gets through, so the
// stars behind the limb are dimmed alike in every colour rather than reddened.

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main()
{
	if (frame.sunDirection.w == 0.0)
	{
		outColor = vec4(0.0);
		return;
	}
	vec2 ndc = fragUv * 2.0 - 1.0;
	vec3 viewRay = vec3(ndc.x / frame.projection[0][0], ndc.y / frame.projection[1][1], -1.0);
	vec3 direction = normalize(transpose(mat3(frame.view)) * viewRay);
	vec3 toSun = frame.sunDirection.xyz;
	vec3 sunIrradiance = frame.sunColorIntensity.rgb * frame.sunColorIntensity.a;

	// Each body's picked up, over the ones before it times what it lets through. The bodies are not
	// sorted: two whose air or rings overlap on the screen are rare, and then the order is wrong
	// only in which is in front.
	vec3 picked = vec3(0.0);
	vec3 through = vec3(1.0);
	for (uint index = 0u; index < frame.counts.w; ++index)
	{
		BodyLight light = LightAlong(
		    bodies[index], vec4(0.0), direction, toSun, sunIrradiance, frame.sunDisc.y,
		    frame.environment.y);
		picked = light.picked + light.through * picked;
		through *= light.through;
	}
	outColor = vec4(picked, 1.0 - (through.r + through.g + through.b) / 3.0);
}
