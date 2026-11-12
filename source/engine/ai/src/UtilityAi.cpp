#include <jettison/ai/UtilityAi.h>

#include <jettison/serialise/Archive.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <string>

namespace Jettison::Ai
{
namespace
{
[[noreturn]] void Refuse(std::string_view name, const std::string &problem)
{
	throw Serialise::FormatError{std::format("Decision '{}' {}", name, problem)};
}

bool Finite(const ResponseCurve &curve)
{
	return std::isfinite(curve.slope) && std::isfinite(curve.exponent) && std::isfinite(curve.shift) &&
	       std::isfinite(curve.offset);
}
} // namespace

void CheckDecision(std::string_view name, const DecisionDefinition &decision)
{
	if (decision.actions.empty())
		Refuse(name, "has no actions");
	for (std::size_t index = 0; index < decision.actions.size(); ++index)
	{
		const UtilityAction &action = decision.actions[index];
		if (action.name.empty())
			Refuse(name, std::format("has an action {} with no name", index + 1));
		for (std::size_t other = 0; other < index; ++other)
			if (decision.actions[other].name == action.name)
				Refuse(name, std::format("has two actions named '{}'", action.name));
		if (!std::isfinite(action.weight) || action.weight < 0.0)
			Refuse(
			    name,
			    std::format("weighs '{}' at {}, which is not a weight", action.name, action.weight));
		for (const Consideration &consideration : action.considerations)
		{
			const std::string what =
			    std::format("weighs '{}' by '{}'", action.name, consideration.input);
			if (consideration.input.empty())
				Refuse(name, std::format("weighs '{}' by an input with no name", action.name));
			if (!std::isfinite(consideration.low) || !std::isfinite(consideration.high) ||
			    consideration.low == consideration.high)
				Refuse(
				    name, std::format(
				              "{} over {} to {}, which is not a range", what, consideration.low,
				              consideration.high));
			if (!Finite(consideration.curve))
				Refuse(name, std::format("{} on a curve that is not finite", what));
			if (consideration.curve.kind == Curve::Power && consideration.curve.exponent <= 0.0)
				Refuse(
				    name, std::format(
				              "{} to the power {}, which is not above nothing", what,
				              consideration.curve.exponent));
		}
	}
}

double Respond(const ResponseCurve &curve, double x)
{
	const double along = x - curve.shift;
	double value = 0.0;
	switch (curve.kind)
	{
		case Curve::Linear: value = curve.slope * along + curve.offset; break;
		case Curve::Power:
			value = curve.slope * std::pow(std::max(along, 0.0), curve.exponent) + curve.offset;
			break;
		case Curve::Logistic:
			value = 1.0 / (1.0 + std::exp(-curve.slope * along)) + curve.offset;
			break;
		case Curve::Step: value = along >= 0.0 ? 1.0 : 0.0; break;
	}
	// A NaN, from an input that is not a number, is worth nothing.
	value = std::isnan(value) ? 0.0 : std::clamp(value, 0.0, 1.0);
	return curve.invert ? 1.0 - value : value;
}

double Consider(const Consideration &consideration, double input)
{
	const double x =
	    std::clamp((input - consideration.low) / (consideration.high - consideration.low), 0.0, 1.0);
	return Respond(consideration.curve, std::isnan(x) ? 0.0 : x);
}

double Score(const UtilityAction &action, const Inputs &inputs, double bar)
{
	if (action.weight <= bar)
		return 0.0;
	const std::size_t count = action.considerations.size();
	const double makeUp = count == 0 ? 0.0 : 1.0 - 1.0 / static_cast<double>(count);
	double score = action.weight;
	for (const Consideration &consideration : action.considerations)
	{
		const double value = Consider(consideration, inputs(consideration.input));
		score *= value + value * (1.0 - value) * makeUp;
		if (score <= bar)
			return 0.0;
	}
	return score;
}

std::optional<Choice> Choose(const DecisionDefinition &decision, std::span<const Option> options)
{
	std::optional<Choice> best;
	for (std::size_t index = 0; index < options.size(); ++index)
	{
		const auto action =
		    std::ranges::find(decision.actions, options[index].action, &UtilityAction::name);
		if (action == decision.actions.end())
			throw std::invalid_argument{
			    std::format("Ai: the decision has no action '{}'", options[index].action)};
		const double score = Score(*action, options[index].inputs, best ? best->score : 0.0);
		if (score > (best ? best->score : 0.0))
			best = Choice{index, score};
	}
	return best;
}
} // namespace Jettison::Ai
