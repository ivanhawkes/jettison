#include <jettison/transit/Path.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace Jettison::Transit
{
namespace
{
// Of a step, let go before rounding up (Steps).
constexpr double kStepTolerance = 1.0e-6;
// How near the y axis a direction is for Facing to take x as its up.
constexpr double kAlongUp = 0.999999;
} // namespace


std::uint64_t Steps(double seconds, double fixedStep)
{
	const double steps = std::ceil((seconds / fixedStep) - kStepTolerance);
	return std::max<std::uint64_t>(1, steps > 0.0 ? static_cast<std::uint64_t>(steps) : 0);
}


double Eased(double share)
{
	if (share < 0.5)
		return 4.0 * share * share * share;
	const double rest = 2.0 - (2.0 * share);
	return 1.0 - (rest * rest * rest / 2.0);
}


glm::dquat Facing(const glm::dvec3 &ahead)
{
	const glm::dvec3 up =
	    std::abs(ahead.y) > kAlongUp ? glm::dvec3{1.0, 0.0, 0.0} : glm::dvec3{0.0, 1.0, 0.0};
	return glm::quatLookAtRH(ahead, up);
}


double Length(std::span<const glm::dvec3> path)
{
	double length = 0.0;
	for (std::size_t point = 1; point < path.size(); ++point)
		length += glm::length(path[point] - path[point - 1]);
	return length;
}


std::pair<glm::dvec3, glm::dvec3> Along(std::span<const glm::dvec3> path, double share)
{
	double left = Length(path) * share;
	for (std::size_t point = 1; point < path.size(); ++point)
	{
		const glm::dvec3 leg = path[point] - path[point - 1];
		const double legLength = glm::length(leg);
		if (legLength <= 0.0)
			continue;
		if (left <= legLength || point + 1 == path.size())
			return {path[point - 1] + leg * (std::min(left, legLength) / legLength), leg / legLength};
		left -= legLength;
	}
	return {path.back(), glm::dvec3{0.0, 0.0, -1.0}};
}


Sim::Transform Underway(
    std::span<const glm::dvec3> path, std::uint64_t departureStep, std::uint64_t arrivalStep,
    const std::optional<MotionProfile> &profile, const glm::dvec3 &origin, std::uint64_t ending)
{
	const double share =
	    static_cast<double>(ending - departureStep) / static_cast<double>(arrivalStep - departureStep);
	double along = Eased(share);
	if (profile)
	{
		const double length = Length(path);
		const double seconds = Seconds(*profile, length);
		along = Covered(*profile, length, share * seconds) / length;
	}
	const auto [at, leg] = Along(path, along);
	return {origin + at, Facing(leg)};
}
} // namespace Jettison::Transit
