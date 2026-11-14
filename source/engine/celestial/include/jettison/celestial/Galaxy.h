#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Celestial
{
struct StarSystem;

// The galaxy frame of docs/architecture.md, "Coordinate frames and units": no origin and no
// positions, only a graph. Its nodes are the star systems and its edges the jump lanes, each lane
// joining a jump point in one system to a jump point in another, travelled either way. Route
// planning over it is the AI's (phase 9a); this is the graph and its file.

// A star system as the galaxy knows it: its name, which is the name its system file gives it, and
// where its files are, by virtual path.
struct GalaxySystem
{
	std::string name;
	// The system file (systems/halcyon.yaml).
	std::string system;
	// A scene file authored for the system, which names the same system file; without one the
	// system is shown as its system scene builds it from the system alone.
	std::optional<std::string> scene;
};

// One end of a lane: a jump point, by its body's name, in a system, by the system's name.
struct LaneEnd
{
	std::string system;
	std::string jumpPoint;

	// Runtime: the system's index in Galaxy::systems. Filled in by Resolve, not written to the file.
	std::size_t systemIndex{0};
};

// A jump lane. It is travelled both ways, so its ends are a and b, in the order the file lists
// them, and nothing about a jump depends on which is which.
struct JumpLane
{
	LaneEnd a;
	LaneEnd b;
	// Simulation seconds between entering one end and leaving the other.
	double transitSeconds{0.0};
	// What a ship's owner is charged to enter it, in whole credits, the game's currency (ai/Gates.h):
	// none for a free lane.
	std::int64_t toll{0};
};

struct Galaxy
{
	std::string name;
	std::vector<GalaxySystem> systems;
	std::vector<JumpLane> lanes;

	// The index of the system of that name, or -1.
	int Find(std::string_view systemName) const;
	// The index of the system whose system file is at that virtual path, or -1.
	int FindBySystemFile(std::string_view virtualPath) const;
};

// A way out of a system: a lane, seen from the end in that system.
struct Exit
{
	std::size_t lane{0};
	// The jump point the lane leaves from, in the system it is an exit of.
	std::string_view jumpPoint;
	// The system it arrives in, an index in Galaxy::systems, and the jump point it arrives at there.
	std::size_t destination{0};
	std::string_view arrival;
	double transitSeconds{0.0};
	std::int64_t toll{0};
};

// Registers GalaxySystem, LaneEnd, JumpLane and Galaxy with Reflect. Idempotent.
void RegisterGalaxyTypes();

// The galaxy file's version, which ReadGalaxy expects and WriteGalaxy writes.
inline constexpr std::uint32_t kGalaxyFormatVersion = 1;

// Checks the graph and fills in the ends' system indices, in one place that the reader and a
// galaxy built in code both go through: at least one system; system names unique and not empty;
// every path a virtual path; each lane's ends in systems the galaxy lists, two different systems,
// each naming a jump point; no jump point the end of two lanes, so a jump point says where it
// leads; transit times and tolls not negative. Throws Serialise::FormatError naming the galaxy and
// the system or the lane. Whether each jump point is one in its system's file is CheckJumpPoints',
// since this reads no system file.
void Resolve(Galaxy &galaxy);

// A galaxy file (YAML, through Serialise::YamlArchive) read and resolved, and its writer.
Galaxy ReadGalaxy(std::string_view text);
std::string WriteGalaxy(const Galaxy &galaxy);

// Checks a system's file against the galaxy: it is the system of that name, and each lane end in
// it names a body of the system's that is a jump point. A jump point no lane ends at is allowed: a
// gate that leads nowhere yet. Throws Serialise::FormatError naming the galaxy, the system and the
// lane.
void CheckJumpPoints(const Galaxy &galaxy, std::size_t system, const StarSystem &starSystem);

// Every lane with an end in the system, in the order the galaxy lists them, seen from that end. The
// views are into the galaxy, which outlives them.
std::vector<Exit> Exits(const Galaxy &galaxy, std::size_t system);
// The lane leaving the system through that jump point, if one does.
std::optional<Exit> ExitThrough(const Galaxy &galaxy, std::size_t system, std::string_view jumpPoint);
} // namespace Jettison::Celestial
