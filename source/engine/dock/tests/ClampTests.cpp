#include "Fixture.h"

#include <jettison/physics/CollisionFile.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <tuple>
#include <unistd.h>
#include <vector>

using namespace Jettison;

namespace
{
Physics::ShapeDefinition Hull(glm::vec3 centre, glm::vec3 half)
{
	Physics::ShapeDefinition hull;
	hull.kind = Physics::ShapeKind::ConvexHull;
	for (const float x : {-1.0f, 1.0f})
		for (const float y : {-1.0f, 1.0f})
			for (const float z : {-1.0f, 1.0f})
				hull.points.push_back(centre + half * glm::vec3{x, y, z});
	return hull;
}


// A vehicle 8 m long, 6 m in the beam and 1.8 m high, drawn as two pods and a keel, as a model's
// collision file has them: the starboard pod its whole length, 1.5 to 2.5 m out, the port pod as far
// out along its fore half alone, both from its underside to 1 m up, and the keel 1 m wide down its
// middle. Its box's sides are 3 m out.
Physics::ShapeDefinition PodsAndKeel()
{
	Physics::ShapeDefinition shape;
	shape.kind = Physics::ShapeKind::Compound;
	shape.parts = {
	    Hull({2.0f, -0.4f, 0.0f}, {0.5f, 0.5f, 4.0f}), Hull({-2.0f, -0.4f, -2.0f}, {0.5f, 0.5f, 2.0f}),
	    Hull({0.0f, 0.0f, 0.0f}, {0.5f, 0.9f, 4.0f})};
	return shape;
}


// The dock suite's fixture with the berth classes, a vehicle drawn from PodsAndKeel's collision file
// and one of the same size that collides as its box, and a deck with one skiff berth headed along +X.
struct Pods : DockTests::Fixture
{
	std::filesystem::path directory{
	    std::filesystem::temp_directory_path() / ("jettison-dock-clamps-" + std::to_string(getpid()))};
	entt::entity platform{entt::null};


	Pods() : Fixture{false}
	{
		std::filesystem::remove_all(directory);
		std::filesystem::create_directories(directory / "collision");
		const std::vector<std::byte> bytes = Physics::WriteCollisionFile(PodsAndKeel(), "pods");
		std::ofstream{directory / "collision/pods.collision", std::ios::binary}.write(
		    reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		Vfs::FileSystem files;
		files.AddMount({"base", directory, false});

		for (const auto &[name, berthClass] : DockTests::SampleClasses())
			docks.DefineBerthClass(name, berthClass);
		docks.DefineVehicle(
		    "pods", {"Pods", 8.0f, 6.0f, 1.8f, "skiff", {}, "collision/pods.collision"}, &files);
		docks.DefineVehicle("plain", {"Plain", 8.0f, 6.0f, 1.8f, "skiff", {}});
		Dock::PlatformLayout deck;
		deck.decks = {{{0.0, 0.0, 0.0}, {400.0, 20.0, 400.0}}};
		deck.berths = {{"skiff", {0.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}}};
		docks.DefineLayout("deck", deck);
		platform = docks.MakePlatform(world, DockTests::kHarbour, DockTests::kFerry, "deck");
	}


	~Pods()
	{
		std::filesystem::remove_all(directory);
	}


	Pods(const Pods &) = delete;

	Pods &operator=(const Pods &) = delete;
};
} // namespace


TEST_CASE("Docks: a berth's clamps grip a box vehicle on its box's sides, two a side fore and aft")
{
	Pods fixture;
	const Dock::ClampGrips *pGrips = fixture.docks.FindClampGrips("plain");
	REQUIRE(pGrips);
	const double height = 0.34 * double{1.8f};
	CHECK(
	    *pGrips == Dock::ClampGrips{
	                   glm::dvec3{3.0, height, -2.0}, glm::dvec3{3.0, height, 2.0},
	                   glm::dvec3{-3.0, height, -2.0}, glm::dvec3{-3.0, height, 2.0}});
	CHECK_FALSE(fixture.docks.FindClampGrips("missing"));
}


TEST_CASE(
    "Docks: a berth's clamps grip a vehicle drawn from a collision file on its pods' sides, and one "
    "with no side in reach stays open")
{
	Pods fixture;
	const Dock::ClampGrips *pGrips = fixture.docks.FindClampGrips("pods");
	REQUIRE(pGrips);
	const Dock::ClampGrips &grips = *pGrips;

	// Each pod's outer side is half a metre in from the box's, at the height a jaw bites; the port
	// aft clamp finds no pod, and the keel, 2.5 m in, is past its 1.5 m of travel.
	const double height = 0.34 * double{1.8f};
	for (const auto &[index, x, z] : std::vector<std::tuple<std::size_t, double, double>>{
	         {0, 2.5, -2.0}, {1, 2.5, 2.0}, {2, -2.5, -2.0}})
	{
		CAPTURE(index);
		REQUIRE(grips.at(index));
		CHECK(grips.at(index)->x == doctest::Approx(x).epsilon(1e-3));
		CHECK(grips.at(index)->y == doctest::Approx(height));
		CHECK(grips.at(index)->z == doctest::Approx(z));
	}
	CHECK_FALSE(grips[3]);

	// Docked on its berth, turned to it, each grip is where a ray in from beside the berth meets the
	// vehicle's body; under the open clamp, nothing within its travel, and the keel beyond it.
	entt::registry &registry = fixture.world.Registry();
	const entt::entity vehicle = fixture.Vehicle(fixture.platform, "pods", {0.0, 2000.0, 0.0});
	const entt::entity berth = fixture.BerthNumbered(fixture.platform, 1);
	fixture.docks.Park(fixture.world, vehicle, berth);
	fixture.Step();
	REQUIRE(Dock::IsDocked(registry, vehicle, fixture.platform));
	const Sim::Transform &place = registry.get<Sim::Transform>(berth);
	const auto inFrame = [&](const glm::dvec3 &onBerth)
	{ return place.position + place.orientation * onBerth; };
	for (std::size_t index = 0; index < 3; ++index)
	{
		CAPTURE(index);
		const glm::dvec3 out{index < 2 ? 2.0 : -2.0, 0.0, 0.0};
		const glm::dvec3 from = inFrame(*grips.at(index) + out);
		const std::optional<Physics::CastHit> hit =
		    fixture.physics.CastRay(DockTests::kHarbour, from, inFrame(*grips.at(index)) - from, 4.0);
		REQUIRE(hit);
		CHECK(hit->entity == vehicle);
		CHECK(glm::distance(hit->point, inFrame(*grips.at(index))) < 1e-3);
	}
	const glm::dvec3 mount{-3.0, height, 2.0};
	const glm::dvec3 from = inFrame(mount);
	const glm::dvec3 in = inFrame(mount + glm::dvec3{1.0, 0.0, 0.0}) - from;
	CHECK_FALSE(fixture.physics.CastRay(DockTests::kHarbour, from, in, 0.25 * 6.0));
	const std::optional<Physics::CastHit> keel =
	    fixture.physics.CastRay(DockTests::kHarbour, from, in, 4.0);
	REQUIRE(keel);
	CHECK(keel->entity == vehicle);
	CHECK(keel->distance == doctest::Approx(2.5).epsilon(1e-3));
}
