#include <jettison/math/Easing.h>
#include <jettison/math/Spline.h>

#include <doctest/doctest.h>

#include <array>
#include <cmath>

using namespace Jettison::Math;

namespace
{
void CheckNear(const glm::vec3 &actual, const glm::vec3 &expected)
{
	CHECK(actual.x == doctest::Approx(expected.x));
	CHECK(actual.y == doctest::Approx(expected.y));
	CHECK(actual.z == doctest::Approx(expected.z));
}
} // namespace

TEST_CASE("CubicBezier: starts and ends on its end points, and the Bernstein weights between")
{
	const glm::vec3 p0{0.0f, 0.0f, 0.0f};
	const glm::vec3 p1{0.0f, 1.0f, 0.0f};
	const glm::vec3 p2{1.0f, 1.0f, 0.0f};
	const glm::vec3 p3{1.0f, 0.0f, 0.0f};
	CheckNear(CubicBezier(p0, p1, p2, p3, 0.0f), p0);
	CheckNear(CubicBezier(p0, p1, p2, p3, 1.0f), p3);
	// 1/8 p0 + 3/8 p1 + 3/8 p2 + 1/8 p3.
	CheckNear(CubicBezier(p0, p1, p2, p3, 0.5f), glm::vec3{0.5f, 0.75f, 0.0f});
	CheckNear(CubicBezier(p0, p1, p2, p3, 2.0f), p3);
	CheckNear(CubicBezier(p0, p1, p2, p3, -1.0f), p0);
}

TEST_CASE("CatmullRom: passes through every point, unevenly spaced ones included")
{
	const std::array<glm::vec3, 5> points{
	    glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{10.0f, 0.0f, 0.0f}, glm::vec3{10.5f, 2.0f, 0.0f},
	    glm::vec3{30.0f, 5.0f, -4.0f}, glm::vec3{31.0f, 5.0f, -4.0f}};
	for (std::size_t index = 0; index < points.size(); ++index)
		CheckNear(CatmullRom(points, static_cast<float>(index) / 4.0f), points[index]);
	CheckNear(CatmullRom(points, -0.5f), points.front());
	CheckNear(CatmullRom(points, 1.5f), points.back());
}

TEST_CASE("CatmullRom: evenly spaced points on a line give a straight, even path")
{
	const std::array<glm::vec3, 3> points{
	    glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{2.0f, 0.0f, 0.0f}};
	CheckNear(CatmullRom(points, 0.25f), glm::vec3{0.5f, 0.0f, 0.0f});
	CheckNear(CatmullRom(points, 0.625f), glm::vec3{1.25f, 0.0f, 0.0f});

	const std::array<glm::vec3, 2> two{glm::vec3{0.0f}, glm::vec3{4.0f, 0.0f, 0.0f}};
	CheckNear(CatmullRom(two, 0.75f), glm::vec3{3.0f, 0.0f, 0.0f});
}

TEST_CASE("CatmullRom: one point, and a repeated point, are not divisions by zero")
{
	const std::array<glm::vec3, 1> one{glm::vec3{1.0f, 2.0f, 3.0f}};
	CheckNear(CatmullRom(one, 0.5f), one.front());

	const std::array<glm::vec3, 4> repeated{
	    glm::vec3{0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f},
	    glm::vec3{2.0f, 0.0f, 0.0f}};
	for (float t = 0.0f; t <= 1.0f; t += 1.0f / 64.0f)
	{
		const glm::vec3 point = CatmullRom(repeated, t);
		CHECK(std::isfinite(point.x));
		CHECK(point.x >= -0.01f);
		CHECK(point.x <= 2.01f);
	}
}

TEST_CASE("Ease: every curve maps 0 to 0 and 1 to 1, and clamps outside")
{
	for (const Easing easing :
	     {Easing::Linear, Easing::InQuad, Easing::OutQuad, Easing::InOutQuad, Easing::InCubic,
	      Easing::OutCubic, Easing::InOutCubic, Easing::OutBack, Easing::OutElastic})
	{
		CAPTURE(static_cast<int>(easing));
		CHECK(Ease(easing, 0.0f) == doctest::Approx(0.0f));
		CHECK(Ease(easing, 1.0f) == doctest::Approx(1.0f));
		CHECK(Ease(easing, -1.0f) == doctest::Approx(0.0f));
		CHECK(Ease(easing, 2.0f) == doctest::Approx(1.0f));
	}
}

TEST_CASE("Ease: the shapes, at hand-computed points")
{
	CHECK(Ease(Easing::Linear, 0.3f) == doctest::Approx(0.3f));
	CHECK(Ease(Easing::InQuad, 0.5f) == doctest::Approx(0.25f));
	CHECK(Ease(Easing::OutQuad, 0.5f) == doctest::Approx(0.75f));
	CHECK(Ease(Easing::InOutQuad, 0.25f) == doctest::Approx(0.125f));
	CHECK(Ease(Easing::InOutQuad, 0.75f) == doctest::Approx(0.875f));
	CHECK(Ease(Easing::InCubic, 0.5f) == doctest::Approx(0.125f));
	CHECK(Ease(Easing::OutCubic, 0.5f) == doctest::Approx(0.875f));
	CHECK(Ease(Easing::InOutCubic, 0.5f) == doctest::Approx(0.5f));
	CHECK(Ease(Easing::InOutCubic, 0.25f) == doctest::Approx(0.0625f));

	// OutBack's constant, 1.70158, is the one that makes it overshoot by a tenth. The peak is at
	// t = 1 - 2 c / (3 (c + 1)).
	CHECK(Ease(Easing::OutBack, 1.0f - 2.0f * 1.70158f / (3.0f * 2.70158f)) == doctest::Approx(1.1f));
	// OutElastic's first crest, where the sine is 1: (10 t - 0.75) 2 pi / 3 = pi / 2.
	CHECK(Ease(Easing::OutElastic, 0.15f) == doctest::Approx(1.0f + std::pow(2.0f, -1.5f)));
}
