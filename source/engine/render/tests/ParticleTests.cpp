// The particles' CPU half and the shaders' own source (render/Particles.h): each emitter's share of
// the pool and its births, and particles.glsl's births and steps compiled as C++.

#include <doctest/doctest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

#include <jettison/render/Particles.h>
#include <jettison/scene/Components.h>

using namespace Jettison::Render;
using doctest::Approx;
using Jettison::Scene::ParticleEmitterComponent;

namespace
{
EmitterInput At(std::uint64_t key, const ParticleEmitterComponent &emitter, const glm::dvec3 &where)
{
	EmitterInput input;
	input.key = key;
	input.emitter = &emitter;
	input.world = glm::translate(glm::dmat4{1.0}, where);
	input.frameKey = key;
	input.frame = input.world;
	return input;
}

std::vector<EmitterDraw> Step(
    ParticlePool &pool, const std::vector<EmitterInput> &inputs, double seconds)
{
	std::vector<EmitterDraw> draws;
	pool.Update(inputs, glm::dvec3{0.0}, seconds, draws);
	return draws;
}
} // namespace

TEST_CASE("Particles: an emitter keeps its share of the pool while it is drawn, and gives it up after")
{
	ParticlePool pool{256};
	ParticleEmitterComponent small;
	small.capacity = 10;
	// A particle each on its first frame, so each is drawn: one with nothing alive is not.
	small.burst = 1;
	ParticleEmitterComponent large;
	large.capacity = 150;
	large.burst = 1;

	// Shares start at a workgroup's multiple: 64 for ten, 192 for 150.
	auto draws = Step(pool, {At(1, small, {}), At(2, large, {})}, 0.0);
	REQUIRE(draws.size() == 2);
	CHECK(draws[0].gpu.slots.x == 0u);
	CHECK(draws[0].gpu.slots.y == 10u);
	CHECK(draws[1].gpu.slots.x == 64u);
	CHECK(pool.UsedSlots() == 256u);
	// A new emitter's slots are emptied on its first frame, and not after.
	CHECK(draws[0].gpu.spawn.z == 1u);
	draws = Step(pool, {At(1, small, {}), At(2, large, {})}, 0.0);
	CHECK(draws[0].gpu.spawn.z == 0u);

	// No room for a third; it is left out, and counted.
	ParticleEmitterComponent third;
	third.capacity = 1;
	third.burst = 1;
	draws = Step(pool, {At(1, small, {}), At(2, large, {}), At(3, third, {})}, 0.0);
	CHECK(draws.size() == 2);
	CHECK(pool.Refused() == 1u);

	// The first gone, the third fits where it was, and is emptied before its births.
	draws = Step(pool, {At(2, large, {}), At(3, third, {})}, 0.0);
	REQUIRE(draws.size() == 2);
	CHECK(draws[1].gpu.slots.x == 0u);
	CHECK(draws[1].gpu.spawn.z == 1u);
	CHECK(pool.Refused() == 0u);

	// All gone, the free ranges join up again: one emitter takes the whole pool.
	Step(pool, {}, 0.0);
	CHECK(pool.UsedSlots() == 0u);
	ParticleEmitterComponent whole;
	whole.capacity = 256;
	whole.burst = 1;
	CHECK(Step(pool, {At(4, whole, {})}, 0.0).size() == 1);

	// Asking for another capacity is new slots, emptied.
	whole.capacity = 64;
	draws = Step(pool, {At(4, whole, {})}, 0.0);
	REQUIRE(draws.size() == 1);
	CHECK(draws[0].gpu.slots.y == 64u);
	CHECK(draws[0].gpu.spawn.z == 1u);
}

