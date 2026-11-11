#include "../src/Piloting.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include <jettison/scene/Components.h>

using namespace Jettison;
using App::CollisionMesh;
using App::MakeCollisionMesh;
using Input::Device;
using Input::InputCode;

namespace
{
glm::vec3 Lowest(const Physics::ShapeDefinition &shape)
{
	glm::vec3 lowest{1e30f};
	for (const glm::vec3 &vertex : shape.vertices)
		lowest = glm::min(lowest, vertex);
	return lowest;
}
glm::vec3 Highest(const Physics::ShapeDefinition &shape)
{
	glm::vec3 highest{-1e30f};
	for (const glm::vec3 &vertex : shape.vertices)
		highest = glm::max(highest, vertex);
	return highest;
}
} // namespace

TEST_CASE("Piloting: a station's collision mesh is the shapes it is drawn with, where they are on it")
{
	entt::registry scene;
	// The station's root, which its orbit places: its translation and rotation are the orbit's, and
	// only its scale is the hull's.
	const entt::entity station = scene.create();
	scene.emplace<Scene::TransformComponent>(
	    station, glm::dvec3{5.0e6, 0.0, 0.0}, glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{2.0f});
	scene.emplace<Scene::PlacementComponent>(station);
	CHECK_FALSE(MakeCollisionMesh(scene, station));

	// A truss box out to starboard, a quarter turn about Z, on a part that is itself offset.
	const entt::entity arm = scene.create();
	scene.emplace<Scene::ParentComponent>(arm, station);
	scene.emplace<Scene::TransformComponent>(arm, glm::dvec3{10.0, 0.0, 0.0});
	const entt::entity truss = scene.create();
	scene.emplace<Scene::ParentComponent>(truss, arm);
	scene.emplace<Scene::TransformComponent>(
	    truss, glm::dvec3{0.0, 0.0, 0.0}, glm::vec3{0.0f, 0.0f, 1.5707963f},
	    glm::vec3{4.0f, 1.0f, 1.0f});
	Scene::MeshComponent box;
	box.shape = Scene::Shape::Box;
	scene.emplace<Scene::MeshComponent>(truss, box);
	// A beacon, a model and a mesh of another station's are not this hull.
	const entt::entity beacon = scene.create();
	scene.emplace<Scene::ParentComponent>(beacon, station);
	scene.emplace<Scene::TransformComponent>(beacon, glm::dvec3{0.0, 50.0, 0.0});
	scene.emplace<Scene::LayerComponent>(beacon, Scene::Layer::Effects);
	scene.emplace<Scene::MeshComponent>(beacon, box);
	const entt::entity model = scene.create();
	scene.emplace<Scene::ParentComponent>(model, station);
	scene.emplace<Scene::TransformComponent>(model);
	Scene::MeshComponent fox;
	fox.model = "models/Fox.glb";
	scene.emplace<Scene::MeshComponent>(model, fox);
	const entt::entity elsewhere = scene.create();
	scene.emplace<Scene::TransformComponent>(elsewhere, glm::dvec3{-500.0, 0.0, 0.0});
	scene.emplace<Scene::MeshComponent>(elsewhere, box);

	const std::optional<CollisionMesh> mesh = MakeCollisionMesh(scene, station);
	REQUIRE(mesh);
	CHECK(mesh->skippedModels == 1);
	CHECK(mesh->shape.kind == Physics::ShapeKind::Mesh);
	// A box is 24 vertices and 12 triangles; turned, it is 2 long in X and 8 tall in Y, at 10 m out,
	// all twice over for the station's scale.
	CHECK(mesh->shape.vertices.size() == 24);
	CHECK(mesh->shape.indices.size() == 36);
	CHECK(Lowest(mesh->shape).x == doctest::Approx(18.0f));
	CHECK(Highest(mesh->shape).x == doctest::Approx(22.0f));
	CHECK(Lowest(mesh->shape).y == doctest::Approx(-8.0f));
	CHECK(Highest(mesh->shape).y == doctest::Approx(8.0f));
	CHECK(Highest(mesh->shape).z == doctest::Approx(2.0f));
	CHECK(
	    std::ranges::all_of(
	        mesh->shape.indices,
	        [&](std::uint32_t index) { return index < mesh->shape.vertices.size(); }));
}

