#include <jettison/render/Particles.h>

#include <algorithm>
#include <cmath>

#include <jettison/scene/Components.h>

namespace Jettison::Render
{
// The shaders' own source, as C++: particles.glsl is written in the subset of GLSL that GLM accepts.
namespace Glsl
{
using namespace glm;
#include "particles.glsl"
} // namespace Glsl

namespace
{
Glsl::Emitter ToGlsl(const GpuEmitter &emitter)
{
	return {emitter.frameToRender, emitter.origin,    emitter.previous, emitter.axis,
	        emitter.motion,        emitter.inherited, emitter.shape,    emitter.colorStart,
	        emitter.colorEnd,      emitter.albedo,    emitter.slots,    emitter.spawn};
}

GpuParticle FromGlsl(const Glsl::Particle &particle)
{
	return {particle.positionAge, particle.velocityLifetime};
}

// The longest frame made up for.
constexpr double kMostSeconds = 0.25;

// A frame's turn and move, without its scale: each axis at unit length.
glm::dmat4 WithoutScale(const glm::dmat4 &matrix)
{
	glm::dmat4 rigid{1.0};
	for (int axis = 0; axis < 3; ++axis)
	{
		const glm::dvec3 column{matrix[axis]};
		const double length = glm::length(column);
		rigid[axis] = glm::dvec4{length > 0.0 ? column / length : glm::dvec3{0.0}, 0.0};
	}
	rigid[3] = matrix[3];
	return rigid;
}
} // namespace

ParticlePool::ParticlePool(std::uint32_t slots) : m_slots{slots}
{
	Clear();
}

void ParticlePool::Clear()
{
	m_emitters.clear();
	m_free.clear();
	if (m_slots != 0)
		m_free.push_back({0, m_slots});
	m_used = 0;
}

bool ParticlePool::Allocate(std::uint32_t count, Range &range)
{
	const std::uint32_t rounded =
	    (count + Glsl::kParticleGroupSize - 1) / Glsl::kParticleGroupSize * Glsl::kParticleGroupSize;
	for (auto free = m_free.begin(); free != m_free.end(); ++free)
	{
		if (free->count < rounded)
			continue;
		range = {free->first, rounded};
		free->first += rounded;
		free->count -= rounded;
		if (free->count == 0)
			m_free.erase(free);
		m_used += rounded;
		return true;
	}
	return false;
}

void ParticlePool::Free(const Range &range)
{
	m_used -= range.count;
	const auto after = std::ranges::lower_bound(m_free, range.first, {}, &Range::first);
	const auto at = m_free.insert(after, range);
	// Joined to the free range after it, then to the one before.
	if (const auto next = at + 1; next != m_free.end() && at->first + at->count == next->first)
	{
		at->count += next->count;
		m_free.erase(next);
	}
	if (at != m_free.begin())
		if (const auto previous = at - 1; previous->first + previous->count == at->first)
		{
			previous->count += at->count;
			m_free.erase(at);
		}
}

void ParticlePool::Update(
    std::span<const EmitterInput> inputs, const glm::dvec3 &camera, double seconds,
    std::vector<EmitterDraw> &draws)
{
	++m_frame;
	m_refused = 0;
	const double step = std::clamp(seconds, 0.0, kMostSeconds);
	m_clock += step;

	// The ones gone, and the ones asking for another capacity, give up their slots before any are
	// given, so a scene's emitters swapping round fit where they did.
	for (const EmitterInput &input : inputs)
		if (const auto found = m_emitters.find(input.key);
		    found != m_emitters.end() && found->second.capacity != input.emitter->capacity)
		{
			Free(found->second.range);
			m_emitters.erase(found);
		}
	for (const EmitterInput &input : inputs)
		if (const auto found = m_emitters.find(input.key); found != m_emitters.end())
			found->second.seen = m_frame;
	for (auto state = m_emitters.begin(); state != m_emitters.end();)
		if (state->second.seen != m_frame)
		{
			Free(state->second.range);
			state = m_emitters.erase(state);
		}
		else
			++state;

	for (const EmitterInput &input : inputs)
	{
		const Scene::ParticleEmitterComponent &emitter = *input.emitter;
		auto found = m_emitters.find(input.key);
		if (found == m_emitters.end())
		{
			State state;
			state.capacity = emitter.capacity;
			state.pulses = emitter.pulses;
			if (!Allocate(emitter.capacity, state.range))
			{
				++m_refused;
				continue;
			}
			found = m_emitters.emplace(input.key, state).first;
		}
		State &state = found->second;
		state.seen = m_frame;

		// The emitter in its particles' frame, the scale taken off the frame so a planet's six
		// million is not in every particle's metres.
		const glm::dmat4 frame = WithoutScale(input.frame);
		const glm::dmat3 turn{frame};
		const glm::dmat3 fromWorld = glm::transpose(turn);
		const glm::dvec3 at = fromWorld * (glm::dvec3{input.world[3]} - glm::dvec3{frame[3]});
		glm::dvec3 axis = -glm::dvec3{input.world[2]};
		axis = glm::length(axis) > 0.0 ? fromWorld * glm::normalize(axis) : glm::dvec3{0.0, 0.0, -1.0};
		if (state.fresh || state.frameKey != input.frameKey)
			state.previous = at;
		state.frameKey = input.frameKey;
		const glm::dvec3 velocity = step > 0.0 ? (at - state.previous) / step : glm::dvec3{0.0};

		std::uint64_t births = 0;
		if (emitter.active)
		{
			state.carry += double{emitter.rate} * step;
			const double whole = std::floor(state.carry);
			births = static_cast<std::uint64_t>(whole);
			state.carry -= whole;
			if (!state.burst)
			{
				births += emitter.burst;
				state.burst = true;
			}
		}
		else
			state.carry = 0.0;
		if (emitter.pulses != state.pulses)
		{
			births += std::uint64_t{emitter.burst} * (emitter.pulses - state.pulses);
			state.pulses = emitter.pulses;
		}
		const auto born = static_cast<std::uint32_t>(std::min<std::uint64_t>(births, state.capacity));
		// One with nothing alive is neither stepped nor drawn: a parked ship's plume costs nothing.
		// What it last made dies within its lifetime, which the jitter only shortens.
		if (born != 0)
			state.liveUntil = m_clock + double{emitter.lifetime};
		if (born == 0 && m_clock >= state.liveUntil)
		{
			state.previous = at;
			continue;
		}

		EmitterDraw draw;
		GpuEmitter &gpu = draw.gpu;
		glm::dmat4 toRender = frame;
		toRender[3] = glm::dvec4{glm::dvec3{frame[3]} - camera, 1.0};
		gpu.frameToRender = glm::mat4{toRender};
		gpu.origin = glm::vec4{glm::vec3{at}, emitter.radius};
		gpu.previous = glm::vec4{glm::vec3{state.previous}, static_cast<float>(step)};
		gpu.axis = glm::vec4{glm::vec3{axis}, std::cos(emitter.spread)};
		gpu.motion = glm::vec4{emitter.speed, emitter.speedJitter, emitter.drag, emitter.lifetime};
		gpu.inherited = glm::vec4{glm::vec3{velocity}, emitter.lifetimeJitter};
		gpu.shape = glm::vec4{emitter.size, emitter.stretch, emitter.opacity};
		gpu.colorStart = emitter.color;
		gpu.colorEnd = emitter.colorEnd;
		const bool lit = emitter.albedo.x > 0.0f || emitter.albedo.y > 0.0f || emitter.albedo.z > 0.0f;
		gpu.albedo = glm::vec4{emitter.albedo, lit ? 1.0f : 0.0f};
		gpu.slots = glm::uvec4{state.range.first, state.capacity, state.cursor, born};
		gpu.spawn = glm::uvec4{state.serial, emitter.seed, state.fresh ? 1u : 0u, 0u};

		// As far as one can get from the emitter: sent at its fastest, with the emitter's own speed
		// through the frame, which it keeps or is left behind by, for its longest life, from the edge
		// of where it is born, and drawn its widest and longest.
		const double fastest = double{emitter.speed} + glm::length(velocity);
		const double reach = fastest * double{emitter.lifetime} + double{emitter.radius} +
		                     0.5 * double{std::max(emitter.size.x, emitter.size.y)} +
		                     fastest * double{emitter.stretch};
		draw.bounds = {glm::dvec3{input.world[3]} - camera, reach};
		draws.push_back(draw);

		state.cursor = (state.cursor + born) % state.capacity;
		state.serial += born;
		state.previous = at;
		state.fresh = false;
	}
}

std::uint32_t BirthOf(std::uint32_t slot, std::uint32_t first, std::uint32_t capacity)
{
	return Glsl::BirthOf(slot, first, capacity);
}

float ParticleRandom(std::uint32_t seed, std::uint32_t serial, std::uint32_t draw)
{
	return Glsl::ParticleRandom(seed, serial, draw);
}

GpuParticle SpawnParticle(
    const GpuEmitter &emitter, std::uint32_t serial, std::uint32_t birth, std::uint32_t births)
{
	return FromGlsl(Glsl::SpawnParticle(ToGlsl(emitter), serial, birth, births));
}

GpuParticle StepParticle(const GpuParticle &particle, float seconds, float drag)
{
	return FromGlsl(
	    Glsl::StepParticle({particle.positionAge, particle.velocityLifetime}, seconds, drag));
}
} // namespace Jettison::Render
