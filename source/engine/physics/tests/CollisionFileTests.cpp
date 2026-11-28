#include <jettison/physics/CollisionFile.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>

#include <jettison/serialise/Archive.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace Jettison;
using Physics::Body;
using Physics::BodyKind;
using Physics::ShapeDefinition;
using Physics::ShapeKind;

namespace
{
const std::uint64_t kHalcyon = Sim::FrameId("Halcyon");


std::vector<glm::vec3> Cube(glm::vec3 centre, glm::vec3 half)
{
	std::vector<glm::vec3> corners;
	for (const float x : {-1.0f, 1.0f})
		for (const float y : {-1.0f, 1.0f})
			for (const float z : {-1.0f, 1.0f})
				corners.push_back(centre + half * glm::vec3{x, y, z});
	return corners;
}


// Two pods 1 m on a side, 4 m apart along x, with nothing between them, and a keel under them: a
// floor 5 m long and 1 m wide at y = -0.5, facing up.
ShapeDefinition Pods(bool keel)
{
	ShapeDefinition shape;
	shape.kind = ShapeKind::Compound;
	for (const float x : {-2.0f, 2.0f})
	{
		ShapeDefinition pod;
		pod.kind = ShapeKind::ConvexHull;
		pod.points = Cube({x, 0.0f, 0.0f}, glm::vec3{0.5f});
		shape.parts.push_back(pod);
	}
	if (keel)
	{
		ShapeDefinition floor;
		floor.kind = ShapeKind::Mesh;
		floor.vertices = {
		    {-2.5f, -0.5f, -0.5f}, {2.5f, -0.5f, -0.5f}, {2.5f, -0.5f, 0.5f}, {-2.5f, -0.5f, 0.5f}};
		floor.indices = {0, 2, 1, 0, 3, 2};
		shape.parts.push_back(floor);
	}
	return shape;
}


std::string Refusal(const ShapeDefinition &shape)
{
	try
	{
		(void)Physics::WriteCollisionFile(shape, "collision/odd.collision");
	}
	catch (const Serialise::FormatError &error)
	{
		return error.what();
	}
	return {};
}


struct Scratch
{
	const std::filesystem::path directory = std::filesystem::temp_directory_path() /
	                                        ("jettison-physics-collision-" + std::to_string(getpid()));

	Scratch()
	{
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory / "collision");
	}

	~Scratch()
	{
		std::filesystem::remove_all(directory);
	}

	Scratch(const Scratch &) = delete;

	Scratch &operator=(const Scratch &) = delete;

