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
// distance. None, or 1, thrusts to the midpoint. At rest at both ends. Registered with Reflect,
// every field Saved and Moddable, by RegisterTypes; a profile with no share saves as one did before.
struct MotionProfile
{
	float boostAcceleration{0.0f};
	float boostSeconds{0.0f};
	float sustainedAcceleration{0.0f};
	std::optional<float> thrustShare{};

	bool operator==(const MotionProfile &) const = default;
};

// The seconds the profile takes over that many metres. None for no distance.
double Seconds(const MotionProfile &profile, double metres);

// The metres the profile has covered of that many that many seconds after setting out, from none at
// 0 to all of them at Seconds and after.
double Covered(const MotionProfile &profile, double metres, double seconds);

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

// Checks a profile a game defines: its boost positive, its sustained acceleration at most the boost
// and not negative, its boost seconds not negative, one of those two above 0 so a way of any length
// ends, its thrust share, if it has one, above 0 and at most 1, and all finite. Throws
// Serialise::FormatError naming what has the profile, as "<owner> has a profile whose ...".
void CheckProfile(std::string_view owner, const MotionProfile &profile);
} // namespace Jettison::Transit
