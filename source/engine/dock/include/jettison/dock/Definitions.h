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
// What a platform's docks are made of, as data (docs/history.md, catalogue entry 31): the classes of
// its berths, the vehicles that land on them, and its layout, which the game names for each platform
// it makes (Docks::MakePlatform). All are definitions (Sim::DefinitionRegistry), so a mod adds a berth
// class or a vehicle or rebuilds a platform, and a save names them and holds none.
//
// Axes are the platform's and a vehicle's own, as Sim::Transform has them: a vehicle's nose is -Z, its
// starboard +X and its top +Y, so its length is along Z, its beam along X and its height along Y.

// A class of berth (Manifest's D-044: a berth's size is the game's data, not the engine's): the largest
// vehicle that lands on one, length by beam by height in metres; a berth's width (across its heading)
// and length (along it), with room on each side of that vehicle for a clamp's reach and a road beside
// it; and its rank among the classes. A vehicle lands on a berth of its own class or of one ranked
// above it that holds it (Fits), the lowest ranked first: the rank orders the choice and the largest
// vehicle is the limit, so a class ranked above a smaller one is never given the larger one's vehicles.
// defs/berths/<name>.yaml; the dock suite's are corvette, frigate, destroyer and cruiser, a fighter's
// to a liner's. Registered with Reflect as "Dock.BerthClassDefinition", every field Saved and Moddable,
// by RegisterTypes.
struct BerthClassDefinition
{
	std::uint32_t rank{0};
	glm::vec3 largestVehicle{0.0f};
	glm::vec2 berthSize{0.0f};

	bool operator==(const BerthClassDefinition &) const = default;
};

// The berth classes defined, by name.
using BerthClasses = std::map<std::string, BerthClassDefinition, std::less<>>;

// The class of that name. Throws Serialise::FormatError for one that is not defined, naming what
// names it.
const BerthClassDefinition &BerthClassOf(
    const BerthClasses &classes, std::string_view berthClass, std::string_view namedBy);

// A vehicle: its size and the berth class it lands on. defs/vehicles/<name>.yaml; its collision shape
// is the box of its size (Docks::VehicleShape), whatever it is drawn as. The model is presentation's
// alone, a glTF file by virtual path in the vehicle's axes and metres, drawn in place of its class's
// proxy; the simulation never reads it. How it travels between platforms is the game's, as a
// component the game gives its vehicles. Registered with Reflect as
// "Dock.VehicleDefinition", every field Saved and Moddable, by RegisterTypes.
struct VehicleDefinition
{
	std::string displayName;
	float length{10.0f};
	float beam{6.0f};
	float height{2.0f};
	// Its berth class's name.
	std::string berthClass;
	std::string model{};
};

// A box of a platform's structure, in its axes, square to them: its centre and its whole size.
// Registered with Reflect as "Dock.Box", both fields Saved and Moddable.
struct Box
{
	glm::dvec3 centre{0.0};
	glm::dvec3 size{0.0};
};

// A berth: its class, the middle of its face on the deck, the deck's normal there (which a vehicle
// lands down) and the heading its vehicles' noses point along, square to the normal, both in the
// platform's axes. A display berth is the layout's dressing, and no vehicle is given it: the vehicle it
// shows is drawn on it by presentation (the platform dressing), not a body, so it is named by its
// definition alone, and only a display berth names one. Berths are numbered from 1 in the layout's
// order.
//
// A berth's features are what it offers besides a place to land, by names that are the game's (a
// "dry-dock", where a vehicle is repaired): the docks give a vehicle that asks for one a
// berth that offers it (RequestBerth::feature) and know nothing of what it is for.
// Registered with Reflect as "Dock.BerthLayout", every field Saved and Moddable.
struct BerthLayout
{
	// A berth class's name.
	std::string berthClass;
	glm::dvec3 position{0.0};
	glm::dvec3 normal{0.0, 1.0, 0.0};
	glm::dvec3 heading{0.0, 0.0, -1.0};
	bool display{false};
	std::string vehicle{};
	std::vector<std::string> features{};
};

// A platform's layout: the decks (its hub and the blocks about it) and the piers that reach out from
// them, both boxes, which the platform collides as (DeckShape); and its berths. A vehicle given a berth
// that has not landed on it in the reservation's seconds loses it. defs/platforms/<name>.yaml.
// Registered with Reflect as "Dock.PlatformLayout", every field Saved and Moddable.
struct PlatformLayout
{
	std::string displayName;
	std::vector<Box> decks;
	std::vector<Box> piers;
	std::vector<BerthLayout> berths;
	float reservationSeconds{300.0f};
};

// The definitions' files' versions, which change in place until player testing (docs/decisions.md,
// "Formats change in place until player testing"): the files the repositories hold are written again.
inline constexpr std::uint32_t kBerthClassVersion = 2;
inline constexpr std::uint32_t kVehicleVersion = 3;
inline constexpr std::uint32_t kPlatformVersion = 2;
inline constexpr std::string_view kBerthClassDirectory = "defs/berths";
inline constexpr std::string_view kVehicleDirectory = "defs/vehicles";
inline constexpr std::string_view kPlatformDirectory = "defs/platforms";

// Checks a berth class: a largest vehicle and a berth of positive, finite size, the vehicle's beam
// within the berth's width and its length within the berth's length. Throws Serialise::FormatError
// naming the class.
void CheckBerthClass(std::string_view name, const BerthClassDefinition &berthClass);

// The definitions' files' chains (Sim::DefinitionRegistry::Load), each format the type's reflected
// name, reading the current version alone.
const Serialise::YamlMigrations &BerthClassMigrations();
const Serialise::YamlMigrations &VehicleMigrations();
const Serialise::YamlMigrations &PlatformMigrations();

// Checks a vehicle, in the one place a file read and a vehicle made in code both go through: a berth
// class of those defined, and a size that is positive and within its class's largest vehicle. Throws
// Serialise::FormatError naming the vehicle.
void CheckVehicle(std::string_view name, const VehicleDefinition &vehicle, const BerthClasses &classes);

// Whether a vehicle lands on a berth of that class: its own, or one ranked above or level with it whose
// largest vehicle is no smaller than the vehicle in any dimension. A class not in those given takes
// none, and a vehicle of a class not in them lands on none. Content validation asks it with no world.
bool Fits(const BerthClasses &classes, const VehicleDefinition &vehicle, std::string_view berthClass);

// Checks a layout the same way: boxes of positive size; at least one; each berth's normal and heading
// of unit length and square to each other; each berth lying on the face of a deck or a pier, all of
// it on the structure; no two berths overlapping; nothing of the platform over any berth, between it
// and the sphere about the platform that holds it all (Radius), so a vehicle comes down onto it from
// outside that sphere straight along its normal; and each berth of a class of those defined. Throws
// Serialise::FormatError naming the layout and what does not fit.
void CheckLayout(std::string_view name, const PlatformLayout &layout, const BerthClasses &classes);

// The radius of the smallest sphere about the platform's centre holding its decks and piers.
double Radius(const PlatformLayout &layout);

// The layout's structure as a mesh, every deck and pier a box of twelve triangles, counter-clockwise
// from outside: the shape the platform collides as.
Physics::ShapeDefinition DeckShape(const PlatformLayout &layout);

// A berth's orientation in the platform's axes: its +Y the berth's normal, its -Z its heading, as a
// vehicle clamped to it is turned.
glm::dquat BerthOrientation(const BerthLayout &berth);

// Registers the module's types with Reflect, and physics' (Physics::RegisterTypes). Idempotent;
// Docks::Attach calls it.
void RegisterTypes();
} // namespace Jettison::Dock
