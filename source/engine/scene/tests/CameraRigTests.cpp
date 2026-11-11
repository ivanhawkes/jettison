#include <jettison/scene/CameraRigs.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/SceneSystems.h>

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace Jettison::Scene;

namespace
{
glm::dvec3 Forward(const CameraPose &pose)
{
	return pose.orientation * glm::dvec3{0.0, 0.0, -1.0};
}

// How far from looking straight at the point the camera is, in radians. By atan2, since acos near
// 1 cannot resolve an angle below about 2e-8.
double AimError(const CameraPose &pose, const glm::dvec3 &point)
{
	const glm::dvec3 forward = Forward(pose);
	const glm::dvec3 towards = glm::normalize(point - pose.position);
	return std::atan2(glm::length(glm::cross(forward, towards)), glm::dot(forward, towards));
}
} // namespace

TEST_CASE("Camera rigs: a look rotation points -Z forward and keeps +Y up")
{
	const glm::dquat look = LookRotation({1.0, 0.0, 0.0});
	CHECK(glm::length(look * glm::dvec3{0.0, 0.0, -1.0} - glm::dvec3{1.0, 0.0, 0.0}) < 1e-12);
	CHECK((look * glm::dvec3{0.0, 1.0, 0.0}).y == doctest::Approx(1.0));
	// Straight down still has a roll.
	const glm::dquat down = LookRotation({0.0, -1.0, 0.0});
	CHECK(glm::length(down * glm::dvec3{0.0, 0.0, -1.0} - glm::dvec3{0.0, -1.0, 0.0}) < 1e-12);
}

TEST_CASE("Camera rigs: smoothing is the same whatever the frame rate")
{
	double many = 0.0;
	for (int step = 0; step < 10; ++step)
		many += (1.0 - many) * SmoothingFactor(5.0, 0.01);
	const double one = SmoothingFactor(5.0, 0.1);
	CHECK(many == doctest::Approx(one).epsilon(1e-12));
	CHECK(SmoothingFactor(0.0, 0.1) == 1.0);
}

TEST_CASE("Camera rigs: the orbit camera follows its target exactly, and smooths what the player moves")
{
	OrbitCamera orbit{{.minDistance = 5.0, .maxDistance = 1.0e8}};
	orbit.Place(0.4, 0.2, 100.0);
	// A station 150 million kilometres out, doing 7.7 km/s.
	const glm::dvec3 start{1.496e11, 2.0e6, -6.8e6};
	const glm::dvec3 velocity{7700.0, 0.0, 0.0};
	glm::dvec3 offset{0.0};
	for (int frame = 0; frame < 120; ++frame)
	{
		const glm::dvec3 target = start + velocity * (frame / 60.0);
		orbit.SetTarget(target);
		const CameraPose pose = orbit.Update(1.0 / 60.0);
		if (frame == 0)
			offset = pose.position - target;
		// The same offset from the target, to the bit the doubles keep, every frame.
		CHECK(glm::length((pose.position - target) - offset) < 1e-4);
		// Doubles a hundred and fifty million kilometres out are 30 micrometres apart, which the
		// subtraction in AimError rounds to: 3e-7 radians over the hundred metres.
		CHECK(AimError(pose, target) < 1e-6);
	}
	CHECK(glm::length(offset) == doctest::Approx(100.0));

	orbit.Zoom(0.25);
	for (int frame = 0; frame < 600; ++frame)
		orbit.Update(1.0 / 60.0);
	CHECK(orbit.Distance() == doctest::Approx(25.0).epsilon(1e-6));
	orbit.Zoom(1e-6);
	for (int frame = 0; frame < 600; ++frame)
		orbit.Update(1.0 / 60.0);
	CHECK(orbit.Distance() == doctest::Approx(5.0).epsilon(1e-6));
	// Pitch stops short of the pole.
	orbit.Orbit(0.0, 10.0);
	for (int frame = 0; frame < 600; ++frame)
		orbit.Update(1.0 / 60.0);
	const CameraPose top = orbit.Update(1.0 / 60.0);
	CHECK(Forward(top).y > -1.0);
	CHECK(Forward(top).y < -0.99);
}

