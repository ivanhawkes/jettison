#include <jettison/app/Game.h>
#include <jettison/app/Reasons.h>

#include <format>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <jettison/physics/Components.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Reason.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/ui/StringTable.h>

namespace Jettison::App
{
std::string EntityName(
    const Sim::Snapshot &snapshot, const Ui::StringTable &strings, entt::entity entity,
    const Game *game)
{
	if (game)
		if (std::optional<std::string> name = game->NameEntity(snapshot, entity))
			return *std::move(name);
	if (const auto *rails = snapshot.Get<Physics::OnRails>(entity))
		return rails->body;
	return std::string{strings.Text("reason.entity")};
}


std::string WordReason(
    const Sim::Reason &reason, const Sim::Snapshot &snapshot,
    const Sim::DefinitionRegistry &definitions, const Ui::StringTable &strings, const Game *game)
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
				words.push_back(EntityName(snapshot, strings, argument.entity, game));
				break;
			case Sim::ReasonValue::Identifier:
				if (std::optional<std::string> name =
				        game ? game->NameIdentifier(
				                   argument.definition, argument.identifier, definitions, strings)
				             : std::nullopt)
					words.push_back(*std::move(name));
				else
					words.push_back(argument.identifier);
				break;
		}
	if (count && !strings.Find(reason.key) && strings.Find(std::format("{}.other", reason.key)))
		return strings.PluralArguments(reason.key, *count, words);
	return strings.FormatArguments(reason.key, words);
}
} // namespace Jettison::App
