#include "SmokeSequence.h"

#include <iostream>

using Jettison::Renderer::SmokeActions;
using Jettison::Renderer::SmokeSequence;

bool CheckSequence(bool firstResize, bool secondResize, bool expected)
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
	if (!close || sequence.Passed() != expected)
	{
		std::cerr << "Unexpected smoke sequence result: " << sequence.Summary() << '\n';
		return false;
	}
	return true;
}

int main()
{
	return CheckSequence(true, true, true) && CheckSequence(false, true, true) &&
	               CheckSequence(true, false, true) && CheckSequence(false, false, false)
	           ? 0
	           : 1;
}