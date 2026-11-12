#include <jettison/ai/Types.h>
#include <jettison/ai/UtilityAi.h>

#include <jettison/serialise/Archive.h>

#include <doctest/doctest.h>

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace Jettison;

namespace
{
Ai::Consideration Linear(std::string input, double low = 0.0, double high = 1.0)
{
	return {std::move(input), low, high, {}};
}

// Inputs from a table, counting what is asked for.
struct Table
{
	std::map<std::string, double, std::less<>> values;
	int asked{0};

	Ai::Inputs Inputs()
	{
		return [this](std::string_view input)
		{
			++asked;
			return values.at(std::string{input});
		};
	}
};
} // namespace

TEST_CASE("Utility AI: each curve gives its known values, clamped to [0, 1] and inverted on asking")
{
	using Ai::Curve;
	using Ai::Respond;
	using Ai::ResponseCurve;
	CHECK(Respond({}, 0.25) == doctest::Approx(0.25));
	CHECK(Respond({Curve::Linear, 2.0, 1.0, 0.25, 0.0, false}, 0.5) == doctest::Approx(0.5));
	// Past one, and below nothing, it is held there.
	CHECK(Respond({Curve::Linear, 2.0, 1.0, 0.0, 0.0, false}, 0.9) == 1.0);
	CHECK(Respond({Curve::Linear, -1.0, 1.0, 0.0, 0.5, false}, 0.9) == 0.0);
	// A quadratic falls off slowly near nothing; below its shift it is nothing, not a square.
	CHECK(Respond({Curve::Power, 1.0, 2.0, 0.0, 0.0, false}, 0.5) == doctest::Approx(0.25));
	CHECK(Respond({Curve::Power, 1.0, 2.0, 0.5, 0.0, false}, 0.25) == 0.0);
	CHECK(Respond({Curve::Power, 1.0, 0.5, 0.0, 0.0, false}, 0.25) == doctest::Approx(0.5));
	// An S about its shift.
	const ResponseCurve logistic{Curve::Logistic, 10.0, 1.0, 0.5, 0.0, false};
	CHECK(Respond(logistic, 0.5) == doctest::Approx(0.5));
	CHECK(Respond(logistic, 0.8) == doctest::Approx(1.0 / (1.0 + std::exp(-3.0))));
	CHECK(Respond(logistic, 0.0) < 0.01);
	CHECK(Respond({Curve::Step, 1.0, 1.0, 0.6, 0.0, false}, 0.59) == 0.0);
	CHECK(Respond({Curve::Step, 1.0, 1.0, 0.6, 0.0, false}, 0.6) == 1.0);
	CHECK(Respond({Curve::Linear, 1.0, 1.0, 0.0, 0.0, true}, 0.25) == doctest::Approx(0.75));

	// An input is brought into [0, 1] by its range, which may run either way.
	CHECK(Ai::Consider(Linear("profit", 0.0, 1000.0), 250.0) == doctest::Approx(0.25));
	CHECK(Ai::Consider(Linear("profit", 0.0, 1000.0), 5000.0) == 1.0);
	CHECK(Ai::Consider(Linear("time", 600.0, 0.0), 150.0) == doctest::Approx(0.75));
	CHECK(Ai::Consider(Linear("time", 600.0, 0.0), NAN) == 0.0);
}

TEST_CASE("Utility AI: a score is the product of its considerations made up for their number")
{
	Table table{{{"a", 0.5}, {"b", 0.5}, {"c", 1.0}}};
	// One consideration needs no making up.
	CHECK(Ai::Score({"one", 1.0, {Linear("a")}}, table.Inputs()) == doctest::Approx(0.5));
	// Two at a half each: 0.5 + 0.5 x 0.5 x 0.5 = 0.625 each, not 0.25 together.
	CHECK(
	    Ai::Score({"two", 1.0, {Linear("a"), Linear("b")}}, table.Inputs()) ==
	    doctest::Approx(0.625 * 0.625));
	// The weight scales it; none at all scores the weight.
	CHECK(Ai::Score({"weighed", 2.0, {Linear("a")}}, table.Inputs()) == doctest::Approx(1.0));
	CHECK(Ai::Score({"idle", 0.3, {}}, table.Inputs()) == doctest::Approx(0.3));

	// Once the score cannot beat the bar, the rest are not asked for.
	table.asked = 0;
	CHECK(
	    Ai::Score({"beaten", 1.0, {Linear("a"), Linear("c"), Linear("b")}}, table.Inputs(), 0.7) ==
	    0.0);
	CHECK(table.asked == 1);
	table.asked = 0;
	CHECK(Ai::Score({"light", 0.5, {Linear("c")}}, table.Inputs(), 0.5) == 0.0);
	CHECK(table.asked == 0);
}

