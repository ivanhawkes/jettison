#include <jettison/math/Bounds.h>
#include <jettison/math/Intersect.h>

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace Jettison::Math;

TEST_CASE("Aabb: overlapping and touching boxes intersect, separated ones do not")
{
	const Aabb unit{glm::vec3{0.0f}, glm::vec3{1.0f}};
	CHECK(Intersects(unit, Aabb{glm::vec3{0.5f}, glm::vec3{2.0f}}));
	CHECK(Intersects(unit, Aabb{glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{2.0f, 1.0f, 1.0f}}));
	CHECK_FALSE(Intersects(unit, Aabb{glm::vec3{1.5f, 0.0f, 0.0f}, glm::vec3{2.0f, 1.0f, 1.0f}}));
	CHECK_FALSE(Intersects(unit, Aabb{glm::vec3{0.0f, 0.0f, -2.0f}, glm::vec3{1.0f, 1.0f, -1.5f}}));
}

// Four million kilometres from the star, two boxes a centimetre apart. In float both edges round
// to the same value and the boxes touch; the simulation's frame needs the double version.
TEST_CASE("DAabb: keeps a centimetre apart at four million kilometres")
{
	const double x = 4.0e9;
	const DAabb a{glm::dvec3{x - 1.0, 0.0, 0.0}, glm::dvec3{x, 1.0, 1.0}};
	const DAabb b{glm::dvec3{x + 0.01, 0.0, 0.0}, glm::dvec3{x + 1.0, 1.0, 1.0}};
	CHECK_FALSE(Intersects(a, b));

	const Aabb aFloat{glm::vec3{a.min}, glm::vec3{a.max}};
	const Aabb bFloat{glm::vec3{b.min}, glm::vec3{b.max}};
	CHECK(Intersects(aFloat, bFloat));
}

TEST_CASE("Sphere: intersect when the centres are within the sum of the radii")
{
	const Sphere a{glm::vec3{0.0f}, 1.0f};
	CHECK(Intersects(a, Sphere{glm::vec3{1.5f, 0.0f, 0.0f}, 1.0f}));
	CHECK(Intersects(a, Sphere{glm::vec3{0.0f, 3.0f, 0.0f}, 2.0f}));
	CHECK_FALSE(Intersects(a, Sphere{glm::vec3{0.0f, 0.0f, 2.5f}, 1.0f}));
	CHECK(Intersects(DSphere{glm::dvec3{0.0}, 1.0}, DSphere{glm::dvec3{1.0, 1.0, 0.0}, 0.5}));
}

TEST_CASE("Ray against Aabb: distance to entry, 0 from inside, misses")
{
	const Aabb box{glm::vec3{-1.0f}, glm::vec3{1.0f}};
	const Ray down{glm::vec3{0.0f, 0.0f, 10.0f}, glm::vec3{0.0f, 0.0f, -1.0f}};
	REQUIRE(Intersect(down, box).has_value());
	CHECK(*Intersect(down, box) == doctest::Approx(9.0f));

	// Measured in multiples of the direction.
	const Ray fast{down.origin, glm::vec3{0.0f, 0.0f, -2.0f}};
	CHECK(*Intersect(fast, box) == doctest::Approx(4.5f));

	CHECK(*Intersect(Ray{glm::vec3{0.5f}, glm::vec3{1.0f, 0.0f, 0.0f}}, box) == 0.0f);
	CHECK_FALSE(Intersect(Ray{down.origin, glm::vec3{0.0f, 0.0f, 1.0f}}, box).has_value());
	CHECK_FALSE(Intersect(Ray{glm::vec3{3.0f, 0.0f, 10.0f}, down.direction}, box).has_value());
}

// The x and y components are zero: a division would give infinities, and NaN on the face.
TEST_CASE("Ray against Aabb: axis-parallel rays, including one along a face")
{
	const Aabb box{glm::vec3{-1.0f}, glm::vec3{1.0f}};
	const glm::vec3 down{0.0f, 0.0f, -1.0f};
	CHECK(*Intersect(Ray{glm::vec3{1.0f, 0.0f, 5.0f}, down}, box) == doctest::Approx(4.0f));
	CHECK(*Intersect(Ray{glm::vec3{1.0f, -1.0f, 5.0f}, down}, box) == doctest::Approx(4.0f));
	CHECK_FALSE(Intersect(Ray{glm::vec3{1.001f, 0.0f, 5.0f}, down}, box).has_value());
}

