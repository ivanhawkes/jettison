#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <jettison/serialise/Archive.h>
#include <jettison/transit/Path.h>
#include <jettison/transit/Profile.h>

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


TEST_CASE("Profile: with its ends at rest, it gives what it gave before end speeds, to the bit")
{
	// Seconds, Covered at 0.3 and 0.7 of them, and Thrust's boost and sustained seconds, as the
	// profile gave them before end speeds (2026-11-28).
	const Transit::MotionProfile profiles[] = {{19.6133f, 14400.0f, 9.80665f},
	                                           {117.6798f, 0.0f, 117.6798f},
	                                           {1.2f, 4.0f, 0.0f},
	                                           {19.6133f, 14400.0f, 9.80665f, 0.25f},
	                                           {9.80665f, 0.0f, 9.80665f, 0.1f},
	                                           {20.0f, 60.0f, 0.0f, 0.5f}};
	struct Pinned
	{
		int profile;
		double metres;
		double seconds;
		double early;
		double late;
		double boost;
		double sustained;
	};
	const Pinned pinned[] = {
	    {0, 0x1.4p+5, 0x1.6d971b8da40bp+1, 0x1.ccccccccccccep+2, 0x1.0666666666666p+5,
	     0x1.6d971b8da40bp+0, 0x0p+0},
	    {0, 0x1.e848p+19, 0x1.c399cae2879b8p+8, 0x1.5f9p+17, 0x1.9064p+19, 0x1.c399cae2879b8p+7,
	     0x0p+0},
	    {0, 0x1.7d784p+26, 0x1.1a401ecd94c13p+12, 0x1.12a88p+24, 0x1.38ce2p+26, 0x1.1a401ecd94c13p+11,
	     0x0p+0},
	    {0, 0x1.70cdc1ep+38, 0x1.7aebbab118963p+18, 0x1.152e16b2c4026p+36, 0x1.2b823c334eff6p+38,
	     0x1.c2p+12, 0x1.6cdbbab118963p+17},
	    {1, 0x1.4p+5, 0x1.2a80d1c3399c2p+0, 0x1.ccccccccccccep+2, 0x1.0666666666666p+5, 0x0p+0,
	     0x1.2a80d1c3399c2p-1},
	    {1, 0x1.e848p+19, 0x1.70bb00148d1p+7, 0x1.5f8fffffffffep+17, 0x1.9064p+19, 0x0p+0,
	     0x1.70bb00148d1p+6},
	    {1, 0x1.7d784p+26, 0x1.cce9c019b0541p+10, 0x1.12a87ffffffffp+24, 0x1.38ce1ffffffffp+26, 0x0p+0,
	     0x1.cce9c019b0541p+9},
	    {1, 0x1.70cdc1ep+38, 0x1.c53255772fcf4p+16, 0x1.0989e7cp+36, 0x1.2e6b47fp+38, 0x0p+0,
	     0x1.c53255772fcf4p+15},
	    {2, 0x1.4p+5, 0x1.2aaaa9f8e38ebp+4, 0x1.6147aep+3, 0x1.cf5c29p+4, 0x1p+1, 0x1.d55553f1c71d6p+2},
	    {2, 0x1.e848p+19, 0x1.96e7299b6639ap+18, 0x1.24f7c28f5cp+18, 0x1.55cc1eb852p+19, 0x1p+1,
	     0x1.96e6299b6639ap+17},
	    {2, 0x1.7d784p+26, 0x1.3de4358167ddp+25, 0x1.c9c37f0a3d7p+24, 0x1.0b07603d70a4p+26, 0x1p+1,
	     0x1.3de4338167ddp+24},
	    {2, 0x1.70cdc1ep+38, 0x1.335620c32bea3p+37, 0x1.ba90823ff0a3ep+36, 0x1.0229a15003d7p+38, 0x1p+1,
	     0x1.335620c30bea3p+36},
	    {3, 0x1.4p+5, 0x1.145c3a33716afp+2, 0x1.5b6db6db6db6dp+3, 0x1.d249249249248p+4,
	     0x1.145c3a33716afp-1, 0x0p+0},
	    {3, 0x1.e848p+19, 0x1.5560c9e23ca9ep+9, 0x1.0911249249249p+18, 0x1.63bf6db6db6dbp+19,
	     0x1.5560c9e23ca9dp+6, 0x0p+0},
	    {3, 0x1.7d784p+26, 0x1.aab8fc5acbd45p+12, 0x1.9e2ac92492492p+24, 0x1.15ed8db6db6dcp+26,
	     0x1.aab8fc5acbd45p+9, 0x0p+0},
	    {3, 0x1.70cdc1ep+38, 0x1.193d8fb1aa2bap+19, 0x1.944faeff43206p+36, 0x1.0bb9d6202f37ep+38,
	     0x1.c2p+12, 0x1.fa3b1f6354574p+15},
	    {4, 0x1.4p+5, 0x1.288869718cf13p+3, 0x1.7286bc9e23751p+3, 0x1.c6bca1b0ee457p+4, 0x0p+0,
	     0x1.da73dc5f7e78fp-2},
	    {4, 0x1.e848p+19, 0x1.6e4bec6cb0f24p+10, 0x1.1ab06bc7666d3p+18, 0x1.5aefca1c4cc96p+19, 0x0p+0,
	     0x1.25098a39cff12p+6},
	    {4, 0x1.7d784p+26, 0x1.c9dee787dd2efp+13, 0x1.b9b3a867900abp+24, 0x1.0f0b55e61bfd5p+26, 0x0p+0,
	     0x1.6e4becc843ed7p+9},
	    {4, 0x1.70cdc1ep+38, 0x1.c23486eefdc42p+19, 0x1.ab0931501fc26p+36, 0x1.060b758bf80f6p+38,
	     0x0p+0, 0x1.682a05e608b7cp+15},
	    {5, 0x1.4p+5, 0x1.a20bd700c2c3ep+1, 0x1.2aaaaaaaaaaaap+3, 0x1.eaaaaaaaaaaaap+4,
	     0x1.a20bd700c2c3ep-1, 0x0p+0},
	    {5, 0x1.e848p+19, 0x1.a82aaaaaaaaabp+10, 0x1.2174p+18, 0x1.578ep+19, 0x1.ep+4,
	     0x1.8a2aaaaaaaaabp+8},
	    {5, 0x1.7d784p+26, 0x1.4594555555555p+17, 0x1.c9b56ffffffffp+24, 0x1.0b0ae4p+26, 0x1.ep+4,
	     0x1.4558555555555p+15},
	    {5, 0x1.70cdc1ep+38, 0x1.3ab668fp+29, 0x1.ba90815fp+36, 0x1.0229a1884p+38, 0x1.ep+4,
	     0x1.3ab6653p+27},
	};
	for (const Pinned &pin : pinned)
	{
		const Transit::MotionProfile &profile = profiles[pin.profile];
		CAPTURE(pin.profile);
		CAPTURE(pin.metres);
		const Transit::EndSpeeds rest;
		CHECK(Transit::Seconds(profile, pin.metres) == pin.seconds);
		CHECK(Transit::Seconds(profile, pin.metres, rest) == pin.seconds);
		CHECK(Transit::Covered(profile, pin.metres, pin.seconds * 0.3) == pin.early);
		CHECK(Transit::Covered(profile, pin.metres, pin.seconds * 0.7, rest) == pin.late);
		const Transit::ThrustSeconds thrust{pin.boost, pin.sustained};
		CHECK(Transit::Thrust(profile, pin.metres) == thrust);
		CHECK(Transit::Thrust(profile, pin.metres, rest) == Transit::EndThrust{thrust, thrust});
		CHECK_FALSE(Transit::CheckEnds(profile, pin.metres, rest));
	}
}


