#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/physics/Shape.h>
#include <jettison/serialise/Migration.h>

namespace Jettison::Dock
{
// What a station's docks are made of, as data (docs/history.md, catalogue entry 31): the classes of
// its pads, the hulls that land on them, and its layout, which a station in its system's file names
// (Celestial::Body::layout). All are definitions (Sim::DefinitionRegistry), so a mod adds a pad
// class or a hull or rebuilds a station, and a save names them and holds none.
//
// Axes are the station's and a hull's own, as Sim::Transform has them: a hull's nose is -Z, its
// starboard +X and its top +Y, so its length is along Z, its beam along X and its height along Y.

// A class of pad (Manifest's D-044: a berth's size is the game's data, not the engine's): the largest
// hull that lands on one, length by beam by height in metres; a pad's width (across its heading) and
// length (along it), with room on each side of that hull for a clamp's reach and a road beside it;
// and its rank among the classes. A hull lands on a pad of its own class or of one ranked above it
// that holds it (Fits), the lowest ranked first: the rank orders the choice and the largest
// hull is the limit, so a class ranked above a smaller one is never given the larger one's hulls.
// defs/pads/<name>.yaml; the space sample's are corvette, frigate, destroyer and cruiser, a fighter's
// to a liner's. Registered with Reflect as "Dock.PadClassDefinition", every field Saved and Moddable,
// by RegisterTypes.
struct PadClassDefinition
{
	std::uint32_t rank{0};
	glm::vec3 largestHull{0.0f};
	glm::vec2 padSize{0.0f};

	bool operator==(const PadClassDefinition &) const = default;
};

// The pad classes defined, by name.
using PadClasses = std::map<std::string, PadClassDefinition, std::less<>>;

// The class of that name. Throws Serialise::FormatError for one that is not defined, naming what
// names it.
const PadClassDefinition &PadClassOf(
    const PadClasses &classes, std::string_view padClass, std::string_view namedBy);

// A hull: its size and the pad class it lands on. defs/hulls/<name>.yaml; its collision shape
// is the box of its size (Docks::HullShape), whatever it is drawn as. The model is presentation's
// alone, a glTF file by virtual path in the hull's axes and metres, drawn in place of its class's
// proxy; the simulation never reads it. How it travels between stations is the game's, as a
// component the game gives its ships (the space sample's cruise drive). Registered with Reflect as
// "Dock.HullDefinition", every field Saved and Moddable, by RegisterTypes.
struct HullDefinition
{
	std::string displayName;
	float length{10.0f};
	float beam{6.0f};
	float height{2.0f};
	// Its pad class's name.
	std::string pad;
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
//
// A pad's features are what it offers besides a place to land, by names that are the game's (the
// space sample's "dry-dock", where a ship is repaired): the docks give a ship that asks for one a
// pad that offers it (RequestPad::feature) and know nothing of what it is for.
// Registered with Reflect as "Dock.PadLayout", every field Saved and Moddable.
struct PadLayout
{
	// A pad class's name.
	std::string padClass;
	glm::dvec3 position{0.0};
	glm::dvec3 normal{0.0, 1.0, 0.0};
	glm::dvec3 heading{0.0, 0.0, -1.0};
	bool display{false};
	std::string ship{};
	std::vector<std::string> features{};
};

// A station's layout: the decks (its hub and the blocks about it) and the piers that reach out from
// them, both boxes, which the station collides as (DeckShape); and its pads. A ship given a pad that
// has not landed on it in the reservation's seconds loses it. defs/stations/<name>.yaml. Registered
// with Reflect as "Dock.StationLayout", every field Saved and Moddable.
struct StationLayout
{
	std::string displayName;
	std::vector<Box> decks;
	std::vector<Box> piers;
	std::vector<PadLayout> pads;
	float reservationSeconds{300.0f};
};

inline constexpr std::uint32_t kDefinitionVersion = 1;
// A hull's file is of version 2; one of version 1 may give a cruise drive, the game's now, and
// loads through HullMigrations with it left out.
inline constexpr std::uint32_t kHullVersion = 2;
inline constexpr std::string_view kPadClassDirectory = "defs/pads";
inline constexpr std::string_view kHullDirectory = "defs/hulls";
inline constexpr std::string_view kStationDirectory = "defs/stations";

// Checks a pad class: a largest hull and a pad of positive, finite size, the hull's beam within the
// pad's width and its length within the pad's length. Throws Serialise::FormatError naming the
// class.
void CheckPadClass(std::string_view name, const PadClassDefinition &padClass);

// A hull's files from version 1 up (Sim::DefinitionRegistry::Load), its format "Dock.HullDefinition".
const Serialise::YamlMigrations &HullMigrations();

// Checks a hull, in the one place a file read and a hull made in code both go through: a pad class
// of those defined, and a size that is positive and within its class's largest hull. Throws
// Serialise::FormatError naming the hull.
void CheckHull(std::string_view name, const HullDefinition &hull, const PadClasses &classes);

// Whether a hull lands on a pad of that class: its own, or one ranked above or level with it whose
// largest hull is no smaller than the hull in any dimension. A class not in those given takes none,
// and a hull of a class not in them lands on none. Content validation asks it with no world.
bool Fits(const PadClasses &classes, const HullDefinition &hull, std::string_view padClass);

// Checks a layout the same way: boxes of positive size; at least one; each pad's normal and heading
// of unit length and square to each other; each pad lying on the face of a deck or a pier, all of
// it on the structure; no two pads overlapping; nothing of the station over any pad, between it
// and the sphere about the station that holds it all (Radius), so a ship comes down onto it from
// outside that sphere straight along its normal; and each pad of a class of those defined. Throws
// Serialise::FormatError naming the layout and what does not fit.
void CheckLayout(std::string_view name, const StationLayout &layout, const PadClasses &classes);

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