TEST_CASE("Ray against Obb: tested in the box's own frame")
{
	// A cube of half extent 1 turned 45 degrees about Y: a corner points along +Z.
	const Obb box{
	    glm::vec3{0.0f}, glm::vec3{1.0f},
	    glm::mat3{glm::rotate(glm::mat4{1.0f}, glm::radians(45.0f), glm::vec3{0, 1, 0})}};
	const glm::vec3 down{0.0f, 0.0f, -1.0f};
	CHECK(
	    *Intersect(Ray{glm::vec3{0.0f, 0.0f, 10.0f}, down}, box) ==
	    doctest::Approx(10.0f - std::sqrt(2.0f)));
	// Outside the unturned cube, inside the turned one.
	CHECK(Intersect(Ray{glm::vec3{1.2f, 0.0f, 10.0f}, down}, box).has_value());
	CHECK_FALSE(Intersect(Ray{glm::vec3{1.5f, 0.0f, 10.0f}, down}, box).has_value());
	CHECK_FALSE(
	    Intersect(Ray{glm::vec3{1.2f, 0.0f, 10.0f}, down}, Obb{glm::vec3{0.0f}, glm::vec3{1.0f}})
	        .has_value());
}

TEST_CASE("Ray against Sphere: first hit, 0 from inside, misses")
{
	const Sphere sphere{glm::vec3{0.0f}, 1.0f};
	const Ray down{glm::vec3{0.0f, 0.0f, 10.0f}, glm::vec3{0.0f, 0.0f, -1.0f}};
	CHECK(*Intersect(down, sphere) == doctest::Approx(9.0f));
	CHECK(*Intersect(Ray{down.origin, glm::vec3{0.0f, 0.0f, -2.0f}}, sphere) == doctest::Approx(4.5f));
	CHECK(*Intersect(Ray{glm::vec3{0.0f, 0.5f, 0.0f}, down.direction}, sphere) == 0.0f);
	CHECK_FALSE(Intersect(Ray{down.origin, glm::vec3{0.0f, 0.0f, 1.0f}}, sphere).has_value());
	CHECK_FALSE(Intersect(Ray{glm::vec3{1.1f, 0.0f, 10.0f}, down.direction}, sphere).has_value());
	CHECK(
	    *Intersect(
	        DRay{glm::dvec3{0.0, 0.0, 10.0}, glm::dvec3{0.0, 0.0, -1.0}},
	        DSphere{glm::dvec3{0.0}, 1.0}) == doctest::Approx(9.0));
}

TEST_CASE("Ray against Plane: in front, behind, and parallel")
{
	const Plane ground{glm::vec3{0.0f, 1.0f, 0.0f}, 0.0f};
	const glm::vec3 start{0.0f, 5.0f, 0.0f};
	CHECK(*Intersect(Ray{start, glm::vec3{0.0f, -1.0f, 0.0f}}, ground) == doctest::Approx(5.0f));
	CHECK(
	    *Intersect(Ray{start, glm::normalize(glm::vec3{1.0f, -1.0f, 0.0f})}, ground) ==
	    doctest::Approx(5.0f * std::sqrt(2.0f)));
	CHECK_FALSE(Intersect(Ray{start, glm::vec3{0.0f, 1.0f, 0.0f}}, ground).has_value());
	CHECK_FALSE(Intersect(Ray{start, glm::vec3{1.0f, 0.0f, 0.0f}}, ground).has_value());
	// dot(normal, p) + distance == 0: this is the plane y = 2.
	CHECK(
	    *Intersect(
	        Ray{start, glm::vec3{0.0f, -1.0f, 0.0f}}, Plane{glm::vec3{0.0f, 1.0f, 0.0f}, -2.0f}) ==
	    doctest::Approx(3.0f));
}

TEST_CASE("Transformed: a box turned and moved still bounds itself")
{
	const Aabb box{glm::vec3{-1.0f}, glm::vec3{1.0f}};
	const glm::mat4 matrix = glm::rotate(
	    glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f}), glm::radians(45.0f),
	    glm::vec3{0.0f, 1.0f, 0.0f});
	const Aabb moved = Transformed(box, matrix);
	const float reach = std::sqrt(2.0f);
	CHECK(moved.min.x == doctest::Approx(10.0f - reach));
	CHECK(moved.max.x == doctest::Approx(10.0f + reach));
	CHECK(moved.min.y == doctest::Approx(-1.0f));
	CHECK(moved.max.y == doctest::Approx(1.0f));
	CHECK(moved.max.z == doctest::Approx(reach));

	const Aabb scaled = Transformed(box, glm::scale(glm::mat4{1.0f}, glm::vec3{2.0f, 3.0f, 4.0f}));
	CHECK(scaled.max.x == doctest::Approx(2.0f));
	CHECK(scaled.min.z == doctest::Approx(-4.0f));
}

