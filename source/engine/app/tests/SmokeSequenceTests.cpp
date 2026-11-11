#include <jettison/app/SmokeSequence.h>

#include <doctest/doctest.h>

using Jettison::App::SmokeActions;
using Jettison::App::SmokeSequence;

namespace
{
// Drives the sequence to its close the way the shell's frame loop does, with the window manager
// answering each of the two resize requests or refusing it, and a jump that takes that many frames
// (none started, when negative), and returns the verdict.
bool RunSequence(bool firstResize, bool secondResize, int jumpFrames = 3)
{
	int requests = 0;
	int handled = 0;
	int jumps = 0;
	int jumpLeft = 0;
	bool close = false;
	SmokeActions actions;
	actions.resize = [&](int, int) { ++requests; };
	actions.iconify = [] {};
	actions.restore = [] {};
	actions.reload = [] {};
	actions.jump = [&]
	{
		if (jumpFrames < 0)
			return false;
		++jumps;
		jumpLeft = jumpFrames;
		return true;
	};
	actions.jumping = [&] { return jumpLeft > 0; };
	actions.close = [&]
	{
		// Nothing closes the window while a jump is under way.
		CHECK(jumpLeft == 0);
		close = true;
	};
	SmokeSequence sequence{actions, true};
	for (int frame = 0; frame < 200 && !close; ++frame)
	{
		if (jumpLeft > 0)
			--jumpLeft;
		const bool resize =
		    requests > handled && ((requests == 1 && firstResize) || (requests == 2 && secondResize));
		if (resize)
			handled = requests;
		sequence.Advance(resize, 640, 480, SmokeSequence::Clock::now());
	}
	REQUIRE_MESSAGE(close, sequence.Summary());
	INFO(sequence.Summary());
	CHECK(jumps == (jumpFrames < 0 ? 0 : 1));
	return sequence.Passed();
}
} // namespace

TEST_CASE("Smoke sequence: one handled resize of the two requested is a pass")
{
	CHECK(RunSequence(true, true));
	CHECK(RunSequence(false, true));
	CHECK(RunSequence(true, false));
}

TEST_CASE("Smoke sequence: no handled resize is a failure")
{
	CHECK_FALSE(RunSequence(false, false));
}

TEST_CASE("Smoke sequence: the jump is waited for however long it takes, and a run without one fails")
{
	CHECK(RunSequence(true, true, 0));
	CHECK(RunSequence(true, true, 60));
	CHECK_FALSE(RunSequence(true, true, -1));
}
