#version 450
#extension GL_GOOGLE_include_directive : require

// The sky behind the scene, drawn last in the opaque pass over what no surface covered; and, built
// with CAPTURE as sky_capture, the same sky rendered into one face of the cube map the environment
// is prefiltered from, with the celestial bodies about the camera over it. Both run on
// fullscreen.vert's triangle.

#ifdef CAPTURE
#include "body.glsl"
#include "cube.glsl"
#include "sky.glsl"

// Render::GpuCaptureBody: a body and its surface's albedo (rgb), which the capture draws as a sphere.
struct CaptureBody
{
	Body body;
	vec4 albedo;
};

// Render::GpuCapture: the sky, the sun (xyz towards it, w 1 when there is one; rgb its irradiance,
// w its angular radius), and the bodies, furthest first, x of counts of them.
layout(set = 0, binding = 0) uniform CaptureSky
{
	Sky sky;
	vec4 sunDirection;
	vec4 sunLight;
	uvec4 counts;
	CaptureBody bodies[8];
} capture;

layout(push_constant) uniform Capture
{
	uint face;
	// The angle one texel of the face covers at its centre, in radians.
	float pixelAngle;
} face;
#else
#include "frame.glsl"
#endif

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main()
{
#ifdef CAPTURE
	// The sun is not in the capture: it lights the scene as a directional light, and a disc in the
	// environment would light it twice. The bodies are, lit by it: the planet a station is over is
	// half its sky.
	vec3 direction = CubeDirection(face.face, fragUv);
	vec3 radiance = SkyRadiance(capture.sky, direction, face.pixelAngle);
	if (capture.sunDirection.w != 0.0)
		for (uint index = 0u; index < capture.counts.x; ++index)
		{
			BodyLight light = LightAlong(
			    capture.bodies[index].body, vec4(capture.bodies[index].albedo.rgb, 1.0), direction,
			    capture.sunDirection.xyz, capture.sunLight.rgb, capture.sunLight.w, face.pixelAngle);
			radiance = light.picked + light.through * radiance;
		}
	outColor = vec4(radiance, 1.0);
#else
	// The view ray through this pixel. The view matrix is a rotation, the camera being the origin
	// of render space, so its transpose takes a view-space direction back to render space.
	vec2 ndc = fragUv * 2.0 - 1.0;
	vec3 viewRay = vec3(ndc.x / frame.projection[0][0], ndc.y / frame.projection[1][1], -1.0);
	vec3 direction = normalize(transpose(mat3(frame.view)) * viewRay);
	vec3 radiance = SkyRadiance(frame.sky, direction, frame.environment.y);

	if (frame.sunDirection.w != 0.0)
	{
		// The disc, darkened towards its limb (a linear law, u = 0.6), with an edge a pixel wide.
		float angle = acos(clamp(dot(direction, frame.sunDirection.xyz), -1.0, 1.0));
		float radius = frame.sunDisc.y;
		float edge = max(frame.environment.y, 0.000001);
		float inside = 1.0 - smoothstep(radius - edge, radius + edge, angle);
		if (inside > 0.0)
		{
			float r = min(angle / radius, 1.0);
			float limb = 1.0 - 0.6 * (1.0 - sqrt(1.0 - r * r));
			radiance += frame.sunColorIntensity.rgb * (frame.sunDisc.z * limb * inside);
		}
	}
	outColor = vec4(radiance, 1.0);
#endif
}
