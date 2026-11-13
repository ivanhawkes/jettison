#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/celestial/StarSystem.h>
#include <jettison/physics/Components.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/Reason.h>

namespace Jettison::Sim
{
class SimWorld;
}

namespace Jettison::Ai
{
// The cruise (docs/architecture.md, "Cruising"; docs/roadmap.md, "Decided": a ship crosses a system
// by cruising): a ship taken across its system to a body, out of physics and its frame as on a lane
// but staying in its system, placed each step along its course, and brought out at rest a few
// kilometres off the body, in its frame. The bodies are on rails, so where the target will be on
// arrival is solved exactly rather than chased.

// How long a ship's cruises take: the seconds a cruise of the reference distance takes, rising with
// the distance to the exponent, slower than it, so one planet's stations are about a minute apart
// and two planets a few. A hull's definition gives it (Dock::HullDefinition::cruise), and the game
// puts it on each ship that cruises. Registered with Reflect, every field Saved and Moddable, by
// RegisterTypes.
struct CruiseDrive
{
	float seconds{45.0f};
	// In metres.
	double reference{1.0e8};
	float exponent{0.25f};

	bool operator==(const CruiseDrive &) const = default;
};

// The seconds a cruise of that many metres takes on the drive: seconds × (metres / reference) to the
// exponent. None for no distance.
double CruiseSeconds(const CruiseDrive &drive, double metres);

// A ship cruising: its Sim::InSystem stays its system, and it has no Physics::Body, Velocity or
// LocalFrame from the step it set out in to the step it arrives in, so physics, steering, the docks
// and the lanes leave it alone; its Transform is where its course puts it at each step's end.
// Registered with Reflect, every field Saved, by RegisterTypes, and shown in snapshots.
struct Cruising
{
	// The body it is cruising to, and the frame it arrives in (Physics::LocalFrame): the body's own,
	// or for a jump point that keeps station by a station, the station's, as a lane's arrival is.
	std::string target;
	std::string frame;
	// The body the course moves with, the nearest both the frame it left and the target orbit (the
	// planet between two of its stations, the star between two planets), or none for the system's
	// own frame.
	std::string carrier;
	// From where it set out to where it arrives, both included, with the waypoints round the bodies
	// in the way between; metres from the carrier, in the system's axes.
	std::vector<glm::dvec3> course;
	// The clock's step counts as it set out and once it has arrived (Time::SimClock::Step): it
	// arrives as the step before the second ends.
	std::uint64_t departureStep{0};
	std::uint64_t arrivalStep{0};
	// Its body, which it has back on arrival.
	Physics::Body body;

	bool operator==(const Cruising &) const = default;
};

// The command that sends a ship on a cruise to a body of its system, applied as the step begins.
// Refused, with a CruiseRefused, for a ship its sender does not command (Sim::MayCommand), one not
// in a defined system (on a lane, or in none), one already cruising, one that is not a dynamic body
// with a CruiseDrive, or on rails (clamped to a pad), a body its system does not have, a ship within
// a station's reach (CruiseTravel::kStationReach), so docking stays in physics, a ship already
// within reach of where it would arrive, and a course every way is blocked across. A ship sent
// gives up its Destination, which a cruise replaces. Registered with Reflect, every field Saved, by
// RegisterTypes.
struct Cruise
{
	Sim::Controller sender;
	entt::entity ship{entt::null};
	std::string body;
};

// Raised as a ship sets out on a cruise, with how long it takes and how far its course is; as it
// arrives; and as a cruise is refused. Registered with Reflect, every field Saved, by RegisterTypes.
struct CruiseStarted
{
	entt::entity ship{entt::null};
	std::string body;
	double seconds{0.0};
	double metres{0.0};
	std::uint64_t arrivalStep{0};
};
struct CruiseEnded
{
	entt::entity ship{entt::null};
	std::string body;
};
struct CruiseRefused
{
	entt::entity ship{entt::null};
	std::string body;
	Sim::Reason reason;
};

// Cruising in a simulation world: the CruiseDrive and Cruising components, the Cruise command, and
// the system "cruise" (kSystemName), which runs after the lanes' (after physics') and places each
// cruising ship where its course has it as the step ends:
//   - the course is solved as the command is applied, from where the ship is and the time its
//     drive takes over the course: the target's place at the arrival step, a course to the point
//     its ArrivalDistance from it, on the side the course comes in from, round the stars,
//     planets and moons in the way (Waypoints, kCourseClearance), its length giving the time
//     again, in kSolveIterations; the time is whole steps, at least one;
//   - each body in the way is where it is relative to the carrier when the ship passes it: at
//     departure for what only the frame it left orbits, at arrival for what only the target does,
//     and otherwise half-way, its radius widened by half of how far it moves relative to the
//     carrier over the cruise;
//   - along the course it eases out of its start and into its end (an in-out cubic of the steps
//     run), carried with the carrier on its orbit, and faces along the leg it is on;
//   - in the step that ends its cruise it is at the course's end, facing the target, at rest in the
//     frame it arrives in, with its body back, which physics makes in the next step, and its
//     Physics::FlightControls taken against nothing; a CruiseEnded is raised.
// Ships are placed in entity order.
//
// The star systems are definitions, the game's start-up as physics' are, and not state: a save names
// bodies by name, and a world it is loaded into is given the same systems.
//
// The world holds it from Attach on, through the systems and commands it registers.
class CruiseTravel
{
  public:
	static constexpr std::string_view kSystemName = "cruise";
	// Metres from a station's radius within which a cruise does not engage.
	static constexpr double kStationReach = 2000.0;
	// Where a cruise arrives: this many metres beyond the target's radius and a quarter of it, so a
	// ship is a few kilometres off a station or a gate and clear of a planet's air; and no nearer a
	// body with gravity than where it pulls at kArrivalGravity. A station's or a gate's frame falls
	// round its planet with the ship, but a planet's own pull is the ship's to hold against: at 1.25
	// radii Corva pulls at 16.6 m/s², past a shuttle's 15 m/s² of reverse thrust, where a fifteenth of
	// that leaves its assist all it needs (Ostra's is 20,000 km out, Corva's 356,000 km).
	static constexpr double kArrivalStandoff = 4000.0;
	static constexpr double kArrivalRadiusShare = 1.25;
	static constexpr double kArrivalGravity = 1.0;
	// Metres a course keeps from a star's, a planet's or a moon's radius.
	static constexpr double kCourseClearance = 2000.0;
	static constexpr int kSolveIterations = 8;

	// Registers the components, the command and the "cruise" system, ordered after the lanes'
	// (LaneTravel::Attach), which have to be attached first. Throws std::logic_error as SimWorld's
	// registrations do, for one already made.
	static CruiseTravel &Attach(Sim::SimWorld &world);

	// A star system, for where its bodies are. It has to be resolved; throws std::invalid_argument
	// for a system already defined. A Cruise in a system not defined is refused.
	void DefineStarSystem(Celestial::StarSystem system);
	bool HasStarSystem(std::uint64_t system) const;

	// How far from a body's centre a cruise to it arrives.
	static double ArrivalDistance(const Celestial::Body &body);

  private:
	CruiseTravel() = default;

	void Start(Sim::SimWorld &world, const Cruise &command) const;
	void Place(Sim::SimWorld &world) const;

	// By Sim::SystemId; looked up, and ordered regardless.
	std::map<std::uint64_t, Celestial::StarSystem> m_systems;
};
} // namespace Jettison::Ai
