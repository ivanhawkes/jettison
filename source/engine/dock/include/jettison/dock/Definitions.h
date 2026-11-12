#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/physics/Shape.h>

namespace Jettison::Dock
{
// What a station's docks are made of, as data (docs/roadmap.md, catalogue entry 31): the hulls that
// land on its pads, and its layout, which a station in its system's file names
// (Celestial::Body::layout). Both are definitions (Sim::DefinitionRegistry), so a mod adds a hull
// or rebuilds a station, and a save names them and holds neither.
//
// Axes are the station's and a hull's own, as Sim::Transform has them: a hull's nose is -Z, its
// starboard +X and its top +Y, so its length is along Z, its beam along X and its height along Y.

// A pad's size, and the largest hull of each class that lands on one. A hull lands on a pad of its
// class or larger.
enum class PadClass : std::uint8_t
{
	S,
	M,
	L,
	// Display bays only: no ship is given one (scope).
	XL
};

// The largest hull of a class, length by beam by height, in metres: S a fighter's 25 x 20 x 8, M a
// transport's 60 x 30 x 15, L the largest transport's 140 x 50 x 30, XL a liner's 300 x 80 x 60.
glm::vec3 LargestHull(PadClass padClass);
// A pad of the class, its width (across its heading) and length (along it): S 35 x 40, M 50 x 90,
// L 80 x 180, XL 120 x 360. Room on each side of the largest hull for a clamp's reach and a buggy's
// road beside it.
glm::vec2 PadSize(PadClass padClass);

// How far aft of a pad's middle, along its centre line, the hatch of its class's largest hull is:
// a fifth of that hull's length and a little more, where the dressing paints the keep-clear box and
// the road graph's node for the pad's hatch is.
float HatchAft(PadClass padClass);

// A hull: its size, the pad class it lands on, and where its cargo hatch is, in its own axes from
// its centre, which the deck's buggies drive to (9e). defs/hulls/<name>.yaml; its collision shape
// is the box of its size (Docks::HullShape), whatever it is drawn as. The model is presentation's
// alone, a glTF file by virtual path in the hull's axes and metres, drawn in place of its class's
// proxy; the simulation never reads it. Registered with Reflect as "Dock.HullDefinition", every
// field Saved and Moddable, by RegisterTypes.
struct HullDefinition
{
	std::string displayName;
	float length{10.0f};
	float beam{6.0f};
	float height{2.0f};
	PadClass pad{PadClass::S};
	glm::vec3 hatch{0.0f};
	std::string model{};
};

// A box of a station's structure, in its axes, square to them: its centre and its whole size.
// Registered with Reflect as "Dock.Box", both fields Saved and Moddable.
struct Box
{
	glm::dvec3 centre{0.0};
	glm::dvec3 size{0.0};
};

// A pad: its class, the middle of its face on the deck, the deck's normal there (which a ship lands
// down) and the heading its ships' noses point along, square to the normal, both in the station's
// axes. A display pad is the layout's dressing, and no ship is given it: the ship it shows is a
// hull drawn on it by presentation (the station dressing), not a body, so it is named by its hull
// alone, and only a display pad names one. Pads are numbered from 1 in the layout's order.
// Registered with Reflect as "Dock.PadLayout", every field Saved and Moddable.
struct PadLayout
{
	PadClass padClass{PadClass::S};
	glm::dvec3 position{0.0};
	glm::dvec3 normal{0.0, 1.0, 0.0};
	glm::dvec3 heading{0.0, 0.0, -1.0};
	bool display{false};
	std::string ship{};
};

// A cargo bay's door, which buggies carry crates between and a ship's hatch (9e): the middle of its
// sill, on the deck where a buggy drives through it, and the way it faces. A door facing along the
// deck stands in a wall, up from its sill; one facing the deck's way is a hatch in it. Registered with
// Reflect as "Dock.Bay", both fields Saved and Moddable.
struct Bay
{
	glm::dvec3 position{0.0};
	glm::dvec3 normal{0.0, 1.0, 0.0};
};

// How a station's cargo crosses its deck (9e): the buggies each pad has, the crates a buggy carries
// a trip and its speed on the roads. Working, the buggies are the simulation's, driven between the
// pad's bay and its ship's hatch; otherwise they are parked on the deck as dressing and the cargo is
// moved at the rate working buggies would move it, a buggy's load each round trip of the pad's road
// shared among them, which costs a station nothing a step. Registered with Reflect as
// "Dock.DeckTraffic", every field Saved and Moddable.
struct DeckTraffic
{
	std::uint32_t buggies{3};
	std::int64_t cratesPerTrip{4};
	float speed{8.0f};
	bool working{true};
};

// A station's layout: the decks (its hub and the blocks about it) and the piers that reach out from
// them, both boxes, which the station collides as (DeckShape); its pads; its cargo bays; and how its
// cargo crosses the deck between them. A ship given a pad that has not landed on it in the
// reservation's seconds loses it. defs/stations/<name>.yaml. Registered with Reflect as
// "Dock.StationLayout", every field Saved and Moddable.
struct StationLayout
{
	std::string displayName;
	std::vector<Box> decks;
	std::vector<Box> piers;
	std::vector<PadLayout> pads;
	std::vector<Bay> bays;
	float reservationSeconds{300.0f};
	DeckTraffic traffic{};
};

// The definitions' version, which the registry expects of every file.
inline constexpr std::uint32_t kDefinitionVersion = 1;
inline constexpr std::string_view kHullDirectory = "defs/hulls";
inline constexpr std::string_view kStationDirectory = "defs/stations";

// Checks a hull, in the one place a file read and a hull made in code both go through: a size that
// is positive and within its class's largest hull, and its hatch within it. Throws
// Serialise::FormatError naming the hull.
void CheckHull(std::string_view name, const HullDefinition &hull);

// Checks a layout the same way: boxes of positive size; at least one; each pad's normal and heading
// of unit length and square to each other; each pad lying on the face of a deck or a pier, all of
// it on the structure; no two pads overlapping; nothing of the station over any pad, between it
// and the sphere about the station that holds it all (Radius), so a ship comes down onto it from
// outside that sphere straight along its normal; deck traffic of a buggy or more a pad, a crate or
// more a trip and a speed; and, for a layout with bays, every pad not for display reached by road
// from one (Roads). Throws Serialise::FormatError naming the layout and what does not fit.
void CheckLayout(std::string_view name, const StationLayout &layout);

// The radius of the smallest sphere about the station's centre holding its decks and piers.
double Radius(const StationLayout &layout);

// The layout's structure as a mesh, every deck and pier a box of twelve triangles, counter-clockwise
// from outside: the shape the station collides as.
Physics::ShapeDefinition DeckShape(const StationLayout &layout);

// A pad's orientation in the station's axes: its +Y the pad's normal, its -Z its heading, as a ship
// clamped to it is turned.
glm::dquat PadOrientation(const PadLayout &pad);

// Registers the module's types with Reflect, and physics' (Physics::RegisterTypes). Idempotent;
// Docks::Attach calls it.
void RegisterTypes();
} // namespace Jettison::Dock
