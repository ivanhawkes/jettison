// A game's smallest build: include the engine through its public paths, link Jettison::Engine,
// call into two modules. It exits non-zero if either call does something other than documented.
#include <jettison/animation/Playback.h>
#include <jettison/core/Assert.h>
#include <jettison/sim/CommandQueue.h>

#include <cstdio>
#include <cstdlib>
#include <memory>

int main()
{
	Jettison::Animation::RotationAnimationComponent rotation;
	rotation.rotationSpeed = 1.0f;
	Jettison::Animation::AdvanceRotation(rotation, 0.5f);
	if (rotation.angle < 0.49f || rotation.angle > 0.51f)
		return EXIT_FAILURE;

	Jettison::Sim::CommandQueue queue;
	queue.Push(std::make_unique<Jettison::Sim::Command>());
	if (queue.Drain().size() != 1)
		return EXIT_FAILURE;

	std::puts("consumer: Jettison::Engine linked and called");
	return EXIT_SUCCESS;
}