TEST_CASE("Profile: with a speed at each end, its time, way and thrust are what integrating gives")
{
	struct Case
	{
		Transit::MotionProfile profile;
		double metres;
		Transit::EndSpeeds ends;
	};
	const auto g = static_cast<float>(kStandardGravity);
	const Case cases[] = {
	    // 2 g for 4 hours and 1 g between, both ends on the boost, then one past it.
	    {{2.0f * g, 14400.0f, g}, 1.0e9, {5000.0, 20000.0}},
	    {{2.0f * g, 14400.0f, g}, 3.96e11, {2.0e5, 3.0e4}},
	    // The same thrusting for a quarter of the time, and 12 g for a hundredth of it.
	    {{2.0f * g, 14400.0f, g, 0.25f}, 3.96e11, {3.0e4, 1.0e5}},
	    {{12.0f * g, 0.0f, 12.0f * g, 0.01f}, 3.96e11, {3.0e4, 2.5e4}},
	    // A lift setting out at 1 m/s and stopping, and from rest arriving at its speed between.
	    {{1.2f, 4.0f, 0.0f}, 40.0, {1.0, 0.0}},
	    {{1.2f, 4.0f, 0.0f}, 40.0, {0.0, 2.4}},
	};
	for (const Case &each : cases)
	{
		const Transit::MotionProfile &profile = each.profile;
		CAPTURE(each.metres);
		CAPTURE(each.ends.start);
		CAPTURE(each.ends.end);
		REQUIRE_FALSE(Transit::CheckEnds(profile, each.metres, each.ends));
		const double seconds = Transit::Seconds(profile, each.metres, each.ends);
		const Transit::EndThrust thrust = Transit::Thrust(profile, each.metres, each.ends);
		const auto boost = double{profile.boostAcceleration};
		const auto sustained = double{profile.sustainedAcceleration};
		const double boostSpeed = boost * double{profile.boostSeconds} / 2.0;
		const double out = thrust.start.boost + thrust.start.sustained;
		const double in = thrust.end.boost + thrust.end.sustained;
		REQUIRE(out + in <= seconds * (1.0 + 1.0e-12));
		if (profile.thrustShare)
			CHECK(
			    (out + in) / seconds == doctest::Approx(double{*profile.thrustShare}).epsilon(1.0e-9));

		// Speeding up at the boost, then the sustained; a coast; then slowing down at the sustained,
		// then the boost: each phase stepped through.
		const std::pair<double, double> phases[] = {
		    {boost, thrust.start.boost},
		    {sustained, thrust.start.sustained},
		    {0.0, std::max(0.0, seconds - out - in)},
		    {-sustained, thrust.end.sustained},
		    {-boost, thrust.end.boost}};
		constexpr int kSteps = 20000;
		double speed = each.ends.start;
		double covered = 0.0;
		double time = 0.0;
		double most = 0.0;
		for (std::size_t phase = 0; phase < std::size(phases); ++phase)
		{
			// The boost is spent at the slow ends of the way, the sustained above its speed.
			const auto [a, duration] = phases[phase];
			const double reached = speed + (a * duration);
			if (duration > 0.0 && (phase == 0 || phase == 4))
				CHECK(std::max(speed, reached) <= boostSpeed * (1.0 + 1.0e-9));
			else if (duration > 0.0 && (phase == 1 || phase == 3))
				CHECK(std::min(speed, reached) >= boostSpeed * (1.0 - 1.0e-9));
			const double step = duration / kSteps;
			for (int index = 0; index < kSteps && duration > 0.0; ++index)
			{
				const double next = speed + (a * step);
				covered += (speed + next) / 2.0 * step;
				speed = next;
				time += step;
				most = std::max(
				    most, std::abs(Transit::Covered(profile, each.metres, time, each.ends) - covered));
			}
		}
		CHECK(covered == doctest::Approx(each.metres).epsilon(1.0e-9));
		CHECK(std::abs(speed - each.ends.end) <= 1.0e-9 * (boostSpeed + each.ends.end));
		CHECK(most <= each.metres * 1.0e-9);
		CHECK(Transit::Covered(profile, each.metres, 0.0, each.ends) == 0.0);
		CHECK(Transit::Covered(profile, each.metres, seconds, each.ends) == each.metres);

		// Leaving at its speed and arriving at the end's.
		const double instant = seconds * 1.0e-7;
		const double leaving = Transit::Covered(profile, each.metres, instant, each.ends) / instant;
		const double arriving =
		    (each.metres - Transit::Covered(profile, each.metres, seconds - instant, each.ends)) /
		    instant;
		CHECK(std::abs(leaving - each.ends.start) <= (boost * instant) + (1.0e-6 * each.ends.start));
		CHECK(std::abs(arriving - each.ends.end) <= (boost * instant) + (1.0e-6 * each.ends.end));
	}

	// A start at the speed a way from rest turns at takes it all at that speed and its slowing down.
	const Transit::MotionProfile manned{2.0f * g, 14400.0f, g};
	const double turn = std::sqrt(double{manned.boostAcceleration} * 1.0e8) * (1.0 - 1.0e-12);
	REQUIRE_FALSE(Transit::CheckEnds(manned, 5.0e7, {turn, 0.0}));
	CHECK(
	    Transit::Seconds(manned, 5.0e7, {turn, 0.0}) ==
	    doctest::Approx(Transit::Seconds(manned, 1.0e8) / 2.0).epsilon(1.0e-9));
}