namespace
{
// Looking down -Z from the origin, a 90-degree field of view: at distance d the frustum reaches
// d to either side.
const glm::mat4 kView = glm::lookAt(glm::vec3{0.0f}, glm::vec3{0.0f, 0.0f, -1.0f}, glm::vec3{0, 1, 0});

bool Sees(const Frustum &frustum, const glm::vec3 &point)
{
	return Intersects(frustum, Sphere{point, 0.0f});
}

// Zero-to-one depth, reversed, with no far plane: what phase 4 moves the renderer to. Depth is
// near / distance, 1 at the near plane and 0 at infinity.
glm::mat4 ReversedInfinitePerspective(float verticalFov, float aspect, float near)
{
	const float focal = 1.0f / std::tan(verticalFov / 2.0f);
	glm::mat4 projection{0.0f};
	projection[0][0] = focal / aspect;
	projection[1][1] = focal;
	projection[2][3] = -1.0f;
	projection[3][2] = near;
	return projection;
}
} // namespace

TEST_CASE("FrustumFromMatrix: conventional depth, the renderer's Vulkan Y flip included")
{
	glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
	projection[1][1] *= -1.0f;
	const Frustum frustum = FrustumFromMatrix(projection * kView);

	for (const Plane &plane : frustum.planes)
		CHECK(glm::length(plane.normal) == doctest::Approx(1.0f));

	CHECK(Sees(frustum, glm::vec3{0.0f, 0.0f, -10.0f}));
	CHECK(Sees(frustum, glm::vec3{9.0f, -9.0f, -10.0f}));
	CHECK_FALSE(Sees(frustum, glm::vec3{11.0f, 0.0f, -10.0f}));
	CHECK_FALSE(Sees(frustum, glm::vec3{0.0f, 11.0f, -10.0f}));
	CHECK_FALSE(Sees(frustum, glm::vec3{0.0f, 0.0f, 10.0f}));
	CHECK_FALSE(Sees(frustum, glm::vec3{0.0f, 0.0f, -0.05f}));
	CHECK_FALSE(Sees(frustum, glm::vec3{0.0f, 0.0f, -101.0f}));

	// A sphere reaching across the right plane from outside it.
	CHECK(Intersects(frustum, Sphere{glm::vec3{11.0f, 0.0f, -10.0f}, 1.0f}));
}

TEST_CASE("FrustumFromMatrix: reversed-Z with an infinite far plane")
{
	const Frustum frustum =
	    FrustumFromMatrix(ReversedInfinitePerspective(glm::radians(90.0f), 1.0f, 0.1f) * kView);

	// Depth 0 is the far plane here, and it is at infinity.
	CHECK(frustum.planes[4].normal == glm::vec3{0.0f});
	CHECK(frustum.planes[4].distance == 1.0f);
	CHECK(glm::length(frustum.planes[5].normal) == doctest::Approx(1.0f));

	CHECK(Sees(frustum, glm::vec3{0.0f, 0.0f, -1.0e9f}));
	CHECK(Sees(frustum, glm::vec3{0.0f, 0.0f, -0.2f}));
	CHECK_FALSE(Sees(frustum, glm::vec3{0.0f, 0.0f, -0.05f}));
	CHECK_FALSE(Sees(frustum, glm::vec3{0.0f, 0.0f, 1.0f}));
	CHECK_FALSE(Sees(frustum, glm::vec3{2.0e9f, 0.0f, -1.0e9f}));
}

TEST_CASE("Frustum against Aabb: inside, outside, and straddling a plane")
{
	const Frustum frustum =
	    FrustumFromMatrix(glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f) * kView);
	CHECK(Intersects(frustum, Aabb{glm::vec3{-1.0f, -1.0f, -11.0f}, glm::vec3{1.0f, 1.0f, -9.0f}}));
	CHECK(Intersects(frustum, Aabb{glm::vec3{9.0f, -1.0f, -11.0f}, glm::vec3{12.0f, 1.0f, -9.0f}}));
	CHECK_FALSE(
	    Intersects(frustum, Aabb{glm::vec3{12.0f, -1.0f, -11.0f}, glm::vec3{14.0f, 1.0f, -9.0f}}));
	CHECK_FALSE(Intersects(frustum, Aabb{glm::vec3{-1.0f, -1.0f, 1.0f}, glm::vec3{1.0f, 1.0f, 3.0f}}));
}
