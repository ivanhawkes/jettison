#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Jettison::Celestial
{
struct StarSystem;
}

namespace Jettison::Dock
{
class Docks;
struct StationLayout;
} // namespace Jettison::Dock

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

// A ship of the hull, the docks' and its physics' (Dock::Docks::HullShape), as the sample makes
// them: of that mass, swept as it flies if continuous, with the default thrusters.
void MakeShip(
    entt::registry &world, entt::entity ship, std::string_view hull, float mass, bool continuous);

// The layout drawn plainly under the station's entity in the presentation scene, as it collides: its
// decks and piers as boxes of the hull's plating, and each pad a darker plate on the deck, of its
// class's size and turned to its heading; built (Scene::BuiltComponent), so a scene file never
// holds it. The station dressing (9d) draws more.
void DrawLayout(entt::registry &scene, entt::entity station, const Dock::StationLayout &layout);

// A ship's proxy for the traffic layer as plain as the deck: a box of its hull's size in its own
// axes, beam, height and length.
entt::entity MakeHullProxy(entt::registry &scene, const glm::vec3 &size);
} // namespace Jettison::App
