#include <jettison/ai/Cruise.h>

#include <jettison/ai/Lanes.h>
#include <jettison/ai/Routes.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/Types.h>
#include <jettison/physics/Flight.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "Transit.h"

namespace Jettison::Ai
{
namespace
{
// Eases out of the start and into the end: a cruise leaves its station gently enough to be seen
// leaving, where a smoothstep is already 290 km out a second into a minute's cruise from Highport to
// Pell. Double precision throughout, since its share of a course of 10^11 m has to place a ship
// to the metre.
double EaseInOutCubic(double share)
{
	if (share < 0.5)
		return 4.0 * share * share * share;
	const double rest = 2.0 - (2.0 * share);
	return 1.0 - (rest * rest * rest / 2.0);
}

glm::dquat Facing(const glm::dvec3 &ahead)
{
	const glm::dvec3 up =
	    std::abs(ahead.y) > 0.999999 ? glm::dvec3{1.0, 0.0, 0.0} : glm::dvec3{0.0, 1.0, 0.0};
	return glm::quatLookAtRH(ahead, up);
}

// The body and every body it orbits, out to the primary.
std::vector<std::size_t> Chain(const Celestial::StarSystem &system, int body)
{
	std::vector<std::size_t> chain;
	for (int at = body; at >= 0; at = system.bodies[static_cast<std::size_t>(at)].parentIndex)
		chain.push_back(static_cast<std::size_t>(at));
	return chain;
}

bool Contains(const std::vector<std::size_t> &chain, std::size_t body)
{
	return std::ranges::find(chain, body) != chain.end();
}

// What a course is planned and flown against: the system, the carrier and the two ends' chains.
struct Planner
{
	const Celestial::StarSystem &system;
	int carrier{-1};
	std::vector<std::size_t> departure;
	std::vector<std::size_t> arrival;

	glm::dvec3 Where(std::size_t body, double time) const
	{
		return Celestial::EvaluateBody(system, body, time).metres;
	}
	glm::dvec3 CarrierAt(double time) const
	{
		return carrier < 0 ? glm::dvec3{0.0} : Where(static_cast<std::size_t>(carrier), time);
	}
	glm::dvec3 Relative(std::size_t body, double time) const
	{
		return Where(body, time) - CarrierAt(time);
	}

	// The course from the start to the target's standoff on the side of `towards`, both relative to
	// the carrier, for a cruise from t0 to t1.
	std::optional<Course> Plot(
	    const glm::dvec3 &start, const glm::dvec3 &towards, std::size_t target, double t0,
	    double t1) const
	{
		const glm::dvec3 at = Relative(target, t1);
		glm::dvec3 away = towards - at;
		if (glm::length(away) == 0.0)
			away = start - at;
		if (glm::length(away) == 0.0)
			away = glm::dvec3{0.0, 0.0, 1.0};
		const glm::dvec3 end =
		    at + glm::normalize(away) * CruiseTravel::ArrivalDistance(system.bodies[target]);

		const double middle = t0 + ((t1 - t0) / 2.0);
		std::vector<Sphere> obstacles;
		for (std::size_t body = 0; body < system.bodies.size(); ++body)
		{
			const Celestial::BodyKind kind = system.bodies[body].kind;
			if (kind != Celestial::BodyKind::Star && kind != Celestial::BodyKind::Planet &&
			    kind != Celestial::BodyKind::Moon)
				continue;
			const bool left = Contains(departure, body);
			const bool reached = Contains(arrival, body);
			const double radius = system.bodies[body].radius;
			if (left && !reached)
				obstacles.push_back({Relative(body, t0), radius});
			else if (reached && !left)
				obstacles.push_back({Relative(body, t1), radius});
			else
				obstacles.push_back(
				    {Relative(body, middle),
				     radius + (glm::length(Relative(body, t1) - Relative(body, t0)) / 2.0)});
		}
		return Waypoints{std::move(obstacles), CruiseTravel::kCourseClearance}.PlotCourse(start, end);
	}
};

// The point the share of the way along a course is at, and the leg it is on.
std::pair<glm::dvec3, glm::dvec3> Along(const std::vector<glm::dvec3> &course, double share)
{
	double length = 0.0;
	for (std::size_t point = 1; point < course.size(); ++point)
		length += glm::length(course[point] - course[point - 1]);
	double left = length * share;
	for (std::size_t point = 1; point < course.size(); ++point)
	{
		const glm::dvec3 leg = course[point] - course[point - 1];
		const double legLength = glm::length(leg);
		if (legLength <= 0.0)
			continue;
		if (left <= legLength || point + 1 == course.size())
			return {course[point - 1] + leg * (std::min(left, legLength) / legLength), leg / legLength};
		left -= legLength;
	}
	return {course.back(), glm::dvec3{0.0, 0.0, -1.0}};
}
} // namespace

double CruiseSeconds(const CruiseDrive &drive, double metres)
{
	if (metres <= 0.0)
		return 0.0;
	return double{drive.seconds} * std::pow(metres / drive.reference, double{drive.exponent});
}

double CruiseTravel::ArrivalDistance(const Celestial::Body &body)
{
	return std::max(
	    (body.radius * kArrivalRadiusShare) + kArrivalStandoff,
	    std::sqrt(body.gravitationalParameter / kArrivalGravity));
}

CruiseTravel &CruiseTravel::Attach(Sim::SimWorld &world)
{
	RegisterTypes();
	// Shared by the command and the system, which the world keeps for as long as it lives.
	std::shared_ptr<CruiseTravel> cruises{new CruiseTravel{}};
	world.RegisterComponent<CruiseDrive>();
	world.RegisterComponent<Cruising>();
	world.ShowInSnapshots<Cruising>();
	world.RegisterCommand<Cruise>([cruises](Sim::SimWorld &target, const Cruise &command)
	                              { cruises->Start(target, command); });
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<CruiseDrive, Sim::InSystem>()
	         .Writing<
	             Cruising, Sim::Transform, Physics::Body, Physics::LocalFrame, Physics::Velocity,
	             Physics::FlightControls>(),
	     {std::string{LaneTravel::kSystemName}},
	     [cruises, &world](entt::registry & /*registry*/, double /*fixedStep*/)
	     { cruises->Place(world); }});
	return *cruises;
}