TEST_CASE("Profile: end speeds it cannot meet are refused, with the reason")
{
	const float infinity = std::numeric_limits<float>::infinity();
	const Transit::MotionProfile lift{1.2f, 4.0f, 0.0f};
	for (const double speed : {-1.0, double{infinity}, std::numeric_limits<double>::quiet_NaN()})
	{
		CHECK(Transit::CheckEnds(lift, 40.0, {speed, 0.0}) == Transit::EndsUnmet::Backward);
		CHECK(Transit::CheckEnds(lift, 40.0, {0.0, speed}) == Transit::EndsUnmet::Backward);
	}

	// A lift's speed between is 2.4 m/s, which takes it 2.4 m to reach or to stop from: above it
	// is refused, as are 1 m or 2.3 m to reach it or stop in, and a change of speed over no way.
	CHECK(Transit::CheckEnds(lift, 40.0, {2.5, 0.0}) == Transit::EndsUnmet::AbovePeak);
	CHECK_FALSE(Transit::CheckEnds(lift, 40.0, {2.4, 2.4}));
	CHECK(Transit::CheckEnds(lift, 1.0, {2.4, 0.0}) == Transit::EndsUnmet::Unreachable);
	CHECK(Transit::CheckEnds(lift, 1.0, {0.0, 2.4}) == Transit::EndsUnmet::Unreachable);
	CHECK_FALSE(Transit::CheckEnds(lift, 2.5, {2.4, 0.0}));
	CHECK(Transit::CheckEnds(lift, 2.3, {2.4, 0.0}) == Transit::EndsUnmet::Unreachable);
	CHECK(Transit::CheckEnds(lift, 0.0, {1.0, 0.5}) == Transit::EndsUnmet::Unreachable);
	CHECK_FALSE(Transit::CheckEnds(lift, 0.0, {1.0, 1.0}));
	CHECK(Transit::Seconds(lift, 0.0, {1.0, 1.0}) == 0.0);

	// Thrusting for a tenth of the time at 1 g over 396 million km: 300 km/s at each end it
	// meets; it cannot slow from 1,000 km/s to rest in a tenth of the time, though it could
	// thrusting throughout, nor from 3,000 km/s at all.
	const auto g = static_cast<float>(kStandardGravity);
	const Transit::MotionProfile tenth{g, 0.0f, g, 0.1f};
	CHECK_FALSE(Transit::CheckEnds(tenth, 3.96e11, {3.0e5, 3.0e5}));
	CHECK(Transit::CheckEnds(tenth, 3.96e11, {1.0e6, 0.0}) == Transit::EndsUnmet::AbovePeak);
	CHECK(Transit::CheckEnds(tenth, 3.96e11, {0.0, 1.0e6}) == Transit::EndsUnmet::AbovePeak);
	CHECK(Transit::CheckEnds(tenth, 3.96e11, {3.0e6, 0.0}) == Transit::EndsUnmet::Unreachable);
	Transit::MotionProfile throughout = tenth;
	throughout.thrustShare.reset();
	CHECK_FALSE(Transit::CheckEnds(throughout, 3.96e11, {1.0e6, 0.0}));
}


