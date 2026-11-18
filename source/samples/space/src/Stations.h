#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/space/Cruise.h>

namespace Jettison::Space
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
struct HullDefinition;
} // namespace Jettison::Dock

namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
struct Controller;
struct Snapshot;
} // namespace Jettison::Sim

namespace Jettison::Sample
{
// The space layer's stations and ships: every station of the galaxy built to a layout is in the world
// from the start, its pads among it (Dock::Docks::MakeStation), for a game to put what it has at; and
// the ships a player commands, their fleet, one of which they may be at the helm of.

// The station the docks built for the celestial body of that name in the system, or none.
entt::entity FindStation(const entt::registry &world, std::uint64_t system, std::string_view body);

// Builds each station of the systems that names a layout the docks know, calling opened with each as
// it is built, before the next: a game parks its ships there (SpaceGame::StationOpened). A station of a
// layout the docks do not know is left out, saying so. Returns the stations built.
std::vector<entt::entity> OpenStations(
    Sim::SimWorld &world, Dock::Docks &docks, std::span<const Celestial::StarSystem> systems,
    const std::function<void(entt::entity station, std::string_view body)> &opened = {});

// A gate for each jump point of each of the systems the gates know that has none yet
// (Space::Gates::MakeGates): every gate of a new world, and every one a world saved before there were
// gates lacks. Returns the gates made, in the systems' order.
std::vector<entt::entity> OpenGates(
    Sim::SimWorld &world, const Space::Gates &gates, std::span<const Celestial::StarSystem> systems);

// Each hull's cruise drive (Space::CruiseDrive), by the hull's name: the sample's, not the docks',
// defs/drives/<hull>.yaml, each an "Ai.CruiseDrive".
using Drives = std::map<std::string, Space::CruiseDrive, std::less<>>;

inline constexpr std::string_view kDriveDirectory = "defs/drives";
inline constexpr std::uint32_t kDriveVersion = 1;

// The docks' pad classes, hulls and station layouts, and the hulls' drives, each drive checked
// (Space::CheckCruiseDrive). Throws Serialise::FormatError for a file refused.
void LoadStationDefinitions(Sim::DefinitionRegistry &definitions);

// The drives loaded, copied, so a ship is given its hull's as the definitions were at start-up.
Drives ReadDrives(const Sim::DefinitionRegistry &definitions);

// A ship of the hull, the docks' and its physics' (Dock::Docks::HullShape), as the layer makes
// them: of that mass, swept as it flies if continuous, with the default thrusters and the hull's
// cruise drive, or the default one for a hull of none.
void MakeShip(
    entt::registry &world, const Drives &drives, entt::entity ship, std::string_view hull, float mass,
    bool continuous);

// The station's lowest numbered free pad the hull fits, not a display one, or none.
entt::entity FreePad(
    const entt::registry &registry, const Dock::Docks &docks, entt::entity station,
    const Dock::HullDefinition &hull);

// The ships the controller commands (Sim::ControlledBy), in entity order, as the snapshot shows them.
std::vector<entt::entity> FleetOf(const Sim::Snapshot &snapshot, const Sim::Controller &controller);

// The ship the controller is at the helm of (Sim::Helm), or none.
entt::entity HelmOf(const Sim::Snapshot &snapshot, const Sim::Controller &controller);

} // namespace Jettison::Sample
