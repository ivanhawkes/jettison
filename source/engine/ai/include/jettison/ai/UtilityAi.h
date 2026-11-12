#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Ai
{
// Utility AI for choices that are scoring problems rather than sequences (docs/architecture.md,
// "Decisions"): which trade, which route, which upgrade. A decision is data: its actions, each
// scored by considerations, each an input of the game's brought into [0, 1] by its range and shaped
// by a response curve. The game offers options, each one of the decision's actions with its own
// inputs (a trade of copper from Corva to Highport), and the engine scores them and chooses,
// deterministically: the same inputs choose the same option in every build.

enum class Curve : std::uint8_t
{
	// slope (x - shift) + offset.
	Linear,
	// slope (x - shift)^exponent + offset, with x - shift below nothing taken as nothing.
	Power,
	// 1 / (1 + e^(-slope (x - shift))) + offset: an S about the shift, as steep as the slope.
	Logistic,
	// 1 from the shift on, and nothing below it.
	Step
};

// What an input in [0, 1] is worth, in [0, 1]: the curve's value, clamped, and with invert, one
// less that. Registered with Reflect as "Ai.ResponseCurve", every field Saved and Moddable.
struct ResponseCurve
{
	Curve kind{Curve::Linear};
	double slope{1.0};
	double exponent{1.0};
	double shift{0.0};
	double offset{0.0};
	bool invert{false};

	bool operator==(const ResponseCurve &) const = default;
};

// One thing an action is weighed by: the game's input of that name, brought into [0, 1] from its
// range, low to high (a low above the high reverses it), clamped, then through the curve.
// Registered as "Ai.Consideration".
struct Consideration
{
	std::string input;
	double low{0.0};
	double high{1.0};
	ResponseCurve curve;

	bool operator==(const Consideration &) const = default;
};

// An action and what it is weighed by. Its score is the product of its considerations, each made
// up for their number (Score), times its weight: an action with no considerations scores its
// weight, a baseline the others have to beat. Registered as "Ai.UtilityAction".
struct UtilityAction
{
	std::string name;
	double weight{1.0};
	std::vector<Consideration> considerations;

	bool operator==(const UtilityAction &) const = default;
};

// A decision: defs/decisions/<name>.yaml, loaded through Sim::DefinitionRegistry, so a mod reweighs
// a trader's choices or adds one. Registered as "Ai.DecisionDefinition", Moddable.
struct DecisionDefinition
{
	std::vector<UtilityAction> actions;

	bool operator==(const DecisionDefinition &) const = default;
};

inline constexpr std::uint32_t kDecisionVersion = 1;
inline constexpr std::string_view kDecisionDirectory = "defs/decisions";

// Checks a decision, in the one place a file read and a decision made in code both go through: an
// action or more, each named once, with a weight that is not negative; each consideration naming
// an input, over a range that is not empty, with a curve whose parameters are finite and, for a
// power, an exponent above nothing. Throws Serialise::FormatError naming the decision and what does
// not fit.
void CheckDecision(std::string_view name, const DecisionDefinition &decision);

// The curve's value at x, clamped to [0, 1], inverted if it says so.
double Respond(const ResponseCurve &curve, double x);
// The consideration's value for its raw input: brought into [0, 1] by the range, then the curve's.
double Consider(const Consideration &consideration, double input);

// The game's inputs for one option, by name: what a consideration asks for. Asked only while the
// option can still win, so an input that costs (a route plotted) is not worked out for an option
// already beaten.
using Inputs = std::function<double(std::string_view input)>;

// An action's score for the inputs. Each consideration's value v is made up for their number n, as
// v + v (1 - v) (1 - 1/n), so that an action weighed by many things is not marked down for each
// one of them; the product of those, times the weight. Considerations are asked in order, and once
// what is left could not take the score above the bar (each made-up value is at most one) the rest
// are not asked and the score is nothing.
double Score(const UtilityAction &action, const Inputs &inputs, double bar = 0.0);

// One option the game offers: an action of the decision, by name, and its inputs.
struct Option
{
	std::string_view action;
	Inputs inputs;
};

struct Choice
{
	// The option's index among those offered.
	std::size_t option{0};
	double score{0.0};
};

// The option of the highest score, the earliest of equal ones, or none if every one scores
// nothing. Each is scored against the best so far (Score's bar), so an option that cannot beat it
// asks for no more of its inputs. Throws std::invalid_argument for an action the decision does not
// have.
std::optional<Choice> Choose(const DecisionDefinition &decision, std::span<const Option> options);
} // namespace Jettison::Ai