TEST_CASE("Profile: a velocity at each end is carried along the way by the profile and across by Drift")
{
	// An L of 10^9 m, leaving with a velocity across its first leg and partly back along it, and
	// arriving with one partly along its last.
	const auto g = static_cast<float>(kStandardGravity);
	const Transit::MotionProfile profile{2.0f * g, 14400.0f, g};
	const std::vector<glm::dvec3> path{{0.0, 0.0, 0.0}, {6.0e8, 0.0, 0.0}, {6.0e8, 4.0e8, 0.0}};
	const double length = Transit::Length(path);
	const glm::dvec3 leaving{-2000.0, 3000.0, 500.0};
	const glm::dvec3 arriving{1000.0, 4000.0, 0.0};
	const Transit::Split start = Transit::SplitAlong(leaving, {1.0, 0.0, 0.0});
	const Transit::Split end = Transit::SplitAlong(arriving, {0.0, 1.0, 0.0});
	CHECK(start.along == 0.0);
	CHECK(start.across == leaving);
	CHECK(end.along == 4000.0);
	CHECK(end.across == glm::dvec3{1000.0, 0.0, 0.0});
	const Transit::EndSpeeds ends{start.along, end.along};
	REQUIRE_FALSE(Transit::CheckEnds(profile, length, ends));
	const double seconds = Transit::Seconds(profile, length, ends);
	const auto at = [&](double time)
	{
		return Transit::Along(path, Transit::Covered(profile, length, time, ends) / length).first +
		       Transit::Drift(start.across, end.across, seconds, time);
	};
	CHECK(at(0.0) == path.front());
	CHECK(at(seconds) == path.back());
	const double instant = 0.01;
	CHECK(glm::length(((at(instant) - at(0.0)) / instant) - leaving) < 0.5);
	CHECK(glm::length(((at(seconds) - at(seconds - instant)) / instant) - arriving) < 0.5);

	// None at either end, and no further off the way than 4/27 of the time at each end's velocity.
	CHECK(Transit::Drift(start.across, end.across, seconds, 0.0) == glm::dvec3{0.0});
	CHECK(Transit::Drift(start.across, end.across, seconds, seconds) == glm::dvec3{0.0});
	CHECK(Transit::Drift(start.across, end.across, 0.0, 0.0) == glm::dvec3{0.0});
	for (int index = 0; index <= 100; ++index)
		CHECK(
		    glm::length(Transit::Drift(start.across, end.across, seconds, seconds * index / 100.0)) <=
		    seconds * 4.0 / 27.0 * (glm::length(start.across) + glm::length(end.across)));
}