void CruiseTravel::DefineStarSystem(Celestial::StarSystem system)
{
	const std::uint64_t id = Sim::SystemId(system.name);
	if (m_systems.contains(id))
		throw std::invalid_argument{
		    std::format("Ai: star system '{}' is already defined", system.name)};
	m_systems.emplace(id, std::move(system));
}

bool CruiseTravel::HasStarSystem(std::uint64_t system) const
{
	return m_systems.contains(system);
}

void CruiseTravel::Start(Sim::SimWorld &world, const Cruise &command) const
{
	entt::registry &registry = world.Registry();
	const auto refuse = [&](Reason reason, std::vector<Sim::ReasonArgument> arguments = {})
	{
		world.Events().Emit(
		    CruiseRefused{command.ship, command.body, Sim::MakeReason(reason, std::move(arguments))});
	};
	const entt::entity ship = command.ship;
	if (!registry.valid(ship))
		return refuse(Reason::NoSuchEntity);
	if (!Sim::MayCommand(registry, ship, command.sender))
		return refuse(Reason::NotController);
	const auto *inSystem = registry.try_get<Sim::InSystem>(ship);
	if (!inSystem || inSystem->system == Sim::kNoSystem)
		return refuse(registry.all_of<OnLane>(ship) ? Reason::OnLane : Reason::InNoSystem);
	if (registry.all_of<Cruising>(ship))
		return refuse(Reason::AlreadyCruising);
	const auto *body = registry.try_get<Physics::Body>(ship);
	const auto *drive = registry.try_get<CruiseDrive>(ship);
	const auto *transform = registry.try_get<Sim::Transform>(ship);
	if (!body || body->kind != Physics::BodyKind::Dynamic || !drive || !transform ||
	    registry.all_of<Physics::OnRails>(ship))
		return refuse(Reason::CannotCruise);
	const auto found = m_systems.find(inSystem->system);
	if (found == m_systems.end())
		return refuse(Reason::SystemUndefined);
	const Celestial::StarSystem &system = found->second;
	const int targetIndex = system.Find(command.body);
	if (targetIndex < 0)
		return refuse(
		    Reason::NoSuchBody, {Sim::ReasonArgument::Identifier(system.name),
		                         Sim::ReasonArgument::Identifier(command.body)});
	const auto target = static_cast<std::size_t>(targetIndex);

	const double t0 = world.Clock().Now();
	const glm::dvec3 from = transform->position;
	const std::vector<Celestial::SystemPosition> bodies = Celestial::EvaluateSystem(system, t0);
	for (std::size_t station = 0; station < system.bodies.size(); ++station)
		if (system.bodies[station].kind == Celestial::BodyKind::Station &&
		    glm::distance(from, bodies[station].metres) < system.bodies[station].radius + kStationReach)
			return refuse(
			    Reason::NearStation, {Sim::ReasonArgument::Identifier(system.bodies[station].name)});
	if (glm::distance(from, bodies[target].metres) <
	    ArrivalDistance(system.bodies[target]) + kStationReach)
		return refuse(Reason::AlreadyThere, {Sim::ReasonArgument::Identifier(command.body)});

	// The carrier: the nearest body both the frame left and the target orbit.
	const auto *frame = registry.try_get<Physics::LocalFrame>(ship);
	Planner planner{
	    system, -1, Chain(system, frame ? system.Find(frame->body) : -1), Chain(system, targetIndex)};
	for (const std::size_t orbited : planner.departure)
		if (Contains(planner.arrival, orbited))
		{
			planner.carrier = static_cast<int>(orbited);
			break;
		}

	// The time from the course, and the course from the time: the drive's time rises as the fourth
	// root of the distance, so each round changes it by a quarter of the last's share, and the
	// rounds settle well within kSolveIterations.
	const glm::dvec3 start = from - planner.CarrierAt(t0);
	const double step = world.FixedStep();
	double seconds = CruiseSeconds(*drive, glm::distance(from, bodies[target].metres));
	glm::dvec3 towards = start;
	std::optional<Course> course;
	std::uint64_t steps = 0;
	for (int round = 0; round <= kSolveIterations; ++round)
	{
		steps = Detail::TransitSteps(seconds, step);
		course = planner.Plot(start, towards, target, t0, t0 + (static_cast<double>(steps) * step));
		if (!course)
			return refuse(Reason::NoCourse, {Sim::ReasonArgument::Identifier(command.body)});
		towards = course->points[course->points.size() - 2];
		if (round < kSolveIterations)
			seconds = CruiseSeconds(*drive, course->length);
	}

	const int parent = system.bodies[target].parentIndex;
	const bool byStation =
	    system.bodies[target].kind == Celestial::BodyKind::JumpPoint && parent >= 0 &&
	    system.bodies[static_cast<std::size_t>(parent)].kind == Celestial::BodyKind::Station;
	Cruising cruising;
	cruising.target = command.body;
	cruising.frame = byStation ? system.bodies[static_cast<std::size_t>(parent)].name : command.body;
	cruising.carrier = planner.carrier < 0
	                       ? std::string{}
	                       : system.bodies[static_cast<std::size_t>(planner.carrier)].name;
	cruising.course = std::move(course->points);
	cruising.departureStep = world.Clock().Step();
	cruising.arrivalStep = cruising.departureStep + steps;
	cruising.body = *body;

	registry.remove<Physics::Body, Physics::Velocity, Physics::LocalFrame, Destination>(ship);
	world.Events().Emit(
	    CruiseStarted{
	        ship, command.body, static_cast<double>(steps) * step, course->length,
	        cruising.arrivalStep});
	registry.emplace_or_replace<Cruising>(ship, std::move(cruising));
}