TEST_CASE("Piloting: a keyboard, a gamepad and a HOTAS fly the ship the same way")
{
	Input::InputSystem input;
	const App::PilotInput pilot{input};
	input.PushContext(App::PilotInput::kContext);

	const auto read = [&]
	{
		input.Poll();
		return pilot.Read(input);
	};
	const auto release = [&]
	{
		for (const InputCode code : input.BoundInputs())
		{
			input.SetButton(code, false);
			input.SetAxis(code, 0.0f);
		}
	};

	// Full ahead, from each: W; the left stick pushed forward, which GLFW reads as -1; a HOTAS's
	// throttle pushed forward, likewise. Ahead is the ship's -Z.
	input.SetButton(Input::Keys::Character('w'), true);
	CHECK(read().linear == glm::vec3{0.0f, 0.0f, -1.0f});
	release();
	input.SetAxis({Device::GamepadAxis, 1}, -1.0f);
	CHECK(read().linear == glm::vec3{0.0f, 0.0f, -1.0f});
	release();
	input.SetAxis(Input::Joysticks::Axis(0, 2), -1.0f);
	CHECK(read().linear == glm::vec3{0.0f, 0.0f, -1.0f});
	release();
	// Half a throttle is half ahead, and a stick at rest, off centre by less than its dead zone, is
	// nothing.
	input.SetAxis(Input::Joysticks::Axis(0, 2), -0.5f);
	input.SetAxis(Input::Joysticks::Axis(0, 1), 0.05f);
	CHECK(read().linear.z == doctest::Approx(-0.5f));
	CHECK(read().angular == glm::vec3{0.0f});
	release();

	// Nose down: Up, the right stick or the HOTAS's stick pushed forward. A turn about +X is nose up.
	input.SetButton(Input::Keys::Up, true);
	CHECK(read().angular == glm::vec3{-1.0f, 0.0f, 0.0f});
	release();
	input.SetAxis({Device::GamepadAxis, 3}, -1.0f);
	CHECK(read().angular == glm::vec3{-1.0f, 0.0f, 0.0f});
	release();
	input.SetAxis(Input::Joysticks::Axis(0, 1), -1.0f);
	CHECK(read().angular == glm::vec3{-1.0f, 0.0f, 0.0f});
	release();

	// Roll to starboard: E, the right bumper, the stick to the right. A turn about +Z, towards the
	// viewer behind the ship, is a roll to port.
	input.SetButton(Input::Keys::Character('e'), true);
	CHECK(read().angular == glm::vec3{0.0f, 0.0f, -1.0f});
	release();
	input.SetButton({Device::GamepadButton, 5}, true);
	CHECK(read().angular == glm::vec3{0.0f, 0.0f, -1.0f});
	release();
	input.SetAxis(Input::Joysticks::Axis(0, 0), 1.0f);
	CHECK(read().angular == glm::vec3{0.0f, 0.0f, -1.0f});
	release();

	// Yaw to starboard and strafe to port and up, together.
	input.SetButton(Input::Keys::Right, true);
	input.SetButton(Input::Keys::Character('a'), true);
	input.SetButton(Input::Keys::Character('r'), true);
	const Physics::FlightControls together = read();
	CHECK(together.angular == glm::vec3{0.0f, -1.0f, 0.0f});
	CHECK(together.linear == glm::vec3{-1.0f, 1.0f, 0.0f});
	release();

	input.SetButton(Input::Joysticks::Button(0, 1), true);
	input.Poll();
	CHECK(pilot.AssistToggled(input));
	input.Poll();
	CHECK_FALSE(pilot.AssistToggled(input));
	input.SetButton({Device::GamepadButton, 6}, true);
	input.Poll();
	CHECK(pilot.ViewToggled(input));
}