TEST_CASE("Particles: births follow the rate across frames, with a burst first and on each pulse")
{
	ParticlePool pool{1024};
	ParticleEmitterComponent emitter;
	emitter.rate = 30.0f;
	emitter.burst = 5;
	emitter.capacity = 100;

	std::uint32_t born = 0;
	std::uint32_t serial = 0;
	std::uint32_t cursor = 0;
	for (int frame = 0; frame < 60; ++frame)
	{
		const auto draws = Step(pool, {At(1, emitter, {})}, 1.0 / 60.0);
		REQUIRE(draws.size() == 1);
		// Each frame's births follow on from the last's, round the ring, numbered in order.
		CHECK(draws[0].gpu.spawn.x == serial);
		CHECK(draws[0].gpu.slots.z == cursor);
		born += draws[0].gpu.slots.w;
		serial += draws[0].gpu.slots.w;
		cursor = (cursor + draws[0].gpu.slots.w) % emitter.capacity;
	}
	// A second at 30 a second, and the first frame's burst; the carry may hold back the last one.
	CHECK(born >= 34u);
	CHECK(born <= 35u);

	// A pulse is another burst; an emitter that is not active makes none at its rate.
	emitter.active = false;
	emitter.pulses = 2;
	auto draws = Step(pool, {At(1, emitter, {})}, 1.0);
	CHECK(draws[0].gpu.slots.w == 10u);
	draws = Step(pool, {At(1, emitter, {})}, 1.0);
	CHECK(draws[0].gpu.slots.w == 0u);

	// No more at once than it has slots, and a long frame counts as a quarter of a second.
	emitter.active = true;
	emitter.rate = 1000.0f;
	draws = Step(pool, {At(1, emitter, {})}, 10.0);
	CHECK(draws[0].gpu.slots.w == 100u);
	CHECK(draws[0].gpu.previous.w == Approx(0.25));

	// Stopped, it is drawn while what it made lives, its second, and not after: it keeps its slots.
	emitter.active = false;
	for (int frame = 0; frame < 3; ++frame)
		CHECK(Step(pool, {At(1, emitter, {})}, 0.25).size() == 1);
	Step(pool, {At(1, emitter, {})}, 0.25);
	CHECK(Step(pool, {At(1, emitter, {})}, 0.1).empty());
	CHECK(pool.UsedSlots() == 128u);
	// Born into again, its slots are not emptied: nothing has been stepped since they were.
	emitter.pulses = 3;
	draws = Step(pool, {At(1, emitter, {})}, 0.1);
	REQUIRE(draws.size() == 1);
	CHECK(draws[0].gpu.slots.w == 5u);
	CHECK(draws[0].gpu.spawn.z == 0u);
}

TEST_CASE("Particles: an emitter is placed in its particles' frame, and moves through it")
{
	ParticlePool pool{256};
	ParticleEmitterComponent emitter;
	emitter.spread = 0.5f;
	// The frame a scaled, turned body far off; the emitter a little way from it, pointing along X.
	const glm::dvec3 centre{1.0e9, 0.0, 0.0};
	const glm::dmat4 frame = glm::scale(
	    glm::rotate(glm::translate(glm::dmat4{1.0}, centre), glm::half_pi<double>(), {0.0, 1.0, 0.0}),
	    glm::dvec3{6.0e6});
	EmitterInput input;
	input.key = 1;
	input.emitter = &emitter;
	input.frameKey = 2;
	input.frame = frame;
	input.world = glm::rotate(
	    glm::translate(glm::dmat4{1.0}, centre + glm::dvec3{0.0, 10.0, 0.0}), -glm::half_pi<double>(),
	    {0.0, 1.0, 0.0});
	const glm::dvec3 camera = centre + glm::dvec3{0.0, 0.0, 50.0};
	std::vector<EmitterDraw> draws;
	pool.Update(std::vector{input}, camera, 0.1, draws);
	REQUIRE(draws.size() == 1);
	const GpuEmitter &gpu = draws[0].gpu;
	// The frame's scale is not in it, and its centre is taken off the camera's in double precision.
	CHECK(glm::length(glm::vec3{gpu.frameToRender[0]}) == Approx(1.0));
	CHECK(gpu.frameToRender[3].z == Approx(-50.0));
	CHECK(gpu.origin.y == Approx(10.0));
	CHECK(gpu.axis.w == Approx(std::cos(0.5)));
	// Sent along the world's +X, the emitter's -Z: in the frame, turned a quarter about Y, that is +Z.
	CHECK(gpu.axis.z == Approx(1.0).epsilon(1e-6));
	CHECK(gpu.inherited.y == Approx(0.0));
	CHECK(draws[0].bounds.center.z == Approx(-50.0));

	// Moved 2 m up through its frame in a tenth of a second: born at 20 m/s, spread from where it was.
	input.world[3].y += 2.0;
	draws.clear();
	pool.Update(std::vector{input}, camera, 0.1, draws);
	CHECK(draws[0].gpu.inherited.y == Approx(20.0));
	CHECK(draws[0].gpu.previous.y == Approx(10.0));
	CHECK(draws[0].gpu.origin.y == Approx(12.0));
}

