#include "Reasons.h"

#include <format>
#include <optional>
#include <string_view>
#include <vector>

#include <jettison/physics/Components.h>
#include <jettison/sim/Reason.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/ui/StringTable.h>

#include "MarketScreen.h"
#include "Trading.h"

namespace Jettison::App
{
namespace
{
constexpr std::string_view kGood = "Shell.Good";
} // namespace

std::string EntityName(
    const Sim::Snapshot &snapshot, const Ui::StringTable &strings, entt::entity entity)
{
	if (const auto *market = snapshot.Get<Market>(entity))
		return market->station;
	if (const auto *rails = snapshot.Get<Physics::OnRails>(entity))
		return rails->body;
	return std::string{strings.Text("reason.entity")};
}

std::string WordReason(
    const Sim::Reason &reason, const Sim::Snapshot &snapshot,
    const Sim::DefinitionRegistry &definitions, const Ui::StringTable &strings)
{
	std::vector<std::string> words;
	std::optional<std::int64_t> count;
	for (const Sim::ReasonArgument &argument : reason.arguments)
		switch (argument.kind)
		{
			case Sim::ReasonValue::Integer:
				if (!count)
					count = argument.integer;
				words.push_back(strings.Number(argument.integer));
				break;
			case Sim::ReasonValue::Entity:
				words.push_back(EntityName(snapshot, strings, argument.entity));
				break;
			case Sim::ReasonValue::Identifier:
				words.push_back(
				    argument.definition == kGood ? GoodName(definitions, strings, argument.identifier)
				                                 : argument.identifier);
				break;
		}
	if (count && !strings.Find(reason.key) && strings.Find(std::format("{}.other", reason.key)))
		return strings.PluralArguments(reason.key, *count, words);
	return strings.FormatArguments(reason.key, words);
}
} // namespace Jettison::App
