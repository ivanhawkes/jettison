#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <jettison/sim/Snapshot.h>
#include <jettison/transit/Profile.h>

namespace Jettison::Transit
{
// A passage's way (Passage.h): points from where it sets out to where it arrives, both included,
// metres from what carries it (its carrier), in its frame's axes. Double precision throughout, as
// a share of a way of 10^11 m has to place an entity to the metre.

// A passage of that many seconds in whole steps, rounded up, and at least one. A millionth of a step
// is let go before rounding up, since a time authored in seconds is rarely a whole number of steps
// in floating point: 40 s at 60 Hz is 2400.0000000000005 steps.
std::uint64_t Steps(double seconds, double fixedStep);

// Eases out of the start and into the end, an in-out cubic of the share of the steps run: a passage
// leaves gently enough to be seen leaving, where a smoothstep is already 290 km out a second into a
// minute's passage of 10^11 m.
double Eased(double share);

// Facing along a direction of unit length, up the y axis, or the x axis for a direction along y.
glm::dquat Facing(const glm::dvec3 &ahead);

// How long the way is: the sum of its legs.
double Length(std::span<const glm::dvec3> path);

// The point the share of the way along it is at, and the direction of the leg it is on; the last
// point, facing down z, for a way of no length.
std::pair<glm::dvec3, glm::dvec3> Along(std::span<const glm::dvec3> path, double share);

// A velocity at an end of a way, in m/s, split for a profile with end speeds: its speed along the leg
// there, its profile's to meet (EndSpeeds), and the rest, across the way or backward along it, for
// Drift to carry.
struct Split
{
	double along{0.0};
	glm::dvec3 across{0.0};
};

// Splits a velocity at an end of a way whose leg there runs along a direction of unit length.
Split SplitAlong(const glm::dvec3 &velocity, const glm::dvec3 &leg);

// How far a passage of that many seconds is off its way that many seconds after setting out, to
// carry what its profile does not of a velocity at each end (Split): the cubic in the time that is
// none at either end, sets out changing at the start's velocity and arrives changing at the end's.
// Its acceleration, at most six times the larger velocity over the seconds, is not in Thrust.
glm::dvec3 Drift(const glm::dvec3 &start, const glm::dvec3 &end, double seconds, double at);

// Where a passage from the departure step to the arrival step has its entity as the step ending at
// that count ends, before it arrives: along its way, about its carrier's place then (origin), facing
// along the leg it is on. It eases along (Eased), or with a profile covers it as the profile does
// (Covered), slowed to the whole steps it was given.
Sim::Transform Underway(
    std::span<const glm::dvec3> path, std::uint64_t departureStep, std::uint64_t arrivalStep,
    const std::optional<MotionProfile> &profile, const glm::dvec3 &origin, std::uint64_t ending);
} // namespace Jettison::Transit
