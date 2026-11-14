#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Jettison::Ai
{
class Gates;
}

namespace Jettison::Celestial
{
struct StarSystem;
}

namespace Jettison::Dock
{
class Docks;
}

namespace Jettison::Sim
{
class SimWorld;
}

namespace Jettison::App
{
// The sample game's stations (docs/sample-game.md): every station of the galaxy built to a layout is
// in the world from the start, its pads among it (Dock::Docks::MakeStation), with the sample's ships
// parked on two or three pads of each, so a ship flown to one finds some taken.

// The station the docks built for the celestial body of that name in the system, or none.
entt::entity FindStation(const entt::registry &world, std::uint64_t system, std::string_view body);

// Builds each station of the systems that names a layout the docks know, and parks the sample's
// ships at it: heavy transports on three of Highport's twelve L pads, and at Corva two transports on
// M pads and a fighter on an S, each a dynamic body of its hull's box with the default thrusters. A
// station of a layout the docks do not know is left out, saying so. Returns the stations built.
std::vector<entt::entity> OpenStations(
    Sim::SimWorld &world, Dock::Docks &docks, std::span<const Celestial::StarSystem> systems);

// A gate for each jump point of each of the systems the gates know that has none yet
// (Ai::Gates::MakeGates): every gate of a new world, and every one a world saved before there were
// gates lacks. Returns the gates made, in the systems' order.
std::vector<entt::entity> OpenGates(
    Sim::SimWorld &world, const Ai::Gates &gates, std::span<const Celestial::StarSystem> systems);

// A ship of the hull, the docks' and its physics' (Dock::Docks::HullShape), as the sample makes
// them: of that mass, swept as it flies if continuous, with the default thrusters and the hull's
// cruise drive (Ai::CruiseDrive), or the default one for a hull the docks do not define.
void MakeShip(
    entt::registry &world, const Dock::Docks &docks, entt::entity ship, std::string_view hull,
    float mass, bool continuous);

} // namespace Jettison::App
