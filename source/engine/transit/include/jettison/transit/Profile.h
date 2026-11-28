#pragma once

#include <optional>
#include <string_view>

namespace Jettison::Transit
{
// A motion profile limited by acceleration, turning at the midpoint: the boost acceleration for at
// most boostSeconds of the whole way, half of it speeding up from the start and half slowing down
// to the end, and the sustained acceleration between, so a long way takes days where a short one
// stays at the boost throughout. In m/s² and seconds. With a thrust share, the share of the whole
// time the drive thrusts, above 0 and at most 1, half at each end, the boost first, and it coasts
// between at the speed it reached: a share rather than seconds, so it means the same at any
// distance. None, or 1, thrusts to the midpoint. At rest at both ends, unless a way gives it speeds
// at its ends (EndSpeeds). Registered with Reflect, every field Saved and Moddable, by RegisterTypes;
// a profile with no share saves as one did before.
struct MotionProfile
{
	float boostAcceleration{0.0f};
	float boostSeconds{0.0f};
	float sustainedAcceleration{0.0f};
	std::optional<float> thrustShare{};

	bool operator==(const MotionProfile &) const = default;
};

// The speeds along its way a profile sets out at and arrives at, in m/s, at rest by default. It then
// moves as it would from rest to rest over a longer way, the metres each end's speed takes to reach
// from rest added, from where it reaches the start's speed to where it is down to the end's: an
// end's boost is spent at its slowest, so an end already moving spends less of it. A thrust share
// stays the share of the way's own time that the drive thrusts, and the profile peaks at the speed
// its thrust ends at. Ends at rest give what a profile gave before end speeds, to the bit.
struct EndSpeeds
{
	double start{0.0};
	double end{0.0};

	bool operator==(const EndSpeeds &) const = default;
};

// Why a profile cannot meet a way's end speeds: one is negative or not finite (backward), one is
// above the speed the profile peaks at with its share or, with a sustained acceleration of 0, the
// speed its boost reaches, or the way is too short to speed up or slow down from one to the other.
enum class EndsUnmet
{
	Backward,
	AbovePeak,
	Unreachable,
};

// Whether a profile meets those end speeds over that many metres: none when it does, or why not.
// Seconds, Covered and Thrust need ends a profile meets, and are asserted to have them.
std::optional<EndsUnmet> CheckEnds(const MotionProfile &profile, double metres, const EndSpeeds &ends);

// The seconds the profile takes over that many metres. None for no distance.
double Seconds(const MotionProfile &profile, double metres, const EndSpeeds &ends = {});

// The metres the profile has covered of that many that many seconds after setting out, from none at
// 0 to all of them at Seconds and after.
double Covered(const MotionProfile &profile, double metres, double seconds, const EndSpeeds &ends = {});

// The seconds the profile thrusts at each end of a way: at the boost, then at the sustained
// acceleration.
struct ThrustSeconds
{
	double boost{0.0};
	double sustained{0.0};

	bool operator==(const ThrustSeconds &) const = default;
};

// How long the profile thrusts at each end over that many metres, so a game can price what its burns
// cost: each phase's speed change is its acceleration times its seconds. None for no distance.
ThrustSeconds Thrust(const MotionProfile &profile, double metres);

// The seconds the profile thrusts at a way's start and at its end, which differ when its end speeds
// do; each is Thrust's at rest.
struct EndThrust
{
	ThrustSeconds start;
	ThrustSeconds end;

	bool operator==(const EndThrust &) const = default;
};

// How long the profile thrusts at each end over that many metres with those end speeds: speeding up
// from the start's speed and slowing down to the end's. None for no distance.
EndThrust Thrust(const MotionProfile &profile, double metres, const EndSpeeds &ends);

// Checks a profile a game defines: its boost positive, its sustained acceleration at most the boost
// and not negative, its boost seconds not negative, one of those two above 0 so a way of any length
// ends, its thrust share, if it has one, above 0 and at most 1, and all finite. Throws
// Serialise::FormatError naming what has the profile, as "<owner> has a profile whose ...".
void CheckProfile(std::string_view owner, const MotionProfile &profile);
} // namespace Jettison::Transit
