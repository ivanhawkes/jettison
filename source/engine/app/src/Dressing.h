#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <jettison/dock/Docks.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::App
{
// The station dressing (docs/roadmap.md, 9d): a station's layout drawn under its entity in the
// presentation scene, built (Scene::BuiltComponent), so a scene file never holds it. None of it is
// the station's collision mesh, which is its layout's decks and piers (Dock::DeckShape) whatever is
// drawn; the lines and lamps are in the effects layer, which a station of no layout leaves out of
// its mesh too (Piloting.h).
//
//   - Its decks and piers as boxes, a tall block (the hub) in the hull's plating with its windows and
//     a flat one (a pier, a wing) in deck plating.
//   - Each pad a darker plate on the deck, turned to its heading and of its class's size, painted
//     with its outline, a centre line with chevrons to the bow, the keep-clear box over the hatch of
//     its class's largest hull, and its number in bar segments, readable from above with the bow up.
//   - Floodlight poles along each pad's long sides, at its corners and, for a pad longer than
//     kPoleSpacing, between them, each a spot aimed across the pad at half its class's largest
//     hull's height, and a lamp under its head showing the pad's state (LampColour).
//   - Each cargo bay's door: a panel in a wall up from its sill, or a hatch in the deck, with a
//     frame and a lamp strip.
//   - On each display pad, the ship it shows (Dock::PadLayout::ship), as a hull proxy.
//   - The roads (Dock::Roads), a lane divider painted down each between its nodes, and, for a
//     station whose buggies are parked, its buggies in a row across each pad's stern.
struct StationDressing
{
	// The station's entity in the scene, and the celestial body it is.
	entt::entity station{entt::null};
	std::string body;
	// Each pad's lamps, by its number less one.
	std::vector<std::vector<entt::entity>> lamps;
	std::size_t floodlights{0};
	// The roads painted, and the buggies parked.
	std::size_t roads{0};
	std::size_t parked{0};
};

// How far apart a pad's floodlight poles stand along its long sides, at most: two a side for an S or
// an M pad, three for an L and four for an XL.
inline constexpr float kPoleSpacing = 120.0f;

// Dresses the station's entity with its layout. A display pad's ship is looked up in the docks; one
// they do not know, or too large for its pad, is left off, saying so.
StationDressing DressStation(
    entt::registry &scene, entt::entity station, const Dock::StationLayout &layout,
    const Dock::Docks &docks);

// A pad's lamp: green free, amber reserved, red holding a ship, and blue for a display pad. A
// reserved pad's lamps also blink (materials/lamp-blinking.material.yaml).
glm::vec3 LampColour(Dock::PadState state, bool display);

// Turns each lamp of the dressing to its pad's state, from the pads in a snapshot that are of the
// station's simulation entity. Returns how many of its pads it found: none, when the station is not
// that entity (a world loaded over the one it was found in), and the caller finds it again.
std::size_t ShowPadStates(
    entt::registry &scene, const StationDressing &dressing,
    std::span<const Sim::ComponentSample<Dock::Pad>> pads, entt::entity simStation);

// A ship's proxy for the traffic layer, in its own axes (nose -Z): the hull's model, if it names
// one, or its class's proxy made of procedural shapes, fitted to its length, beam and height, so the
// box it collides as holds what is drawn. S is the shell's shuttle, M a transport of four cargo pods
// behind a cab, L a spine of racked containers under a bridge, and XL a long hull under a
// superstructure, which the liner and the frigate share.
// A proxy for a simulation entity, ship, has a plume at each nozzle (ShipEngines), which ShowEngines
// lights; the display wing's ships, of none, have none.
entt::entity MakeHullProxy(
    entt::registry &scene, const Dock::HullDefinition &hull, entt::entity ship = entt::null);

// A ship proxy's engines: the simulation entity whose throttle they show, and the emitters of their
// plumes, in the effects layer.
struct ShipEngines
{
	entt::entity ship{entt::null};
	std::vector<entt::entity> plumes;
};

// Each ship's plumes to its throttle in the snapshot, the share of full ahead its flight controls
// ask for, and at rest in the frame given, the entity of the station the ships fly by (null for
// each plume's own).
void ShowEngines(entt::registry &scene, const Sim::Snapshot &snapshot, entt::entity frame);

// A buggy's proxy, in its own axes (nose -Z, its wheels on the deck at its origin): a 4 m flatbed
// tug, a cab with a beacon in the effects layer, and a bed for four crates, which show what it
// carries. The crates are found by the buggy's simulation entity (BuggyLoad), or by none for a
// parked buggy of the dressing, which carries nothing.
struct BuggyLoad
{
	entt::entity buggy{entt::null};
	std::array<entt::entity, 4> crates{};
};
entt::entity MakeBuggyProxy(entt::registry &scene, entt::entity buggy);

// Shows on each buggy's bed as many crates as the snapshot has it carry, four at most.
void ShowBuggyLoads(entt::registry &scene, const Sim::Snapshot &snapshot);
} // namespace Jettison::App