TEST_CASE(
    "Utility AI: the best option is chosen, the earliest of equals, and none when all score nothing")
{
	const Ai::DecisionDefinition decision{
	    {{"trade", 1.0, {Linear("profit", 0.0, 100.0), Linear("time", 300.0, 0.0)}},
	     {"wait", 0.2, {}}}};
	Table slow{{{"profit", 80.0}, {"time", 270.0}}};
	Table quick{{{"profit", 50.0}, {"time", 30.0}}};
	Table same{{{"profit", 50.0}, {"time", 30.0}}};
	Table never{{{"profit", 0.0}, {"time", 0.0}}};

	const std::vector<Ai::Option> options{
	    {"trade", slow.Inputs()}, {"trade", quick.Inputs()}, {"trade", same.Inputs()}, {"wait", {}}};
	const auto choice = Ai::Choose(decision, options);
	REQUIRE(choice);
	CHECK(choice->option == 1);
	// 0.5 and 0.9, each made up for two: 0.625 x 0.945.
	CHECK(choice->score == doctest::Approx(0.625 * 0.945));
	// The equal one after it asked for both and did not beat it; the slow one, first, set the bar.
	CHECK(same.asked == 2);

	// Nothing to gain: the wait's baseline wins over a trade worth nothing.
	const std::vector<Ai::Option> idle{{"trade", never.Inputs()}, {"wait", {}}};
	CHECK(Ai::Choose(decision, idle)->option == 1);
	// And with no baseline, nothing is chosen.
	CHECK_FALSE(Ai::Choose(decision, std::vector<Ai::Option>{{"trade", never.Inputs()}}));
	CHECK_FALSE(Ai::Choose(decision, {}));
	CHECK_THROWS_AS(
	    (void)Ai::Choose(decision, std::vector<Ai::Option>{{"fly", {}}}), std::invalid_argument);

	// The same inputs choose the same option every time.
	for (int run = 0; run < 3; ++run)
		CHECK(Ai::Choose(decision, options)->option == 1);
}

TEST_CASE("Utility AI: a decision reads from data and is checked as it is defined")
{
	Ai::RegisterTypes();
	const auto decision = Serialise::YamlArchive::Read<Ai::DecisionDefinition>(
	    R"(version: 1
actions:
  - name: trade
    considerations:
      - input: margin
        high: 0.5
        curve: {kind: Logistic, slope: 12, shift: 0.3}
      - input: minutes
        low: 30
        high: 0
        curve: {kind: Power, exponent: 2}
  - name: wait
    weight: 0.05
)",
	    Serialise::FormatVersion{Ai::kDecisionVersion});
	REQUIRE(decision.actions.size() == 2);
	CHECK(decision.actions[0].considerations[0].curve.kind == Ai::Curve::Logistic);
	CHECK(decision.actions[0].considerations[1].low == 30.0);
	CHECK(decision.actions[1].weight == 0.05);
	CHECK_NOTHROW(Ai::CheckDecision("trader", decision));
	CHECK(
	    Serialise::YamlArchive::Read<Ai::DecisionDefinition>(
	        Serialise::YamlArchive::Write(decision, Serialise::FormatVersion{Ai::kDecisionVersion}),
	        Serialise::FormatVersion{Ai::kDecisionVersion}) == decision);

	const auto refused = [](Ai::DecisionDefinition definition, const char *problem)
	{
		CAPTURE(problem);
		CHECK_THROWS_WITH_AS(
		    Ai::CheckDecision("trader", definition), doctest::Contains(problem),
		    Serialise::FormatError);
	};
	refused({}, "has no actions");
	refused({{{"trade", 1.0, {}}, {"trade", 1.0, {}}}}, "two actions named 'trade'");
	refused({{{"", 1.0, {}}}}, "with no name");
	refused({{{"trade", -1.0, {}}}}, "which is not a weight");
	refused({{{"trade", 1.0, {Linear("")}}}}, "by an input with no name");
	refused({{{"trade", 1.0, {Linear("margin", 1.0, 1.0)}}}}, "which is not a range");
	refused(
	    {{{"trade", 1.0, {{"margin", 0.0, 1.0, {Ai::Curve::Power, 1.0, 0.0, 0.0, 0.0, false}}}}}},
	    "to the power 0");
	refused(
	    {{{"trade", 1.0, {{"margin", 0.0, 1.0, {Ai::Curve::Linear, NAN, 1.0, 0.0, 0.0, false}}}}}},
	    "not finite");
}
