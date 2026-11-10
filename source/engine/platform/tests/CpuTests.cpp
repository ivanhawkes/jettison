#include <jettison/platform/Cpu.h>

#include <doctest/doctest.h>

#include <optional>
#include <string>

using namespace Jettison::Platform;

namespace
{
constexpr CpuFeatures kHaswell{.avx2 = true, .bmi1 = true, .lzcnt = true, .popcnt = true, .f16c = true};
} // namespace

// The machines the gate runs on meet the baseline by decision (docs/roadmap.md, "Decided"), so a
// failure here is a machine the engine does not support, or a detection that has gone wrong. This
// test executable got this far, so the start-up guard agreed.
TEST_CASE("Cpu: this machine meets the baseline")
{
	const CpuFeatures features = DetectCpuFeatures();
	CHECK(features.avx2);
	CHECK(features.bmi1);
	CHECK(features.lzcnt);
	CHECK(features.popcnt);
	CHECK(features.f16c);
	CHECK(CheckCpuBaseline() == std::nullopt);
}

TEST_CASE("Cpu: a machine with every feature meets the baseline")
{
	CHECK(CheckCpuBaseline(kHaswell) == std::nullopt);
}

TEST_CASE("Cpu: a machine below the baseline is told what it is missing")
{
	CpuFeatures sandyBridge = kHaswell;
	sandyBridge.avx2 = false;
	sandyBridge.bmi1 = false;
	sandyBridge.lzcnt = false;
	sandyBridge.f16c = false;
	const std::optional<std::string> message = CheckCpuBaseline(sandyBridge);
	REQUIRE(message.has_value());
	CHECK(message->starts_with("Jettison needs an x86-64 CPU with AVX2"));
	CHECK(message->ends_with("This one is missing: AVX2, BMI1, LZCNT, F16C."));
}

TEST_CASE("Cpu: one missing feature is named alone")
{
	CpuFeatures noPopcnt = kHaswell;
	noPopcnt.popcnt = false;
	const std::optional<std::string> message = CheckCpuBaseline(noPopcnt);
	REQUIRE(message.has_value());
	CHECK(message->ends_with("missing: POPCNT."));
}
