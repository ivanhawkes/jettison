#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include <jettison/render/Culling.h>
#include <jettison/render/FrameData.h>

namespace Jettison::Scene
{
struct ParticleEmitterComponent;
}

namespace Jettison::Render
{
// The particle emitters' CPU half (docs/architecture.md, "Particles"). The particles themselves are
// the GPU's: a pool of slots, device-local and kept from frame to frame, that particles.comp steps in
// place and particle.vert draws. What the CPU keeps is each emitter's share of the pool, a ring its
// births go round, and the count of what it has made, so it is told each frame only how many are
// born and where; nothing is read back. Nothing here touches the device, so the render suite checks
// the bookkeeping, and the shaders' own source compiled as C++ (below), without one.

// One emitter as the frame found it.
struct EmitterInput
{
	// What keeps its slots from frame to frame: its entity.
	std::uint64_t key{0};
	const Scene::ParticleEmitterComponent *emitter{};
	// The emitter's world transform, and the entity its particles are at rest in, and that entity's
	// world transform (the emitter's own, when it names none), in the system frame.
	glm::dmat4 world{1.0};
	std::uint64_t frameKey{0};
	glm::dmat4 frame{1.0};
};

// One emitter that has slots this frame: what the GPU is told, and how far its particles can reach in
// render space, which the frame culls it against and sorts it by.
struct EmitterDraw
{
	GpuEmitter gpu;
	BoundingSphere bounds;
};

class ParticlePool
{
  public:
	// The pool's slots. An emitter's share starts at a multiple of particles.comp's workgroup.
	explicit ParticlePool(std::uint32_t slots);

	// The frame's emitters, given the seconds since the last frame (clamped to a quarter of a second,
	// so a hitch is not made up for by a quarter second's births at once). An emitter keeps its slots
	// while every frame lists it, is given them when it comes (emptied before its first births) and
	// loses them in a frame that leaves it out; one asking for another capacity is given new ones. One
	// that does not fit is left out, and counted. `draws` gets one for each emitter with slots and
	// something alive in them, or born this frame, in the inputs' order, its positions taken off the
	// camera's in double precision; one with nothing alive keeps its slots, unstepped.
	void Update(
	    std::span<const EmitterInput> inputs, const glm::dvec3 &camera, double seconds,
	    std::vector<EmitterDraw> &draws);

	// Every emitter's slots given up, as a replaced scene's are.
	void Clear();

	std::uint32_t Slots() const
	{
		return m_slots;
	}
	std::uint32_t UsedSlots() const
	{
		return m_used;
	}
	// The emitters left out for want of room in the last frame.
	std::uint32_t Refused() const
	{
		return m_refused;
	}

  private:
	struct Range
	{
		std::uint32_t first{0};
		std::uint32_t count{0};
	};

	struct State
	{
		Range range;
		std::uint32_t capacity{0};
		// The slot, as an index into its own, its next birth takes; and how many it has made.
		std::uint32_t cursor{0};
		std::uint32_t serial{0};
		// The share of a birth carried from frame to frame at its rate.
		double carry{0.0};
		std::uint32_t pulses{0};
		// Its burst has been made, on its first frame active.
		bool burst{false};
		// Its slots have not yet been emptied.
		bool fresh{true};
		// The pool's clock when the last particle it made dies.
		double liveUntil{0.0};
		// Where it was in its frame at the last frame, and which frame that was.
		glm::dvec3 previous{0.0};
		std::uint64_t frameKey{0};
		std::uint64_t seen{0};
	};

	bool Allocate(std::uint32_t count, Range &range);
	void Free(const Range &range);

	std::uint32_t m_slots{0};
	std::uint32_t m_used{0};
	std::uint32_t m_refused{0};
	std::uint64_t m_frame{0};
	// The seconds the pool has stepped through.
	double m_clock{0.0};
	// Ordered by key, so which emitter is given room first is the same every run.
	std::map<std::uint64_t, State> m_emitters;
	// The free ranges, by their first slot, none touching another.
	std::vector<Range> m_free;
};

// particles.glsl compiled as C++, the source particles.comp runs.
std::uint32_t BirthOf(std::uint32_t slot, std::uint32_t first, std::uint32_t capacity);
float ParticleRandom(std::uint32_t seed, std::uint32_t serial, std::uint32_t draw);
GpuParticle SpawnParticle(
    const GpuEmitter &emitter, std::uint32_t serial, std::uint32_t birth, std::uint32_t births);
GpuParticle StepParticle(const GpuParticle &particle, float seconds, float drag);
} // namespace Jettison::Render
