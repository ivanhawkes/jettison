#include <jettison/spatial/SystemIndices.h>

#include <jettison/math/Random.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;
const std::uint64_t kHalcyon = Sim::SystemId("Halcyon");
const std::uint64_t kVesper = Sim::SystemId("Vesper");

double Coordinate(Math::Random &random)
{
	return (random.NextDouble() - 0.5) * 10000.0;
}

struct Fixture
{
	Sim::SimWorld world{kStep, 3};
	Spatial::SystemIndices &indices{Setup(world)};

	static Spatial::SystemIndices &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InSystem>();
		return Spatial::SystemIndices::Attach(world);
	}

	entt::entity Add(const glm::dvec3 &position, std::uint64_t system)
	{
		entt::registry &registry = world.Registry();
		const entt::entity entity = registry.create();
		registry.emplace<Sim::Transform>(entity, Sim::Transform{position});
		registry.emplace<Sim::InSystem>(entity, system);
		return entity;
	}

	// What the index of a system should find within the radius: every entity in that system, by
	// its Transform, in identifier order.
	std::vector<entt::entity> Expected(
	    std::uint64_t system, const glm::dvec3 &centre, double radius) const
	{
		std::vector<entt::entity> found;
		const entt::registry &registry = world.Registry();
		for (const auto [entity, transform, inSystem] :
		     registry.view<const Sim::Transform, const Sim::InSystem>().each())
			if (inSystem.system == system && Spatial::InSphere(transform.position, centre, radius))
				found.push_back(entity);
		std::ranges::sort(found, Spatial::IdentifierLess);
		return found;
	}

	std::vector<entt::entity> Found(std::uint64_t system, const glm::dvec3 &centre, double radius) const
	{
		const Spatial::SpatialIndex *index = indices.Find(system);
		return index ? index->QuerySphere(centre, radius) : std::vector<entt::entity>{};
	}
};
} // namespace

TEST_CASE("System indices: each system's entities are in its own index, as the step found them")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	Math::Random random{11};
	const glm::dvec3 station{1.496e11, 2.0e7, -3.0e6};
	std::vector<entt::entity> ships;
	ships.reserve(400);
	for (int ship = 0; ship < 400; ++ship)
		ships.push_back(fixture.Add(
		    station + glm::dvec3{Coordinate(random), Coordinate(random), Coordinate(random)},
		    ship % 3 == 0 ? kVesper : kHalcyon));
	const entt::entity lane = fixture.Add(station, Sim::kNoSystem);
	CHECK(fixture.indices.Systems().empty());

	fixture.world.Step();
	CHECK(
	    fixture.indices.Systems() ==
	    std::vector<std::uint64_t>{std::min(kHalcyon, kVesper), std::max(kHalcyon, kVesper)});
	CHECK(
	    fixture.indices.Find(kHalcyon)->Count() + fixture.indices.Find(kVesper)->Count() ==
	    ships.size());
	CHECK_FALSE(fixture.indices.Find(kHalcyon)->Contains(lane));
	for (const double radius : {300.0, 2000.0, 1.0e5})
		for (const std::uint64_t system : {kHalcyon, kVesper})
			CHECK(fixture.Found(system, station, radius) == fixture.Expected(system, station, radius));

	// A step later the index follows what moved, changed system, lost a component, went onto a lane
	// or was destroyed; and an index reused at a later version is the new entity, not the old.
	for (std::size_t ship = 0; ship < ships.size(); ++ship)
		registry.get<Sim::Transform>(ships[ship]).position +=
		    glm::dvec3{3.0 * double(ship % 7), 0.0, -1.5};
	registry.replace<Sim::InSystem>(ships[1], kVesper);
	registry.remove<Sim::InSystem>(ships[2]);
	registry.remove<Sim::Transform>(ships[4]);
	registry.replace<Sim::InSystem>(ships[5], Sim::kNoSystem);
	registry.destroy(ships[7]);
	const entt::entity reused = fixture.Add(station, kVesper);
	CHECK(entt::to_entity(reused) == entt::to_entity(ships[7]));
	registry.replace<Sim::InSystem>(lane, kHalcyon);
	fixture.world.Step();

	CHECK(fixture.indices.Find(kVesper)->Contains(ships[1]));
	CHECK_FALSE(fixture.indices.Find(kHalcyon)->Contains(ships[1]));
	for (const entt::entity gone : {ships[2], ships[4], ships[5], ships[7]})
	{
		CHECK_FALSE(fixture.indices.Find(kHalcyon)->Contains(gone));
		CHECK_FALSE(fixture.indices.Find(kVesper)->Contains(gone));
	}
	CHECK(fixture.indices.Find(kVesper)->Contains(reused));
	CHECK(fixture.indices.Find(kHalcyon)->Contains(lane));
	for (const double radius : {300.0, 2000.0, 1.0e5})
		for (const std::uint64_t system : {kHalcyon, kVesper})
			CHECK(fixture.Found(system, station, radius) == fixture.Expected(system, station, radius));

	// A system everything has left has no index.
	for (const auto [entity, inSystem] : registry.view<Sim::InSystem>().each())
		if (inSystem.system == kVesper)
			inSystem.system = kHalcyon;
	fixture.world.Step();
	CHECK(fixture.indices.Find(kVesper) == nullptr);
	CHECK(fixture.indices.Systems() == std::vector<std::uint64_t>{kHalcyon});
	CHECK(fixture.Found(kHalcyon, station, 1.0e5) == fixture.Expected(kHalcyon, station, 1.0e5));
}

TEST_CASE("System indices: a world loaded from a save is indexed from its next step")
{
	Fixture saved;
	for (int ship = 0; ship < 20; ++ship)
		saved.Add({100.0 * ship, 0.0, 0.0}, ship < 10 ? kHalcyon : kVesper);
	saved.world.Step();
	const std::vector<std::byte> save = saved.world.Save();

	Fixture loaded;
	loaded.Add({0.0, 0.0, 0.0}, kHalcyon);
	loaded.world.Step();
	loaded.world.Load(save);
	loaded.world.Step();
	saved.world.Step();
	for (const std::uint64_t system : {kHalcyon, kVesper})
	{
		CHECK(loaded.Found(system, {}, 1.0e4) == saved.Found(system, {}, 1.0e4));
		CHECK(loaded.Found(system, {}, 1.0e4) == loaded.Expected(system, {}, 1.0e4));
	}
}