	void Put(const std::string &path, const std::vector<std::byte> &bytes) const
	{
		std::ofstream{directory / path, std::ios::binary}.write(
		    reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
};
} // namespace


TEST_CASE("Physics: a collision file is written the same twice and reads back as written")
{
	const ShapeDefinition pods = Pods(true);
	const std::vector<std::byte> bytes = Physics::WriteCollisionFile(pods, "collision/pods.collision");
	CHECK(Physics::WriteCollisionFile(pods, "collision/pods.collision") == bytes);

	const ShapeDefinition read = Physics::ReadCollisionFile(bytes, "collision/pods.collision");
	CHECK(read.kind == ShapeKind::Compound);
	REQUIRE(read.parts.size() == 3);
	CHECK(read.parts[0].kind == ShapeKind::ConvexHull);
	CHECK(read.parts[0].points == pods.parts[0].points);
	CHECK(read.parts[2].kind == ShapeKind::Mesh);
	CHECK(read.parts[2].vertices == pods.parts[2].vertices);
	CHECK(read.parts[2].indices == pods.parts[2].indices);
	CHECK(Physics::WriteCollisionFile(read, "collision/pods.collision") == bytes);
}


TEST_CASE("Physics: a collision file is a compound of convex hulls and meshes, and nothing else")
{
	ShapeDefinition box;
	box.kind = ShapeKind::Box;
	CHECK(Refusal(box).find("'collision/odd.collision' is not a compound") != std::string::npos);
	ShapeDefinition empty;
	empty.kind = ShapeKind::Compound;
	CHECK(Refusal(empty).find("is a compound of no parts") != std::string::npos);
	ShapeDefinition round = Pods(false);
	round.parts[1].kind = ShapeKind::Sphere;
	CHECK(
	    Refusal(round).find("has a part, 1, that is not a convex hull or a mesh") != std::string::npos);

	// Bytes that are not a collision file, and a file of another version, name the path.
	const std::vector<std::byte> junk(16, std::byte{7});
	CHECK_THROWS_WITH_AS(
	    Physics::ReadCollisionFile(junk, "collision/junk.collision"),
	    doctest::Contains("'collision/junk.collision' does not read"), Serialise::FormatError);
	const std::vector<std::byte> later = Serialise::BinaryArchive::Write(Pods(false), {2});
	CHECK_THROWS_WITH_AS(
	    Physics::ReadCollisionFile(later, "collision/later.collision"),
	    doctest::Contains("'collision/later.collision' does not read"), Serialise::FormatError);
	Vfs::FileSystem files;
	CHECK_THROWS_WITH_AS(
	    Physics::ReadCollisionFile(files, "collision/none.collision"),
	    doctest::Contains("no collision file at 'collision/none.collision'"), std::runtime_error);
}


TEST_CASE("Physics: a convex hull's points are those that shape it, at most as many as asked")
{
	// A cube's corners, with points inside it and on its faces: the corners alone are kept, in the
	// order given.
	std::vector<glm::vec3> points = Cube(glm::vec3{0.0f}, glm::vec3{1.0f});
	points.insert(points.begin() + 3, glm::vec3{0.1f, 0.2f, 0.3f});
	points.emplace_back(1.0f, 0.0f, 0.0f);
	std::vector<glm::vec3> corners = Cube(glm::vec3{0.0f}, glm::vec3{1.0f});
	CHECK(Physics::ConvexHullPoints(points, 32) == corners);

	// A sphere of 500 points is simplified to the bound, every point kept one of those given.
	std::vector<glm::vec3> sphere;
	for (int index = 0; index < 500; ++index)
	{
		const float y = 1.0f - 2.0f * (static_cast<float>(index) + 0.5f) / 500.0f;
		const float ring = std::sqrt(1.0f - y * y);
		const float angle = static_cast<float>(index) * glm::pi<float>() * (3.0f - std::sqrt(5.0f));
		sphere.emplace_back(ring * std::cos(angle), y, ring * std::sin(angle));
	}
	const std::vector<glm::vec3> simplified = Physics::ConvexHullPoints(sphere, 24);
	CHECK(simplified.size() <= 24);
	CHECK(simplified.size() >= 4);
	for (const glm::vec3 &point : simplified)
		CHECK(std::ranges::find(sphere, point) != sphere.end());
	CHECK(Physics::ConvexHullPoints(sphere, 24) == simplified);

	// Points on a plane make a flat convex hull of its outline; points on a line make none.
	const std::vector<glm::vec3> flat{{0, 0, 0}, {1, 0, 0}, {0.5f, 0, 0.5f}, {1, 0, 1}, {0, 0, 1}};
	CHECK(
	    Physics::ConvexHullPoints(flat, 32) ==
	    std::vector<glm::vec3>{flat[0], flat[1], flat[3], flat[4]});
	Sim::SimWorld world{1.0 / 60.0, 1};
	ShapeDefinition sheet;
	sheet.kind = ShapeKind::ConvexHull;
	sheet.points = Physics::ConvexHullPoints(flat, 32);
	CHECK_NOTHROW(Physics::PhysicsWorld::Attach(world).DefineShape("sheet", sheet));
	const std::vector<glm::vec3> line{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
	CHECK_THROWS_AS(Physics::ConvexHullPoints(line, 32), std::invalid_argument);
	CHECK_THROWS_AS(Physics::ConvexHullPoints(corners, 3), std::invalid_argument);
}


TEST_CASE("Physics: a body collides as the collision file it is read from, where its box would not")
{
	const Scratch scratch;
	scratch.Put("collision/pods.collision", Physics::WriteCollisionFile(Pods(true), "pods"));
	Vfs::FileSystem files;
	files.AddMount({"base", scratch.directory, false});

	Sim::RegisterTypes();
	Sim::SimWorld world{1.0 / 60.0, 1};
	world.RegisterComponent<Sim::Transform>();
	world.RegisterComponent<Sim::InFrame>();
	Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
	physics.DefineShape("pods", Physics::ReadCollisionFile(files, "collision/pods.collision"));
	ShapeDefinition box;
	box.kind = ShapeKind::Box;
	box.halfExtents = {2.5f, 0.5f, 0.5f};
	physics.DefineShape("box", box);

	entt::registry &registry = world.Registry();
	for (const auto &[shape, z] : {std::pair{"pods", 0.0}, std::pair{"box", 20.0}})
	{
		const entt::entity entity = registry.create();
		registry.emplace<Sim::Transform>(entity, Sim::Transform{{0.0, 0.0, z}});
		registry.emplace<Sim::InFrame>(entity, kHalcyon);
		registry.emplace<Body>(entity, Body{BodyKind::Static, shape});
	}

	world.Step();

	const std::optional<Physics::CastHit> pod =
	    physics.CastRay(kHalcyon, {2.0, 10.0, 0.0}, {0.0, -1.0, 0.0}, 20.0);
	REQUIRE(pod);
	CHECK(pod->distance == doctest::Approx(9.5).epsilon(1e-4));
	CHECK(pod->part == 1);
	const std::optional<Physics::CastHit> between =
	    physics.CastRay(kHalcyon, {0.0, 10.0, 0.0}, {0.0, -1.0, 0.0}, 20.0);
	REQUIRE(between);
	CHECK(between->distance == doctest::Approx(10.5).epsilon(1e-4));
	CHECK(between->part == 2);
	const std::optional<Physics::CastHit> boxed =
	    physics.CastRay(kHalcyon, {0.0, 10.0, 20.0}, {0.0, -1.0, 0.0}, 20.0);
	REQUIRE(boxed);
	CHECK(boxed->distance == doctest::Approx(9.5).epsilon(1e-4));

	// A crate dropped onto a pod rests on it, and one dropped between the pods falls past them to
	// the keel, a metre lower; the box of their size would hold both up at its top.
	ShapeDefinition crate;
	crate.kind = ShapeKind::Box;
	crate.halfExtents = glm::vec3{0.2f};
	physics.DefineShape("crate", crate);
	std::vector<entt::entity> crates;
	for (const double x : {2.0, 0.0})
	{
		const entt::entity entity = registry.create();
		registry.emplace<Sim::Transform>(entity, Sim::Transform{{x, 3.0, 0.0}});
		registry.emplace<Sim::InFrame>(entity, kHalcyon);
		Body body{BodyKind::Dynamic, "crate", 10.0f};
		registry.emplace<Body>(entity, body);
		crates.push_back(entity);
	}

	// Gravity is the game's frame model's, and this world has none: the crates are pushed down.
	for (const entt::entity entity : crates)
		registry.emplace_or_replace<Physics::Velocity>(
		    entity, glm::vec3{0.0f, -2.0f, 0.0f}, glm::vec3{0.0f});
	for (int step = 0; step < 180; ++step)
		world.Step();
	CHECK(registry.get<Sim::Transform>(crates[0]).position.y > 0.6);
	CHECK(registry.get<Sim::Transform>(crates[1]).position.y < 0.0);
	CHECK(registry.get<Sim::Transform>(crates[1]).position.y > -0.5);
}
