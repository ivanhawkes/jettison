#include <jettison/app/SmokeSequence.h>

#include <doctest/doctest.h>

#include <optional>

using Jettison::App::SmokeActions;
using Jettison::App::SmokeSequence;

namespace
{
// The market's screen shows what the snapshot has unless screenWrong, and its trade is answered two
// frames on unless never, and shown unless answerHidden; none of it where there is no market.
struct Market
{
	bool present{true};
	bool screenWrong{false};
	bool never{false};
	bool answerHidden{false};
};


// Drives the sequence to its close the way the shell's frame loop does, with the window manager
// answering each of the two resize requests or refusing it, and each piloting stage that many frames
// showing what it flew moved (over once it starts, for none), and returns the verdict.
bool RunSequence(
    bool firstResize, bool secondResize, bool saveLoads = true, Market market = {},
    std::optional<int> pilotFrames = std::nullopt)
{
	int requests = 0;
	int handled = 0;
	int pilots = 0;
	int pilotLeft = 0;
	bool close = false;
	SmokeActions actions;
	actions.resize = [&](int, int) { ++requests; };
	actions.iconify = [] {};
	actions.restore = [] {};
	actions.reload = [] {};

	// The chase camera, then the cockpit.
	actions.pilot = [&](bool cockpit)
	{
		CHECK(cockpit == (pilots == 1));

		// The second waits for the first to have shown.
		CHECK(pilotLeft == 0);
		++pilots;
		pilotLeft = pilotFrames.value_or(0);
		return true;
	};
	if (pilotFrames)
		actions.piloting = [&] { return pilotLeft > 0; };

	// Answered two frames on, after both pilot stages.
	int picks = 0;
	int pickLeft = 0;
	actions.pick = [&]
	{
		CHECK(pilots == 2);
		CHECK(pilotLeft == 0);
		++picks;
		pickLeft = 2;
		return true;
	};
	actions.picking = [&] { return pickLeft > 0; };

	// After the pick is answered.
	int saves = 0;
	actions.saveAndLoad = [&]
	{
		CHECK(picks == 1);
		CHECK(pickLeft == 0);
		++saves;
		return saveLoads;
	};

	// After the save.
	int trades = 0;
	int tradeLeft = 0;
	actions.gameUi = [&]() -> std::optional<bool>
	{
		CHECK(saves == 1);
		if (!market.present)
			return std::nullopt;
		++trades;
		tradeLeft = market.never ? 1000 : 2;
		return !market.screenWrong;
	};
	actions.gameUiWaiting = [&] { return tradeLeft > 0; };
	actions.gameUiAnswered = [&] { return !market.answerHidden; };
	actions.close = [&] { close = true; };
	SmokeSequence sequence{actions, true};
	for (int frame = 0; frame < 300 && !close; ++frame)
	{
		if (pilotLeft > 0)
			--pilotLeft;
		if (pickLeft > 0)
			--pickLeft;
		if (tradeLeft > 0)
			--tradeLeft;
		const bool resize =
		    requests > handled && ((requests == 1 && firstResize) || (requests == 2 && secondResize));
		if (resize)
			handled = requests;
		sequence.Advance(resize, 640, 480, SmokeSequence::Clock::now());
	}
	REQUIRE_MESSAGE(close, sequence.Summary());
	INFO(sequence.Summary());
	CHECK(pilots == 2);
	CHECK(picks == 1);
	CHECK(saves == 1);
	CHECK(trades == (market.present ? 1 : 0));
	CHECK(sequence.Summary().find("picks=1") != std::string::npos);
	CHECK(sequence.Summary().find(saveLoads ? "saves=1" : "saves=0") != std::string::npos);
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


TEST_CASE("Smoke sequence: a save that does not load back as it was is a failure")
{
	CHECK_FALSE(RunSequence(true, true, false));
}


TEST_CASE("Smoke sequence: a market screen has to show the snapshot's market and its trade's answer")
{
	CHECK(RunSequence(true, true, true, {}));

	// A system with no market is not a market tested, and not a failure.
	CHECK(RunSequence(true, true, true, {.present = false}));
	CHECK_FALSE(RunSequence(true, true, true, {.screenWrong = true}));
	CHECK_FALSE(RunSequence(true, true, true, {.never = true}));
	CHECK_FALSE(RunSequence(true, true, true, {.answerHidden = true}));
}


TEST_CASE("Smoke sequence: a piloting stage waits for what it flew to show it moved")
{
	CHECK(RunSequence(true, true, true, {}, 0));
	CHECK(RunSequence(true, true, true, {}, 40));
	CHECK(RunSequence(true, true, true, {.present = false}, 40));
}
