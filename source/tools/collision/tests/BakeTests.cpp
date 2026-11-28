#include "Bake.h"

#include <jettison/physics/CollisionFile.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

using namespace Jettison;
using Physics::ShapeDefinition;
using Physics::ShapeKind;

// The models (tests/models), written for the suite: a cube mesh 1 m on a side and a floor 5 m by 1 m.
// pods.gltf draws two cubes 4 m apart along x, the right one twice as tall, and has no collision-
// nodes; pods-authored.gltf draws one long box 5 m by 2 m by 1 m, and makes its collision shape of
// collision-left and collision-right, cubes where the pods are, and collision-mesh-keel, the floor
// at y = -0.5.
namespace
{
const std::uint64_t kHalcyon = Sim::FrameId("Halcyon");


std::filesystem::path Model(std::string_view name)
{
	return std::filesystem::path{JETTISON_BAKE_MODELS} / name;
}


bool Within(const std::vector<glm::vec3> &points, glm::vec3 low, glm::vec3 high)
{
	for (const glm::vec3 &point : points)
		if (glm::any(glm::lessThan(point, low)) || glm::any(glm::greaterThan(point, high)))
			return false;
	return true;
}


// Where a ray down from 10 m over the point on the shape's x axis meets the shape, read from the
// file baked from the model, through the file system, as a static body at the origin.
class Ground
{
  public:
	explicit Ground(std::string_view model)
	{
		std::filesystem::remove_all(m_directory);
		std::filesystem::create_directories(m_directory / "collision");
		const std::vector<std::byte> bytes =
		    Physics::WriteCollisionFile(Bake::CollisionShape(Model(model)), "baked");
		std::ofstream{m_directory / "collision/baked.collision", std::ios::binary}.write(
		    reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Vfs::FileSystem files;
		files.AddMount({"base", m_directory, false});

		Sim::RegisterTypes();
		m_world.RegisterComponent<Sim::Transform>();
		m_world.RegisterComponent<Sim::InFrame>();
		m_physics.DefineShape("baked", Physics::ReadCollisionFile(files, "collision/baked.collision"));
		entt::registry &registry = m_world.Registry();
		const entt::entity entity = registry.create();
		registry.emplace<Sim::Transform>(entity, Sim::Transform{});
		registry.emplace<Sim::InFrame>(entity, kHalcyon);
		registry.emplace<Physics::Body>(entity, Physics::Body{Physics::BodyKind::Static, "baked"});
		m_world.Step();
	}

	~Ground()
	{
		std::filesystem::remove_all(m_directory);
	}

	Ground(const Ground &) = delete;

	Ground &operator=(const Ground &) = delete;

	std::optional<double> Top(double x) const
	{
		const std::optional<Physics::CastHit> hit =
		    m_physics.CastRay(kHalcyon, {x, 10.0, 0.0}, {0.0, -1.0, 0.0}, 20.0);
		return hit ? std::optional{10.0 - hit->distance} : std::nullopt;
	}

  private:
	std::filesystem::path m_directory{
	    std::filesystem::temp_directory_path() / ("jettison-bake-tests-" + std::to_string(getpid()))};
	Sim::SimWorld m_world{1.0 / 60.0, 1};
	Physics::PhysicsWorld &m_physics{Physics::PhysicsWorld::Attach(m_world)};
};
} // namespace


TEST_CASE("Bake: a model with no collision- nodes collides as a convex hull of each mesh node")
{
	const ShapeDefinition shape = Bake::CollisionShape(Model("pods.gltf"));
	CHECK(shape.kind == ShapeKind::Compound);
	REQUIRE(shape.parts.size() == 2);
	for (const ShapeDefinition &part : shape.parts)
	{
		CHECK(part.kind == ShapeKind::ConvexHull);
		CHECK(part.points.size() == 8);
		CHECK(part.vertices.empty());
	}
	CHECK(Within(shape.parts[0].points, {-2.5f, -0.5f, -0.5f}, {-1.5f, 0.5f, 0.5f}));
	CHECK(Within(shape.parts[1].points, {1.5f, -1.0f, -0.5f}, {2.5f, 1.0f, 0.5f}));

	// The same model, the same bytes.
	CHECK(
	    Physics::WriteCollisionFile(Bake::CollisionShape(Model("pods.gltf")), "pods") ==
	    Physics::WriteCollisionFile(shape, "pods"));

	// Between the pods there is nothing, where the box of their size would be met at its top.
	const Ground ground{"pods.gltf"};
	CHECK_FALSE(ground.Top(0.0));
	CHECK(ground.Top(-2.0).value_or(0.0) == doctest::Approx(0.5).epsilon(1e-4));
	CHECK(ground.Top(2.0).value_or(0.0) == doctest::Approx(1.0).epsilon(1e-4));
}


TEST_CASE("Bake: a model's collision- nodes are its shape, its drawn mesh left out")
{
	const ShapeDefinition shape = Bake::CollisionShape(Model("pods-authored.gltf"));
	REQUIRE(shape.parts.size() == 3);
	CHECK(shape.parts[0].kind == ShapeKind::ConvexHull);
	CHECK(Within(shape.parts[0].points, {-2.5f, -0.5f, -0.5f}, {-1.5f, 0.5f, 0.5f}));
	CHECK(shape.parts[1].kind == ShapeKind::ConvexHull);
	CHECK(shape.parts[2].kind == ShapeKind::Mesh);
	CHECK(shape.parts[2].vertices.size() == 4);
	CHECK(shape.parts[2].indices.size() == 6);
	CHECK(Within(shape.parts[2].vertices, {-2.5f, -0.5f, -0.5f}, {2.5f, -0.5f, 0.5f}));
	CHECK(
	    Physics::WriteCollisionFile(Bake::CollisionShape(Model("pods-authored.gltf")), "authored") ==
	    Physics::WriteCollisionFile(shape, "authored"));

	// The drawn box's top is a metre up; what is met is the authored pods' top and, between them,
	// the keel, faced up.
	const Ground ground{"pods-authored.gltf"};
	CHECK(ground.Top(0.0).value_or(0.0) == doctest::Approx(-0.5).epsilon(1e-4));
	CHECK(ground.Top(2.0).value_or(0.0) == doctest::Approx(0.5).epsilon(1e-4));
}


TEST_CASE("Bake: a model that does not read is refused, naming it")
{
	CHECK_THROWS_WITH_AS(
	    Bake::CollisionShape(Model("none.gltf")), doctest::Contains("none.gltf' does not read"),
	    std::runtime_error);
}
