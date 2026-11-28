#include <jettison/transit/Profile.h>

#include <jettison/core/Assert.h>
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
// rises with t, solved on the boost and, past it, at the sustained acceleration. A way with end
// speeds is part of a longer one from rest (EndSpeeds) that skips seconds of its thrust, s0 + s1:
// its thrust is 2t − s0 − s1, a share of T − s0 − s1, so (1 − share)(s0 + s1) v(t) joins the right.
Half Coasting(const MotionProfile &profile, double metres, double share, double skipped)
{
	const auto boost = double{profile.boostAcceleration};
	const auto sustained = double{profile.sustainedAcceleration};
	const double boostSeconds = double{profile.boostSeconds} / 2.0;
	const double ends = (1.0 - share) * skipped;

	// On the boost, (2 − share) a t² − ends × a t = share × metres; from rest, by the sums before
	// end speeds, so its times are theirs to the bit.
	const double onBoost =
	    skipped > 0.0 ? ((ends * boost) + std::sqrt(
	                                          (ends * boost * ends * boost) +
	                                          (4.0 * boost * (2.0 - share) * share * metres))) /
	                        (2.0 * boost * (2.0 - share))
	                  : std::sqrt(share * metres / (boost * (2.0 - share)));
	Half half;
	if (onBoost <= boostSeconds)
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

		// a u² + b u = c in the seconds u past the boost, by the root that does not cancel: the end
		// speeds' term can make b negative, which the other form of the root takes.
		const double a = sustained * (2.0 - share);
		const double b = (2.0 * (1.0 - share) * ((boostSeconds * sustained) + speed)) +
		                 (2.0 * share * speed) - (ends * sustained);
		const double c = (share * metres) - (2.0 * (1.0 - share) * boostSeconds * speed) -
		                 (2.0 * share * boostMetres) + (ends * speed);
		const double root = std::sqrt((b * b) + (4.0 * a * c));
		const double past = b < 0.0 ? (root - b) / (2.0 * a) : 2.0 * c / (b + root);
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


// The first half of a way from rest to rest, or of the longer way from rest a way with end speeds is
// part of, which skips that many seconds of its thrust.
Half HalfOf(const MotionProfile &profile, double metres, double skipped = 0.0)
{
	// A share of 1 thrusts to the midpoint, as none does, and by the same sums, so its times are
	// those of a profile saved before shares to the bit.
	if (profile.thrustShare && *profile.thrustShare < 1.0f)
		return Coasting(profile, metres, double{*profile.thrustShare}, skipped);
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


// The metres a way from rest to rest has covered that many seconds after setting out, short of its
// whole time.
double OnWay(const MotionProfile &profile, const Half &half, double metres, double seconds)
{
	const double whole = 2.0 * half.seconds;

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


// The seconds and metres the profile takes from rest to a speed, its boost first; none for a speed it
// never reaches, past its boost's with no sustained acceleration.
struct Reach
{
	double seconds{0.0};
	double metres{0.0};
};


std::optional<Reach> ReachOf(const MotionProfile &profile, double speed)
{
	const auto boost = double{profile.boostAcceleration};
	const auto sustained = double{profile.sustainedAcceleration};
	const double boostSeconds = double{profile.boostSeconds} / 2.0;
	const double boostSpeed = boost * boostSeconds;
	if (speed <= boostSpeed)
	{
		const double seconds = speed / boost;
		return Reach{seconds, boost * seconds * seconds / 2.0};
	}
	if (!(sustained > 0.0))
		return std::nullopt;
	const double past = (speed - boostSpeed) / sustained;
	return Reach{
	    boostSeconds + past, (boost * boostSeconds * boostSeconds / 2.0) + (boostSpeed * past) +
	                             (sustained * past * past / 2.0)};
}


// A way with end speeds as part of the longer way from rest to rest (EndSpeeds): each end's reach,
// the longer way's metres and its first half, or why the profile cannot meet them.
struct Ended
{
	Reach start;
	Reach end;
	double metres{0.0};
	Half half;
	std::optional<EndsUnmet> unmet;
};


Ended EndedOf(const MotionProfile &profile, double metres, const EndSpeeds &ends)
{
	Ended ended;
	if (!(ends.start >= 0.0) || !(ends.end >= 0.0) || !std::isfinite(ends.start) ||
	    !std::isfinite(ends.end))
	{
		ended.unmet = EndsUnmet::Backward;
		return ended;
	}
	const std::optional<Reach> start = ReachOf(profile, ends.start);
	const std::optional<Reach> end = ReachOf(profile, ends.end);
	if (!start || !end)
	{
		ended.unmet = EndsUnmet::AbovePeak;
		return ended;
	}
	ended.start = *start;
	ended.end = *end;
	if (metres <= 0.0)
	{
		if (ends.start != ends.end)
			ended.unmet = EndsUnmet::Unreachable;
		return ended;
	}

	// Each end on the longer way's thrust, which a way too short for the change of speed puts past
	// its midpoint, and a share too small past its turn to coasting.
	ended.metres = metres + start->metres + end->metres;
	const double latest = std::max(start->seconds, end->seconds);
	MotionProfile throughout = profile;
	throughout.thrustShare.reset();
	ended.half = HalfOf(profile, ended.metres, start->seconds + end->seconds);
	if (HalfOf(throughout, ended.metres).seconds < latest)
		ended.unmet = EndsUnmet::Unreachable;
	else if (ended.half.thrustSeconds < latest)
		ended.unmet = EndsUnmet::AbovePeak;
	return ended;
}
} // namespace


std::optional<EndsUnmet> CheckEnds(const MotionProfile &profile, double metres, const EndSpeeds &ends)
{
	return EndedOf(profile, metres, ends).unmet;
}


double Seconds(const MotionProfile &profile, double metres, const EndSpeeds &ends)
{
	if (metres <= 0.0)
		return 0.0;
	if (ends == EndSpeeds{})
		return 2.0 * HalfOf(profile, metres).seconds;
	const Ended ended = EndedOf(profile, metres, ends);
	JETTISON_ASSERT(!ended.unmet);
	return (2.0 * ended.half.seconds) - ended.start.seconds - ended.end.seconds;
}


double Covered(const MotionProfile &profile, double metres, double seconds, const EndSpeeds &ends)
{
	if (metres <= 0.0 || seconds <= 0.0)
		return 0.0;
	if (ends == EndSpeeds{})
	{
		const Half half = HalfOf(profile, metres);
		return seconds >= 2.0 * half.seconds ? metres : OnWay(profile, half, metres, seconds);
	}
	const Ended ended = EndedOf(profile, metres, ends);
	JETTISON_ASSERT(!ended.unmet);
	if (seconds >= (2.0 * ended.half.seconds) - ended.start.seconds - ended.end.seconds)
		return metres;
	const double covered =
	    OnWay(profile, ended.half, ended.metres, seconds + ended.start.seconds) - ended.start.metres;
	return std::clamp(covered, 0.0, metres);
}


ThrustSeconds Thrust(const MotionProfile &profile, double metres)
{
	if (metres <= 0.0)
		return {};
	const Half half = HalfOf(profile, metres);
	return {half.boostSeconds, half.thrustSeconds - half.boostSeconds};
}


EndThrust Thrust(const MotionProfile &profile, double metres, const EndSpeeds &ends)
{
	if (metres <= 0.0)
		return {};
	if (ends == EndSpeeds{})
	{
		const ThrustSeconds each = Thrust(profile, metres);
		return {each, each};
	}
	const Ended ended = EndedOf(profile, metres, ends);
	JETTISON_ASSERT(!ended.unmet);

	// An end's thrust is the longer way's less what reaching its speed from rest took.
	const Half &half = ended.half;
	const auto from = [&](double reached) -> ThrustSeconds
	{
		return {
		    std::max(0.0, half.boostSeconds - reached),
		    half.thrustSeconds - std::max(half.boostSeconds, reached)};
	};
	return {from(ended.start.seconds), from(ended.end.seconds)};
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
