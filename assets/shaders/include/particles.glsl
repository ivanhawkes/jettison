// GPU particles (docs/architecture.md, "Particles"): what a particle is in the pool, what its
// emitter tells the GPU each frame, and how one is born and moves. particles.comp steps every slot of
// an emitter once a frame, and particle.vert draws them in the transparent pass.
//
// A particle lives in its emitter's frame (Scene::ParticleEmitterComponent), the entity it is at rest
// in, in metres along that entity's axes and without its scale: a station's exhaust stays where it
// was left as the station moves, and a nozzle's glow keeps to its nozzle. Positions are small numbers
// there, so they keep their millimetres as a float wherever the frame is in the system.
//
// Written in the subset of GLSL that GLM also compiles: render/Particles.h runs this source as C++,
// which the render suite checks.

#ifndef JETTISON_PARTICLES_GLSL
#define JETTISON_PARTICLES_GLSL

// A slot of the pool, std430: Render::GpuParticle (render/FrameData.h).
struct Particle
{
	// xyz: where it is in its frame, in metres; w: its age, in seconds.
	vec4 positionAge;
	// xyz: its velocity in its frame, in metres a second; w: its lifetime, 0 for a slot holding none.
	vec4 velocityLifetime;
};

// One emitter this frame, std430: Render::GpuEmitter, which Render::ParticlePool fills.
struct Emitter
{
	// The frame the particles are at rest in, to render space: a turn and a move, no scale.
	mat4 frameToRender;
	// xyz: the emitter now, in the frame; w: the radius of the sphere particles are born within.
	vec4 origin;
	// xyz: the emitter at the last frame, in the frame; w: the seconds since it.
	vec4 previous;
	// xyz: the way particles are sent, unit length, in the frame; w: the cosine of the cone's
	// half-angle.
	vec4 axis;
	// x: speed at birth, y: the share it is slowed by at random, z: drag a second, w: lifetime.
	vec4 motion;
	// xyz: the emitter's own velocity in the frame, which each particle is born with as well; w: the
	// share a lifetime is shortened by at random.
	vec4 inherited;
	// x, y: the diameter at birth and at death; z: the stretch in seconds; w: the opacity.
	vec4 shape;
	// rgb: linear colour, a: intensity, at birth and at death.
	vec4 colorStart;
	vec4 colorEnd;
	// rgb: the albedo, a: 1 when it is lit.
	vec4 albedo;
	// x: its first slot in the pool, y: its slots, z: the slot of its first birth this frame, as an
	// index into its own, w: the births this frame.
	uvec4 slots;
	// x: the serial number of its first birth this frame, y: its seed, z: 1 when its slots are to be
	// emptied before anything is born in them, which a new emitter's are.
	uvec4 spawn;
};

// The workgroup particles.comp steps an emitter's slots in.
const uint kParticleGroupSize = 64u;

// PCG's output permutation of an LCG step (Jarzynski and Olano, "Hash Functions for GPU Rendering",
// 2020): a well mixed 32 bits from 32.
uint ParticleHash(uint value)
{
	uint state = value * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

// A number from 0 to 1, below 1, for one of a particle's draws: its emitter's seed, its serial number
// and which draw it is.
float ParticleRandom(uint seed, uint serial, uint draw)
{
	uint hashed = ParticleHash(seed ^ ParticleHash(serial * 8u + draw));
	return float(hashed >> 8u) * 5.9604645e-8f;
}

// Which of this frame's births a slot of the emitter's takes, counting from the slot the first takes
// round its ring; the births or more for a slot with none.
uint BirthOf(uint slot, uint first, uint capacity)
{
	return (slot + capacity - first) % capacity;
}

// A unit vector within the cone about an axis whose half-angle has the cosine given, spread evenly
// over its cap, from two random numbers.
vec3 ConeDirection(vec3 axis, float cosHalfAngle, float u, float v)
{
	float cosTheta = 1.0f - u * (1.0f - cosHalfAngle);
	float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 0.0f));
	float phi = 6.28318531f * v;
	// Any two directions square to the axis and to each other.
	vec3 helper = abs(axis.z) < 0.9f ? vec3(0.0f, 0.0f, 1.0f) : vec3(1.0f, 0.0f, 0.0f);
	vec3 tangent = normalize(cross(helper, axis));
	vec3 bitangent = cross(axis, tangent);
	return axis * cosTheta + (tangent * cos(phi) + bitangent * sin(phi)) * sinTheta;
}

// A point within a sphere of radius one, spread evenly through it, from three random numbers.
vec3 InUnitSphere(float u, float v, float w)
{
	float z = 1.0f - 2.0f * u;
	float ring = sqrt(max(1.0f - z * z, 0.0f));
	float phi = 6.28318531f * v;
	return vec3(ring * cos(phi), ring * sin(phi), z) * pow(w, 0.33333333f);
}

// A particle moved on by some seconds under drag: its speed falls as e^(-drag t), and it goes the
// integral of that, so the path is the same however the seconds are cut into frames. One that
// reaches its lifetime is gone.
Particle StepParticle(Particle particle, float seconds, float drag)
{
	vec3 velocity = vec3(particle.velocityLifetime);
	float decay = exp(-drag * seconds);
	float travelled = drag > 0.0f ? (1.0f - decay) / drag : seconds;
	Particle moved;
	moved.positionAge = vec4(
	    vec3(particle.positionAge) + velocity * travelled, particle.positionAge.w + seconds);
	moved.velocityLifetime = vec4(velocity * decay, particle.velocityLifetime.w);
	if (moved.positionAge.w >= moved.velocityLifetime.w)
		moved.velocityLifetime.w = 0.0f;
	return moved;
}

// The birth of one of this frame's: the serial number counts every particle the emitter has made, so
// each has its own draws. The births are spread through the seconds since the last frame, the last
// at the end of them, each born where the emitter was then and moved on since, so a moving emitter
// leaves an even trail rather than a clump a frame.
Particle SpawnParticle(Emitter emitter, uint serial, uint birth, uint births)
{
	uint seed = emitter.spawn.y;
	float share = float(birth + 1u) / float(births);
	float age = (1.0f - share) * emitter.previous.w;
	vec3 at = mix(vec3(emitter.previous), vec3(emitter.origin), share) +
	          InUnitSphere(
	              ParticleRandom(seed, serial, 0u), ParticleRandom(seed, serial, 1u),
	              ParticleRandom(seed, serial, 2u)) *
	              emitter.origin.w;
	vec3 direction = ConeDirection(
	    vec3(emitter.axis), emitter.axis.w, ParticleRandom(seed, serial, 3u),
	    ParticleRandom(seed, serial, 4u));
	float speed = emitter.motion.x * (1.0f - emitter.motion.y * ParticleRandom(seed, serial, 5u));
	float lifetime =
	    emitter.motion.w * (1.0f - emitter.inherited.w * ParticleRandom(seed, serial, 6u));
	Particle born;
	born.positionAge = vec4(at, 0.0f);
	born.velocityLifetime =
	    vec4(direction * speed + vec3(emitter.inherited), max(lifetime, 1.0e-4f));
	return StepParticle(born, age, emitter.motion.z);
}

// How far through its life a particle is, from 0 to 1.
float ParticleLife(Particle particle)
{
	return clamp(particle.positionAge.w / max(particle.velocityLifetime.w, 1.0e-6f), 0.0f, 1.0f);
}

#endif
