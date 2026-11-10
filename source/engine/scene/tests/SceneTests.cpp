#include <jettison/scene/SystemScene.h>

#include <jettison/celestial/StarSystem.h>
#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Scene: a new system scene is not ready; loading one fails loudly until phase 6")
{
	Jettison::Scene::SystemScene scene;
	CHECK_FALSE(scene.IsReady());
	const Jettison::Celestial::StarSystem system{"Sol", {}};
	CHECK_THROWS_AS(scene.BeginLoad(system), Jettison::Core::NotImplementedError);
}
