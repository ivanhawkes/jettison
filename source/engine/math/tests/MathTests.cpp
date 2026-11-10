#include <jettison/math/Bounds.h>
#include <jettison/math/Easing.h>
#include <jettison/math/Intersect.h>
#include <jettison/math/Random.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

using Jettison::Core::NotImplementedError;
using namespace Jettison::Math;

TEST_CASE("Math: the bounding volumes are plain data with sensible defaults")
{
	const Aabb box;
	CHECK(box.min == box.max);
	const Plane plane;
	CHECK(glm::length(plane.normal) == doctest::Approx(1.0f));
}

TEST_CASE("Math: the stubs fail loudly until phase 2")
{
	CHECK_THROWS_AS(Intersects(Aabb{}, Aabb{}), NotImplementedError);
	CHECK_THROWS_AS(Ease(Easing::Linear, 0.5f), NotImplementedError);
	Random random{42};
	CHECK(random.State() == 42);
	CHECK_THROWS_AS(random.NextU32(), NotImplementedError);
}