TEST_CASE("Camera rigs: the chase camera lags a turn, not a speed")
{
	ChaseCamera chase;
	CameraPose ship{{1.0e9, 0.0, 0.0}, {1.0, 0.0, 0.0, 0.0}};
	chase.SetTarget(ship);
	const CameraPose behind = chase.Update(1.0 / 60.0);
	CHECK(glm::length(behind.position - ship.position - glm::dvec3{0.0, 4.0, 18.0}) < 1e-6);

	// Fast, straight: the offset does not change.
	ship.position += glm::dvec3{0.0, 0.0, -1.0e4};
	chase.SetTarget(ship);
	CHECK(
	    glm::length(chase.Update(1.0 / 60.0).position - ship.position - glm::dvec3{0.0, 4.0, 18.0}) <
	    1e-6);

	// A quarter turn: the camera swings round over several frames, then sits behind again.
	ship.orientation = glm::angleAxis(glm::radians(90.0), glm::dvec3{0.0, 1.0, 0.0});
	chase.SetTarget(ship);
	const glm::dvec3 settled = ship.position + ship.orientation * glm::dvec3{0.0, 4.0, 18.0};
	const double first = glm::length(chase.Update(1.0 / 60.0).position - settled);
	CHECK(first > 1.0);
	for (int frame = 0; frame < 600; ++frame)
		chase.Update(1.0 / 60.0);
	CHECK(glm::length(chase.Update(1.0 / 60.0).position - settled) < 1e-3);
}

TEST_CASE("Camera rigs: the cockpit is rigid with the ship, and its free look is held in a cone")
{
	CockpitCamera cockpit{{.maxYaw = 1.0, .maxPitch = 0.5}};
	const CameraPose ship{
	    {5.0, 6.0, 7.0}, glm::angleAxis(glm::radians(30.0), glm::dvec3{0.0, 1.0, 0.0})};
	cockpit.SetTarget(ship);
	const CameraPose seat = cockpit.Update(1.0 / 60.0);
	CHECK(
	    glm::length(seat.position - (ship.position + ship.orientation * glm::dvec3{0.0, 0.8, -1.5})) <
	    1e-12);
	CHECK(glm::length(Forward(seat) - ship.orientation * glm::dvec3{0.0, 0.0, -1.0}) < 1e-12);

	cockpit.Look(5.0, 5.0);
	for (int frame = 0; frame < 600; ++frame)
		cockpit.Update(1.0 / 60.0);
	const glm::dvec3 local = glm::conjugate(ship.orientation) * Forward(cockpit.Update(1.0 / 60.0));
	CHECK(std::asin(local.y) == doctest::Approx(0.5).epsilon(1e-6));
	cockpit.Centre();
	for (int frame = 0; frame < 600; ++frame)
		cockpit.Update(1.0 / 60.0);
	CHECK(
	    glm::length(
	        Forward(cockpit.Update(1.0 / 60.0)) - ship.orientation * glm::dvec3{0.0, 0.0, -1.0}) <
	    1e-6);
}

TEST_CASE("Camera rigs: the map zooms by ratios from a station to the whole system")
{
	MapCamera map{{.minDistance = 1.0e3, .maxDistance = 1.0e13, .zoomRatio = 2.0}};
	const glm::dvec3 planet{1.496e11, 0.0, 0.0};
	map.SetFocus(planet);
	map.Zoom(-100.0);
	for (int frame = 0; frame < 600; ++frame)
		map.Update(1.0 / 60.0);
	CHECK(map.Distance() == doctest::Approx(1.0e13).epsilon(1e-6));
	const double before = map.Distance();
	map.Zoom(3.0);
	for (int frame = 0; frame < 600; ++frame)
		map.Update(1.0 / 60.0);
	CHECK(map.Distance() == doctest::Approx(before / 8.0).epsilon(1e-6));
	const CameraPose pose = map.Update(1.0 / 60.0);
	CHECK(AimError(pose, planet) < 1e-9);
	// Looking down at the plane.
	CHECK(Forward(pose).y < 0.0);

	// A pan moves across the plane by a share of the distance, and recentring comes back.
	map.Pan(0.5, 0.0);
	for (int frame = 0; frame < 600; ++frame)
		map.Update(1.0 / 60.0);
	const CameraPose panned = map.Update(1.0 / 60.0);
	CHECK(
	    glm::length(panned.position - pose.position) ==
	    doctest::Approx(0.5 * map.Distance()).epsilon(1e-6));
	map.Recentre();
	for (int frame = 0; frame < 600; ++frame)
		map.Update(1.0 / 60.0);
	CHECK(AimError(map.Update(1.0 / 60.0), planet) < 1e-6);
}

