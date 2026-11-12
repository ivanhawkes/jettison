#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame.glsl"
#include "post.glsl"

// Ambient occlusion (docs/architecture.md, "Ambient occlusion"): the share of the ambient light that
// reaches each pixel's opaque surface, from the depth prepass's depth and normals at sample zero,
// before the opaque pass shades. Scalable Ambient Obscurance's estimator (post.glsl) over a spiral of
// samples a view-space radius across, turned a pixel's own way in a 4 x 4 pattern that the blur after
// it takes out.
layout(set = 1, binding = 0) uniform sampler2DMS sceneDepth;
layout(set = 1, binding = 1) uniform sampler2DMS sceneNormals;

layout(push_constant) uniform Post
{
	// x: the radius in metres, y: the intensity, z: the view depth past which nothing is occluded.
	vec4 parameters;
	vec4 unused;
} post;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out float outAccess;

vec3 ViewAt(ivec2 pixel, float depth)
{
	return ViewPositionAt(
	    vec2(pixel), depth, frame.clusterTile.zw,
	    vec2(frame.projection[0][0], frame.projection[1][1]), frame.clusterDepth.x);
}

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	float depth = texelFetch(sceneDepth, pixel, 0).r;
	outAccess = 1.0;
	// The sky, at the far end of the reversed range.
	if (depth <= 0.0)
		return;
	vec3 p = ViewAt(pixel, depth);
	float radius = post.parameters.x;
	float viewDepth = -p.z;
	float screenRadius =
	    ScreenRadius(radius, viewDepth, frame.clusterTile.w, frame.projection[1][1]);
	// Under a few pixels across the samples would land on the pixel itself; and far off, a station's
	// corners are not what the eye picks out.
	if (screenRadius < 3.0 || viewDepth > post.parameters.z)
		return;
	vec3 n = normalize(texelFetch(sceneNormals, pixel, 0).xyz * 2.0 - 1.0);
	float angle = AoPixelAngle(pixel.x, pixel.y);
	ivec2 last = ivec2(frame.clusterTile.zw) - 1;
	float sum = 0.0;
	for (int index = 0; index < kAoSamples; ++index)
	{
		vec3 spiral = AoSpiralSample(index, angle);
		ivec2 at = clamp(pixel + ivec2(spiral.xy * (spiral.z * screenRadius)), ivec2(0), last);
		float sampleDepth = texelFetch(sceneDepth, at, 0).r;
		if (sampleDepth <= 0.0)
			continue;
		sum += ObscuranceSample(p, n, ViewAt(at, sampleDepth), radius);
	}
	float access = AmbientAccess(sum, float(kAoSamples), radius, post.parameters.y);
	// Faded out over the last fifth of its reach, so its end is not a line across the frame.
	float fade = clamp((post.parameters.z - viewDepth) / (0.2 * post.parameters.z), 0.0, 1.0);
	outAccess = mix(1.0, access, fade);
}
