#version 450
#extension GL_GOOGLE_include_directive : require

// The sky behind the scene, drawn last in the opaque pass over what no surface covered; and, built
// with CAPTURE as sky_capture, the same sky rendered into one face of the cube map the environment
// is prefiltered from. Both run on fullscreen.vert's triangle.

#ifdef CAPTURE
#include "cube.glsl"
#include "sky.glsl"

// Render::GpuSky.
layout(set = 0, binding = 0) uniform CaptureSky
{
	Sky sky;
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
	// environment would light it twice.
	outColor = vec4(SkyRadiance(capture.sky, CubeDirection(face.face, fragUv), face.pixelAngle), 1.0);
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
