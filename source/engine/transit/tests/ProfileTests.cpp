#include <jettison/serialise/Archive.h>
#include <jettison/transit/Profile.h>

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace Jettison;

namespace
{
constexpr double kStandardGravity = 9.80665;
} // namespace


TEST_CASE("Profile: its time and what it covers are its closed form")
{
	// 2 g for 4 hours, 2 at each end, and 1 g between.
	const Transit::MotionProfile manned{
	    static_cast<float>(2.0 * kStandardGravity), 14400.0f, static_cast<float>(kStandardGravity)};
	const auto boost = double{manned.boostAcceleration};
	const auto sustained = double{manned.sustainedAcceleration};

	// Short of the boost's distance it is at the boost throughout: 2 √(d / a).
	CHECK(Transit::Seconds(manned, 1.0e6) == doctest::Approx(2.0 * std::sqrt(1.0e6 / boost)));
	CHECK(Transit::Seconds(manned, 0.0) == 0.0);

	// Two hours at the boost and one at the sustained rate each way.
	const double boosted = boost * 7200.0 * 7200.0 / 2.0;
	const double speed = boost * 7200.0;
	const double half = boosted + (speed * 3600.0) + (sustained * 3600.0 * 3600.0 / 2.0);
	CHECK(Transit::Seconds(manned, 2.0 * half) == doctest::Approx(21600.0).epsilon(1.0e-12));
	CHECK(Transit::Covered(manned, 2.0 * half, 0.0) == 0.0);
	CHECK(Transit::Covered(manned, 2.0 * half, 3600.0) == doctest::Approx(boost * 3600.0 * 1800.0));
	CHECK(Transit::Covered(manned, 2.0 * half, 7200.0) == doctest::Approx(boosted));
	CHECK(Transit::Covered(manned, 2.0 * half, 10800.0) == doctest::Approx(half));
	CHECK(
	    Transit::Covered(manned, 2.0 * half, 21600.0 - 3600.0) ==
	    doctest::Approx((2.0 * half) - (boost * 3600.0 * 1800.0)));
	CHECK(Transit::Covered(manned, 2.0 * half, 21600.0) == 2.0 * half);
	CHECK(Transit::Covered(manned, 2.0 * half, 1.0e9) == 2.0 * half);

	// Unmanned, 12 g throughout.
	const auto twelve = static_cast<float>(12.0 * kStandardGravity);
	const Transit::MotionProfile unmanned{twelve, 0.0f, twelve};
	CHECK(
	    Transit::Seconds(unmanned, 4.0e11) ==
	    doctest::Approx(2.0 * std::sqrt(4.0e11 / double{twelve})));
	CHECK_NOTHROW(Transit::CheckProfile("manned", manned));
	CHECK_NOTHROW(Transit::CheckProfile("unmanned", unmanned));
	CHECK_NOTHROW(Transit::CheckProfile("coasting", Transit::MotionProfile{20.0f, 60.0f}));
}