TEST_CASE("Camera rigs: a transition eases from one pose to a moving other, and ends on it")
{
	const CameraPose from{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0, 0.0}};
	CameraPose to{{100.0, 0.0, 0.0}, glm::angleAxis(glm::radians(90.0), glm::dvec3{0.0, 1.0, 0.0})};
	CameraTransition transition{from, 1.0, Jettison::Math::Easing::Linear};
	CHECK(transition.Active());
	const CameraPose half = transition.Update(to, 0.5);
	CHECK(half.position.x == doctest::Approx(50.0));
	to.position.x = 200.0;
	const CameraPose end = transition.Update(to, 0.5);
	CHECK(end.position.x == doctest::Approx(200.0));
	CHECK_FALSE(transition.Active());
	CHECK(transition.Update(to, 0.1).position.x == 200.0);
}

TEST_CASE("Camera rigs: a pilot camera follows its ship's world transform through either rig")
{
	entt::registry registry;
	const entt::entity ship = registry.create();
	// A ship 10 km out, nose along -Z turned a quarter turn to port, so it points along -X.
	const glm::dquat heading = glm::angleAxis(glm::radians(90.0), glm::dvec3{0.0, 1.0, 0.0});
	glm::dmat4 matrix = glm::mat4_cast(heading);
	matrix[3] = glm::dvec4{1.0e4, 0.0, 0.0, 1.0};
	registry.emplace<Jettison::Scene::WorldTransformComponent>(ship, matrix);
	const entt::entity camera = registry.create();
	auto &pilot = registry.emplace<Jettison::Scene::PilotCameraComponent>(camera);
	pilot.target = ship;

	Jettison::Scene::UpdatePilotCameras(registry, 1.0 / 60.0);
	const auto &chase = registry.get<Jettison::Scene::CameraPoseComponent>(camera);
	// Behind it, which is +X of it, above it, and looking along its nose.
	CHECK(chase.position.x > 1.0e4 + 10.0);
	CHECK(chase.position.y > 0.0);
	CHECK(glm::dot(chase.orientation * glm::dvec3{0.0, 0.0, -1.0}, glm::dvec3{-1.0, 0.0, 0.0}) > 0.95);

	pilot.view = Jettison::Scene::PilotView::Cockpit;
	Jettison::Scene::UpdatePilotCameras(registry, 1.0 / 60.0);
	const auto &cockpit = registry.get<Jettison::Scene::CameraPoseComponent>(camera);
	const glm::dvec3 eye = glm::dvec3{1.0e4, 0.0, 0.0} + heading * CockpitCameraSettings{}.eye;
	CHECK(glm::length(cockpit.position - eye) < 1e-9);
	CHECK(
	    glm::dot(cockpit.orientation * glm::dvec3{0.0, 0.0, -1.0}, glm::dvec3{-1.0, 0.0, 0.0}) ==
	    doctest::Approx(1.0));

	// Its ship gone, the camera stays where it was.
	registry.destroy(ship);
	const glm::dvec3 left = cockpit.position;
	Jettison::Scene::UpdatePilotCameras(registry, 1.0 / 60.0);
	CHECK(registry.get<Jettison::Scene::CameraPoseComponent>(camera).position == left);
}
