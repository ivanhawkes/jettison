#include <jettison/transit/Profile.h>

#include <jettison/serialise/Archive.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace Jettison::Transit
{
namespace
{
// The first half of a way on a profile, to its midpoint: the seconds and metres of its boost, the
// speed the boost reaches, the seconds, metres and speed its thrust ends at, and the seconds to the
// midpoint, coasting from the thrust's end.
struct Half
{
	double boostSeconds{0.0};
	double boostMetres{0.0};
	double speed{0.0};
	double thrustSeconds{0.0};
	double thrustMetres{0.0};
	double topSpeed{0.0};
	double seconds{0.0};
};


// The thrust a share of the whole time takes at each end, t of a way of T, so 2t = share × T
// with T = 2t + (metres − 2x(t)) / v(t): 2(1 − share) t v(t) + 2 share x(t) = share × metres, which
// rises with t, solved on the boost and, past it, at the sustained acceleration.
Half Coasting(const MotionProfile &profile, double metres, double share)
{
	const auto boost = double{profile.boostAcceleration};
	const auto sustained = double{profile.sustainedAcceleration};
	const double boostSeconds = double{profile.boostSeconds} / 2.0;
	Half half;
	if (const double onBoost = std::sqrt(share * metres / (boost * (2.0 - share)));
	    onBoost <= boostSeconds)
	{
		half.boostSeconds = onBoost;
		half.boostMetres = boost * onBoost * onBoost / 2.0;
		half.speed = boost * onBoost;
		half.thrustSeconds = onBoost;
		half.thrustMetres = half.boostMetres;
		half.topSpeed = half.speed;
	}
	else
	{
		const double speed = boost * boostSeconds;
		const double boostMetres = boost * boostSeconds * boostSeconds / 2.0;

		// a u² + b u = c in the seconds u past the boost, by the root that does not cancel.
		const double a = sustained * (2.0 - share);
		const double b =
		    (2.0 * (1.0 - share) * ((boostSeconds * sustained) + speed)) + (2.0 * share * speed);
		const double c = (share * metres) - (2.0 * (1.0 - share) * boostSeconds * speed) -
		                 (2.0 * share * boostMetres);
		const double past = 2.0 * c / (b + std::sqrt((b * b) + (4.0 * a * c)));
		half.boostSeconds = boostSeconds;
		half.boostMetres = boostMetres;
		half.speed = speed;
		half.thrustSeconds = boostSeconds + past;
		half.thrustMetres = boostMetres + (speed * past) + (sustained * past * past / 2.0);
		half.topSpeed = speed + (sustained * past);
	}
	half.seconds =
	    half.thrustSeconds + (std::max(0.0, (metres / 2.0) - half.thrustMetres) / half.topSpeed);
	return half;
}


Half HalfOf(const MotionProfile &profile, double metres)
{
	// A share of 1 thrusts to the midpoint, as none does, and by the same sums, so its times are
	// those of a profile saved before shares to the bit.
	if (profile.thrustShare && *profile.thrustShare < 1.0f)
		return Coasting(profile, metres, double{*profile.thrustShare});
	const double half = metres / 2.0;
	const auto boost = double{profile.boostAcceleration};
	const double boostSeconds = double{profile.boostSeconds} / 2.0;
	const double boostMetres = boost * boostSeconds * boostSeconds / 2.0;
	if (half <= boostMetres)
	{
		const double seconds = std::sqrt(2.0 * half / boost);
		const double speed = boost * seconds;
		return {seconds, half, speed, seconds, half, speed, seconds};
	}
	const double speed = boost * boostSeconds;
	const double rest = half - boostMetres;

	// speed × t + sustained × t² / 2 = rest, by the root that does not cancel when the speed is high.
	const double sustained =
	    2.0 * rest /
	    (speed + std::sqrt((speed * speed) + (2.0 * double{profile.sustainedAcceleration} * rest)));
	const double seconds = boostSeconds + sustained;
	return {boostSeconds, boostMetres, speed,
	        seconds,      half,        speed + (double{profile.sustainedAcceleration} * sustained),
	        seconds};
}
} // namespace


double Seconds(const MotionProfile &profile, double metres)
{
	return metres <= 0.0 ? 0.0 : 2.0 * HalfOf(profile, metres).seconds;
}


double Covered(const MotionProfile &profile, double metres, double seconds)
{
	if (metres <= 0.0 || seconds <= 0.0)
		return 0.0;
	const Half half = HalfOf(profile, metres);
	const double whole = 2.0 * half.seconds;
	if (seconds >= whole)
		return metres;

	// The way out to the midpoint; the way in is its mirror.
	const auto out = [&](double at)
	{
		if (at <= half.boostSeconds)
			return double{profile.boostAcceleration} * at * at / 2.0;
		if (at > half.thrustSeconds)
			return half.thrustMetres + (half.topSpeed * (at - half.thrustSeconds));
		const double on = at - half.boostSeconds;
		return half.boostMetres + (half.speed * on) +
		       (double{profile.sustainedAcceleration} * on * on / 2.0);
	};
	return seconds <= half.seconds ? out(seconds) : metres - out(whole - seconds);
}


ThrustSeconds Thrust(const MotionProfile &profile, double metres)
{
	if (metres <= 0.0)
		return {};
	const Half half = HalfOf(profile, metres);
	return {half.boostSeconds, half.thrustSeconds - half.boostSeconds};
}


void CheckProfile(std::string_view owner, const MotionProfile &profile)
{
	// Stated as what holds, so a NaN, which fails every comparison, is refused.
	const bool valid = profile.boostAcceleration > 0.0f && std::isfinite(profile.boostAcceleration) &&
	                   profile.sustainedAcceleration >= 0.0f &&
	                   profile.sustainedAcceleration <= profile.boostAcceleration &&
	                   profile.boostSeconds >= 0.0f && std::isfinite(profile.boostSeconds) &&
	                   (profile.boostSeconds > 0.0f || profile.sustainedAcceleration > 0.0f);
	if (!valid)
		throw Serialise::FormatError{std::format(
		    "{} has a profile whose boost is not positive, whose sustained acceleration is negative or "
		    "past the boost, or whose boost seconds are negative, with neither above 0",
		    owner)};
	if (profile.thrustShare && (!std::isfinite(*profile.thrustShare) || *profile.thrustShare <= 0.0f ||
	                            *profile.thrustShare > 1.0f))
		throw Serialise::FormatError{
		    std::format("{} has a profile whose thrust share is not above 0 and at most 1", owner)};
}
} // namespace Jettison::Transit