TEST_CASE("Profile: thrusting for a share of the time, it coasts between, in closed form")
{
	// 1 g throughout over Ore to Brine's 396 million km (Manifest's W21 plan): a share f reaches
	// s = √(f / (2 − f)) of the full thrust's speed, so the way takes (1 + s²) / 2s times as long.
	const auto g = static_cast<float>(kStandardGravity);
	const double metres = 3.96e11;
	const Transit::MotionProfile full{g, 0.0f, g};
	const double fullSeconds = Transit::Seconds(full, metres);
	for (const float share : {0.5f, 0.25f, 0.1f})
	{
		Transit::MotionProfile coasting = full;
		coasting.thrustShare = share;
		const double reached = std::sqrt(double{share} / (2.0 - double{share}));
		const double seconds = Transit::Seconds(coasting, metres);
		CHECK(
		    seconds == doctest::Approx(fullSeconds * (1.0 + (reached * reached)) / (2.0 * reached))
		                   .epsilon(1.0e-12));
		const Transit::ThrustSeconds thrust = Transit::Thrust(coasting, metres);
		CHECK(thrust.boost == 0.0);
		CHECK(2.0 * thrust.sustained / seconds == doctest::Approx(double{share}).epsilon(1.0e-12));

		// Thrusting from rest, then coasting at the speed reached to the turn and on to the burn in.
		const double thrusted = double{g} * thrust.sustained * thrust.sustained / 2.0;
		const double top = double{g} * thrust.sustained;
		CHECK(Transit::Covered(coasting, metres, thrust.sustained) == doctest::Approx(thrusted));
		CHECK(
		    Transit::Covered(coasting, metres, seconds / 2.0) ==
		    doctest::Approx(metres / 2.0).epsilon(1.0e-12));
		CHECK(
		    Transit::Covered(coasting, metres, (seconds / 2.0) + 3600.0) ==
		    doctest::Approx((metres / 2.0) + (top * 3600.0)).epsilon(1.0e-12));
		CHECK(
		    Transit::Covered(coasting, metres, seconds - thrust.sustained) ==
		    doctest::Approx(metres - thrusted));
		CHECK(Transit::Covered(coasting, metres, seconds) == metres);
		CHECK_NOTHROW(Transit::CheckProfile("coasting", coasting));
	}
	Transit::MotionProfile half = full;
	half.thrustShare = 0.5f;
	MESSAGE(
	    "Ore to Brine at 1 g: " << fullSeconds / 86400.0 << " days thrusting throughout, "
	                            << Transit::Seconds(half, metres) / 86400.0 << " at half the time");
	CHECK(fullSeconds / 86400.0 == doctest::Approx(4.65).epsilon(0.001));
	CHECK(Transit::Seconds(half, metres) / 86400.0 == doctest::Approx(5.37).epsilon(0.001));

	// 2 g for 4 hours, 2 at each end, and 1 g between: a quarter of a long way thrusts past the
	// boost, a quarter of a short one only on it.
	Transit::MotionProfile manned{
	    static_cast<float>(2.0 * kStandardGravity), 14400.0f, static_cast<float>(kStandardGravity),
	    0.25f};
	for (const double distance : {metres, 1.0e8})
	{
		const double seconds = Transit::Seconds(manned, distance);
		const Transit::ThrustSeconds thrust = Transit::Thrust(manned, distance);
		CHECK(
		    2.0 * (thrust.boost + thrust.sustained) / seconds ==
		    doctest::Approx(0.25).epsilon(1.0e-12));
		const auto boost = double{manned.boostAcceleration};
		const double boosted = boost * thrust.boost * thrust.boost / 2.0;
		const auto sustained = double{manned.sustainedAcceleration};
		const double thrusted = boosted + (boost * thrust.boost * thrust.sustained) +
		                        (sustained * thrust.sustained * thrust.sustained / 2.0);
		CHECK(Transit::Covered(manned, distance, thrust.boost) == doctest::Approx(boosted));
		CHECK(
		    Transit::Covered(manned, distance, thrust.boost + thrust.sustained) ==
		    doctest::Approx(thrusted));
		CHECK(
		    Transit::Covered(manned, distance, seconds / 2.0) ==
		    doctest::Approx(distance / 2.0).epsilon(1.0e-12));
	}
	CHECK(Transit::Thrust(manned, metres).boost == 7200.0);
	CHECK(Transit::Thrust(manned, metres).sustained > 0.0);
	CHECK(Transit::Thrust(manned, 1.0e8).boost < 7200.0);
	CHECK(Transit::Thrust(manned, 1.0e8).sustained == 0.0);
	CHECK(Transit::Thrust(manned, 0.0) == Transit::ThrustSeconds{});

	// A share of 1 is none, to the bit, and thrusts to the midpoint.
	const Transit::MotionProfile throughout{
	    manned.boostAcceleration, manned.boostSeconds, manned.sustainedAcceleration};
	manned.thrustShare = 1.0f;
	CHECK(Transit::Seconds(manned, metres) == Transit::Seconds(throughout, metres));
	for (const double at : {3600.0, 7200.0, 1.0e5, 3.0e5})
		CHECK(Transit::Covered(manned, metres, at) == Transit::Covered(throughout, metres, at));
	const Transit::ThrustSeconds thrust = Transit::Thrust(throughout, metres);
	CHECK(
	    2.0 * (thrust.boost + thrust.sustained) ==
	    doctest::Approx(Transit::Seconds(throughout, metres)).epsilon(1.0e-12));
	CHECK_NOTHROW(Transit::CheckProfile("throughout", manned));
}


TEST_CASE("Profile: one of no boost, or past it, or that never ends, is refused")
{
	const float infinity = std::numeric_limits<float>::infinity();
	for (const Transit::MotionProfile profile :
	     {Transit::MotionProfile{0.0f, 60.0f, 0.0f}, Transit::MotionProfile{infinity, 60.0f, 10.0f},
	      Transit::MotionProfile{20.0f, -1.0f, 10.0f}, Transit::MotionProfile{20.0f, infinity, 10.0f},
	      Transit::MotionProfile{20.0f, 60.0f, -1.0f}, Transit::MotionProfile{20.0f, 60.0f, 30.0f},
	      Transit::MotionProfile{20.0f, 0.0f, 0.0f}})
		CHECK_THROWS_AS(Transit::CheckProfile("bad", profile), Serialise::FormatError);

	// A thrust share not above 0 and at most 1.
	for (const float share : {0.0f, -0.5f, 1.5f, infinity, std::numeric_limits<float>::quiet_NaN()})
		CHECK_THROWS_AS(
		    Transit::CheckProfile("bad", Transit::MotionProfile{20.0f, 60.0f, 10.0f, share}),
		    Serialise::FormatError);
}