TEST_CASE("Particles: particles.glsl's births are in the cone, the sphere and the speeds given")
{
	GpuEmitter emitter;
	emitter.origin = {1.0f, 2.0f, 3.0f, 0.5f};
	emitter.previous = {1.0f, 2.0f, 3.0f, 0.0f};
	emitter.axis = {0.0f, 1.0f, 0.0f, std::cos(0.3f)};
	emitter.motion = {10.0f, 0.5f, 0.0f, 2.0f};
	emitter.inherited = {0.0f, 0.0f, 0.0f, 0.25f};
	emitter.spawn = {0u, 7u, 0u, 0u};
	double meanRandom = 0.0;
	for (std::uint32_t serial = 0; serial < 2000; ++serial)
	{
		const GpuParticle particle = SpawnParticle(emitter, serial, 0, 1);
		const glm::vec3 position{particle.positionAge};
		const glm::vec3 velocity{particle.velocityLifetime};
		CHECK(glm::length(position - glm::vec3{emitter.origin}) <= 0.5f + 1e-5f);
		const float speed = glm::length(velocity);
		CHECK(speed >= 5.0f - 1e-4f);
		CHECK(speed <= 10.0f + 1e-4f);
		CHECK(glm::dot(velocity / speed, glm::vec3{emitter.axis}) >= emitter.axis.w - 1e-5f);
		CHECK(particle.velocityLifetime.w >= 1.5f - 1e-5f);
		CHECK(particle.velocityLifetime.w <= 2.0f);
		CHECK(particle.positionAge.w == 0.0f);
		const float random = ParticleRandom(7u, serial, 0u);
		CHECK(random >= 0.0f);
		CHECK(random < 1.0f);
		meanRandom += random;
	}
	CHECK(meanRandom / 2000.0 == Approx(0.5).epsilon(0.03));
	// The same serial is the same particle, and another is not.
	CHECK(SpawnParticle(emitter, 3, 0, 1).positionAge == SpawnParticle(emitter, 3, 0, 1).positionAge);
	CHECK(SpawnParticle(emitter, 3, 0, 1).positionAge != SpawnParticle(emitter, 4, 0, 1).positionAge);

	// Births through a frame: the last at its end, the first a share of it earlier, and older.
	emitter.previous = {0.0f, 0.0f, 0.0f, 0.1f};
	emitter.origin = {1.0f, 0.0f, 0.0f, 0.0f};
	emitter.motion = {0.0f, 0.0f, 0.0f, 2.0f};
	const GpuParticle last = SpawnParticle(emitter, 0, 3, 4);
	const GpuParticle first = SpawnParticle(emitter, 1, 0, 4);
	CHECK(last.positionAge.x == Approx(1.0));
	CHECK(last.positionAge.w == Approx(0.0));
	CHECK(first.positionAge.x == Approx(0.25));
	CHECK(first.positionAge.w == Approx(0.075));
}

TEST_CASE(
    "Particles: particles.glsl steps the same path however the frames fall, and empties a slot at its "
    "lifetime")
{
	const GpuParticle start{{0.0f, 0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f, 5.0f}};
	const GpuParticle once = StepParticle(start, 1.0f, 0.7f);
	GpuParticle stepped = start;
	for (int frame = 0; frame < 10; ++frame)
		stepped = StepParticle(stepped, 0.1f, 0.7f);
	CHECK(stepped.positionAge.x == Approx(once.positionAge.x).epsilon(1e-5));
	CHECK(stepped.velocityLifetime.x == Approx(once.velocityLifetime.x).epsilon(1e-5));
	// Drag: e^-0.7 of the speed left, and the distance its integral.
	CHECK(once.velocityLifetime.x == Approx(10.0 * std::exp(-0.7)).epsilon(1e-5));
	CHECK(once.positionAge.x == Approx(10.0 * (1.0 - std::exp(-0.7)) / 0.7).epsilon(1e-5));
	// Without drag, it coasts.
	CHECK(StepParticle(start, 2.0f, 0.0f).positionAge.x == Approx(20.0));
	// At its lifetime the slot holds none.
	CHECK(StepParticle(start, 5.0f, 0.0f).velocityLifetime.w == 0.0f);
	CHECK(StepParticle(start, 4.9f, 0.0f).velocityLifetime.w == 5.0f);

	// A slot's birth counts round its ring from the frame's first.
	CHECK(BirthOf(7, 7, 10) == 0u);
	CHECK(BirthOf(2, 7, 10) == 5u);
	CHECK(BirthOf(6, 7, 10) == 9u);
}