void CruiseTravel::Place(Sim::SimWorld &world) const
{
	entt::registry &registry = world.Registry();
	std::vector<entt::entity> ships;
	for (const entt::entity ship : registry.view<const Cruising>())
		ships.push_back(ship);
	if (ships.empty())
		return;
	std::ranges::sort(ships, {}, [](entt::entity ship) { return entt::to_integral(ship); });

	// The step being run ends at the next count, and at this time.
	const std::uint64_t ending = world.Clock().Step() + 1;
	const double endsAt = world.Clock().Now() + world.FixedStep();
	for (const entt::entity ship : ships)
	{
		const Cruising &cruising = registry.get<const Cruising>(ship);
		const std::uint64_t system = registry.get<const Sim::InSystem>(ship).system;
		const auto found = m_systems.find(system);
		const int carrier = found == m_systems.end() ? -1 : found->second.Find(cruising.carrier);
		const int target = found == m_systems.end() ? -1 : found->second.Find(cruising.target);
		if (target < 0 || (carrier < 0 && !cruising.carrier.empty()))
			throw std::invalid_argument{std::format(
			    "Ai: entity {:#x} cruises to '{}' by '{}' in a system {}", entt::to_integral(ship),
			    cruising.target, cruising.carrier,
			    found == m_systems.end() ? "not defined" : "without those bodies")};
		const Celestial::StarSystem &starSystem = found->second;
		const glm::dvec3 origin =
		    carrier < 0
		        ? glm::dvec3{0.0}
		        : Celestial::EvaluateBody(starSystem, static_cast<std::size_t>(carrier), endsAt).metres;

		if (ending < cruising.arrivalStep)
		{
			const double share = static_cast<double>(ending - cruising.departureStep) /
			                     static_cast<double>(cruising.arrivalStep - cruising.departureStep);
			const auto [at, leg] = Along(cruising.course, EaseInOutCubic(share));
			registry.emplace_or_replace<Sim::Transform>(ship, Sim::Transform{origin + at, Facing(leg)});
			continue;
		}

		const glm::dvec3 at = origin + cruising.course.back();
		const glm::dvec3 ahead =
		    Celestial::EvaluateBody(starSystem, static_cast<std::size_t>(target), endsAt).metres - at;
		registry.emplace_or_replace<Sim::Transform>(
		    ship,
		    Sim::Transform{
		        at,
		        Facing(glm::length(ahead) > 0.0 ? glm::normalize(ahead) : glm::dvec3{0.0, 0.0, -1.0})});
		registry.emplace_or_replace<Physics::Body>(ship, cruising.body);
		registry.emplace_or_replace<Physics::LocalFrame>(ship, cruising.frame);
		registry.emplace_or_replace<Physics::Velocity>(ship);
		if (auto *controls = registry.try_get<Physics::FlightControls>(ship))
			controls->reference = entt::null;
		const std::string body = cruising.target;
		registry.remove<Cruising>(ship);
		world.Events().Emit(CruiseEnded{ship, body});
	}
}
} // namespace Jettison::Ai
