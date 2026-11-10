#include <jettison/app/SimPresentation.h>

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

#include <jettison/render/Scene.h>
#include <jettison/render/SceneSystems.h>

using namespace Jettison;

namespace
{
bool Near(const glm::dmat4 &a, const glm::dmat4 &b)
{
	for (int column = 0; column < 4; ++column)
	{
		for (int row = 0; row < 4; ++row)
		{
			// Absolute: the translation is a trillion metres, held to the metre's fraction.
			if (std::abs(a[column][row] - b[column][row]) > 1e-5)
				return false;
		}
	}
	return true;
}
} // namespace

TEST_CASE("App: a proxy shows its simulation entity's interpolated transform, in double precision")
{
	const auto ship = entt::entity{7};
	const glm::dquat from = glm::angleAxis(0.2, glm::normalize(glm::dvec3{1.0, 2.0, 3.0}));
	const glm::dquat to = glm::angleAxis(1.4, glm::normalize(glm::dvec3{-3.0, 1.0, 0.5}));
	// A system position a float cannot hold to the metre: the scene keeps it whole.
	const Sim::Snapshot previous{4, 0.4, {{ship, {{1.0e12, 2.0, -4.0}, from}}}};
	const Sim::Snapshot current{5, 0.5, {{ship, {{1.0e12 + 10.0, 4.0, -8.0}, to}}}};

	entt::registry presentation;
	const entt::entity proxy = presentation.create();
	presentation.emplace<App::SimProxyComponent>(proxy, ship);
	presentation.emplace<Render::TransformComponent>(proxy);
	// A proxy whose entity the snapshots do not hold stays where it was.
	const entt::entity orphan = presentation.create();
	presentation.emplace<App::SimProxyComponent>(orphan, entt::entity{8});
	presentation.emplace<Render::TransformComponent>(orphan, glm::dvec3{1.0, 2.0, 3.0});

	App::SyncSimProxies(presentation, previous, current, 0.25);
	const auto &shown = presentation.get<Render::TransformComponent>(proxy);
	CHECK(shown.translation.x == 1.0e12 + 2.5);
	CHECK(shown.translation.y == doctest::Approx(2.5));
	CHECK(shown.translation.z == doctest::Approx(-5.0));
	CHECK(
	    presentation.get<Render::TransformComponent>(orphan).translation == glm::dvec3{1.0, 2.0, 3.0});

	// The Euler angles written are the ones the scene composes back into the same rotation.
	Render::SceneUpdateSystem{}.Update(presentation, 0.0f);
	const glm::dquat expected{glm::quat{glm::slerp(from, to, 0.25)}};
	const glm::dmat4 world =
	    glm::translate(glm::dmat4{1.0}, glm::dvec3{1.0e12 + 2.5, 2.5, -5.0}) * glm::mat4_cast(expected);
	CHECK(Near(presentation.get<Render::WorldTransformComponent>(proxy).matrix, world));
}
