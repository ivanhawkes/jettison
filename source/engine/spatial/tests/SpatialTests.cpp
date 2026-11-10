#include <jettison/spatial/SpatialIndex.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Spatial: an empty index holds nothing; inserting fails loudly until phase 6")
{
	Jettison::Spatial::SpatialIndex index;
	CHECK(index.Count() == 0);
	CHECK_THROWS_AS(
	    index.Insert(entt::entity{0}, {0.0, 0.0, 0.0}), Jettison::Core::NotImplementedError);
}
