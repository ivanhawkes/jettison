#include <jettison/app/SmokeSequence.h>

#include <doctest/doctest.h>

using Jettison::App::SmokeActions;
using Jettison::App::SmokeSequence;

namespace
{
// Drives the sequence to its close the way the shell's frame loop does, with the window manager
// answering each of the two resize requests or refusing it, and returns the verdict.
bool RunSequence(bool firstResize, bool secondResize)
{
	int requests = 0;
	int handled = 0;
	bool close = false;
	SmokeActions actions;
	actions.resize = [&](int, int) { ++requests; };
	actions.iconify = [] {};
	actions.restore = [] {};
	actions.reload = [] {};
	actions.close = [&] { close = true; };
	SmokeSequence sequence{actions, true};
	for (int frame = 0; frame < 100 && !close; ++frame)
	{
		const bool resize =
		    requests > handled && ((requests == 1 && firstResize) || (requests == 2 && secondResize));
		if (resize)
			handled = requests;
		sequence.Advance(resize, 640, 480, SmokeSequence::Clock::now());
	}
	REQUIRE_MESSAGE(close, sequence.Summary());
	INFO(sequence.Summary());
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
