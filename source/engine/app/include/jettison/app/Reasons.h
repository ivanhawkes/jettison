#pragma once

#include <entt/entt.hpp>

#include <string>

namespace Jettison::Sim
{
class DefinitionRegistry;
struct Reason;
struct Snapshot;
} // namespace Jettison::Sim
namespace Jettison::Ui
{
class StringTable;
}

namespace Jettison::App
{
class Game;

// What the player reads for a reason the step gave (Sim::Reason), in the string table's language:
// the text under its key, its arguments by position, each in words first: an integer as the
// language writes numbers (StringTable::Number), credits among them; an entity by its name in the
// snapshot (EntityName); an identifier by its name where the game names it (Game::NameIdentifier),
// a hull by its definition's name, and any other as the data gives it. A key whose text has plural forms (key.one, key.other) chooses one by its first integer
// argument. A key no table has reads as itself, and counts among the table's problems.
std::string WordReason(
    const Sim::Reason &reason, const Sim::Snapshot &snapshot,
    const Sim::DefinitionRegistry &definitions, const Ui::StringTable &strings,
    const Game *game = nullptr);

// An entity's name in the snapshot, for a reason: the game's for it (Game::NameEntity), or the body a
// station or a gate rides the rails of (Physics::OnRails, which the shell shows in snapshots); an
// entity with neither, or one the snapshot does not have, reads as reason.entity.
std::string EntityName(
    const Sim::Snapshot &snapshot, const Ui::StringTable &strings, entt::entity entity,
    const Game *game = nullptr);
} // namespace Jettison::App
