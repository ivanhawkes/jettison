#version 450
#extension GL_GOOGLE_include_directive : require

// The particles drawn (docs/architecture.md, "Particles"): one instance a slot of an emitter's share
// of the pool, six vertices each, two triangles of a quad facing the camera. A slot holding none is
// every vertex at one point, which draws nothing. A stretched particle is drawn out behind it along
// its path, by the stretch's seconds of its motion, as a streak or a spark.

#include "draw.glsl"
#include "frame.glsl"
#include "particles.glsl"

layout(std430, set = 0, binding = 13) readonly buffer ParticlePool
{
	Particle particles[];
};

layout(std430, set = 0, binding = 14) readonly buffer EmitterList
{
	Emitter emitters[];
};

// Where in the particle the fragment is, in metres: along its streak, its head at 0 and its tail at
// -streak, and across it.
layout(location = 0) out vec2 fragLocal;
// x: its radius, y: its streak's length, both in metres.
layout(location = 1) flat out vec2 fragExtent;
// rgb: the light it adds at its middle, a: the share of what is behind it it hides there.
layout(location = 2) flat out vec4 fragColor;
layout(location = 3) out vec3 fragRenderPosition;

const vec2 kCorners[6] = vec2[6](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, 1.0));

void main()
{
	// The draw's material offset is the emitter's index, and the instance its slot, from the first
	// instance the draw gives, its first slot.
	Emitter emitter = emitters[draw.materialOffset];
	Particle particle = particles[gl_InstanceIndex];
	if (particle.velocityLifetime.w <= 0.0)
	{
		gl_Position = vec4(0.0, 0.0, -1.0, 1.0);
		fragLocal = vec2(0.0);
		fragExtent = vec2(1.0, 0.0);
		fragColor = vec4(0.0);
		fragRenderPosition = vec3(0.0);
		return;
	}
	float life = ParticleLife(particle);
	vec3 centre = (emitter.frameToRender * vec4(particle.positionAge.xyz, 1.0)).xyz;
	vec3 viewCentre = (frame.view * vec4(centre, 1.0)).xyz;
	vec3 viewVelocity =
	    mat3(frame.view) * (mat3(emitter.frameToRender) * particle.velocityLifetime.xyz);

	float radius = 0.5 * mix(emitter.shape.x, emitter.shape.y, life);
	// The streak is the particle's path over the stretch's seconds before now, in three dimensions,
	// so one coming at the camera or going away from it is drawn out in perspective, from where its
	// path points on the screen, as a light at speed is; its quad's ends are a radius past its head
	// and its tail, turned along the way the path crosses the screen.
	vec3 tail = viewCentre - viewVelocity * emitter.shape.z;
	tail.z = min(tail.z, -1.0e-3);
	float streak = length(viewCentre - tail);
	vec2 onScreen = viewCentre.xy / -viewCentre.z - tail.xy / -tail.z;
	vec2 along = length(onScreen) > 1.0e-7 ? normalize(onScreen) : vec2(1.0, 0.0);
	vec2 side = vec2(-along.y, along.x);
	vec2 corner = kCorners[gl_VertexIndex];
	bool head = corner.x > 0.0;
	vec3 end = head ? viewCentre + vec3(along * radius, 0.0) : tail - vec3(along * radius, 0.0);
	vec3 viewPosition = end + vec3(side * (corner.y * radius), 0.0);
	gl_Position = frame.projection * vec4(viewPosition, 1.0);
	float forward = head ? radius : -radius - streak;

	vec4 color = mix(emitter.colorStart, emitter.colorEnd, life);
	// A streak spreads the same light over more of the view: the share of the time a point on its path
	// is under the particle, as a moving light is in a photograph.
	float spread = radius / (radius + 0.5 * streak);
	fragColor = vec4(color.rgb * (color.a * spread), emitter.shape.w * spread);
	fragLocal = vec2(forward, corner.y * radius);
	fragExtent = vec2(radius, streak);
	fragRenderPosition = transpose(mat3(frame.view)) * viewPosition;
}
