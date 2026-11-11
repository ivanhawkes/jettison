#include <jettison/scene/SystemScene.h>

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <jettison/celestial/StarSystem.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>
#include <jettison/scene/SceneSystems.h>

using namespace Jettison;
using Scene::Layer;

namespace
{
Celestial::StarSystem Halcyon()
{
	std::ifstream file{JETTISON_ASSET_SOURCE_DIR "/systems/halcyon.yaml"};
	std::stringstream text;
	text << file.rdbuf();
	return Celestial::ReadStarSystem(text.str());
}

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

entt::entity Named(const entt::registry &registry, std::string_view id)
{
	for (const auto [entity, name] : registry.view<const Scene::SceneEntityId>().each())
		if (name.value == id)
			return entity;
	return entt::null;
}

glm::dvec3 WorldPosition(const entt::registry &registry, entt::entity entity)
{
	return glm::dvec3{registry.get<Scene::WorldTransformComponent>(entity).matrix[3]};
}

// A scene file that presents two of Halcyon's bodies itself, the station with a part of its own.
constexpr const char *kDressed = R"(version: 2
system: systems/halcyon.yaml
entities:
  - id: ostra
    celestial:
      body: Ostra
    transform:
      scale: [6371000, 6371000, 6371000]
    mesh:
      shape: sphere
      color: [0.16, 0.3, 0.55]
  - id: highport
    celestial:
      body: Ostra Highport
  - id: highport-beacon
    parent: highport
    layer: effects
    transform:
      translation: [0, 0, -38.6]
    mesh:
      shape: icosphere
)";
} // namespace

TEST_CASE("System scene: a body's entity id is its name in lower case, hyphenated")
{
	CHECK(Scene::BodyEntityId("Ostra Highport") == "ostra-highport");
	CHECK(Scene::BodyEntityId("  Halcyon  Gate-2 ") == "halcyon-gate-2");
	CHECK(Scene::BodyEntityId("Pell") == "pell");
}

TEST_CASE(
    "System scene: halcyon.yaml alone builds a scene whose bodies and stations follow their orbits")
{
	const Celestial::StarSystem system = Halcyon();
	Scene::Scene scene;
	scene.LoadSystem("systems/halcyon.yaml");
	Scene::SystemScene systemScene{scene, system};
	entt::registry &registry = scene.Registry();

	// An entity for every body, each built, each looking as its kind does.
	CHECK(registry.view<const Scene::CelestialBodyComponent>().size() == system.bodies.size());
	for (const Celestial::Body &body : system.bodies)
	{
		const entt::entity entity = Named(registry, Scene::BodyEntityId(body.name));
		REQUIRE(entity != entt::null);
		CHECK(registry.get<Scene::CelestialBodyComponent>(entity).body == body.name);
		CHECK(registry.all_of<Scene::BuiltComponent>(entity));
	}
	const entt::entity star = Named(registry, "halcyon");
	CHECK(registry.all_of<Scene::SunComponent>(star));
	CHECK_FALSE(registry.all_of<Scene::MeshComponent>(star));
	const entt::entity planet = Named(registry, "ostra");
	CHECK(registry.get<Scene::MeshComponent>(planet).shape == Scene::Shape::Sphere);
	CHECK(registry.get<Scene::TransformComponent>(planet).scale == glm::vec3{6.371e6f});
	const entt::entity station = Named(registry, "ostra-highport");
	CHECK(registry.get<Scene::MeshComponent>(station).shape == Scene::Shape::Box);
	const entt::entity gate = Named(registry, "halcyon-gate");
	CHECK(registry.get<Scene::LayerComponent>(gate).layer == Layer::Effects);
	CHECK(registry.get<Scene::MeshComponent>(gate).shape == Scene::Shape::Ring);

	// A sky from the system's name, and a camera on the station.
	CHECK(registry.view<const Scene::SkyComponent>().size() == 1);
	const entt::entity camera = Named(registry, "halcyon-camera");
	REQUIRE(camera != entt::null);
	CHECK(registry.get<Scene::CameraComponent>(camera).active);
	CHECK(registry.get<Scene::OrbitCameraComponent>(camera).target == station);

	// Each body is where its orbit puts it, at one time and at another; the station moves about the
	// planet as a station in low orbit does, by kilometres a second.
	const Scene::SceneUpdateSystem update;
	std::vector<glm::dvec3> before;
	for (const double time : {0.0, 1.0e8, 1.0e8 + 10.0})
	{
		systemScene.Place(time);
		update.Update(registry, 0.0f);
		for (std::size_t body = 0; body < system.bodies.size(); ++body)
		{
			const entt::entity entity = Named(registry, Scene::BodyEntityId(system.bodies[body].name));
			CHECK(
			    WorldPosition(registry, entity) == Celestial::EvaluateBody(system, body, time).metres);
		}
		before.push_back(WorldPosition(registry, station) - WorldPosition(registry, planet));
	}
	const double speed = glm::length(before[2] - before[1]) / 10.0;
	CHECK(speed > 7000.0);
	CHECK(speed < 8500.0);

	// The camera follows the station there.
	Scene::UpdateOrbitCameras(registry, 1.0);
	const glm::dvec3 eye = registry.get<Scene::CameraPoseComponent>(camera).position;
	CHECK(glm::length(eye - WorldPosition(registry, station)) == doctest::Approx(180.0).epsilon(0.01));
}

TEST_CASE("System scene: the file's bodies are its own, and only the rest are built")
{
	const Celestial::StarSystem system = Halcyon();
	Scene::Scene scene;
	scene.LoadText(kDressed);
	const std::string authored = scene.SaveText();
	Scene::SystemScene systemScene{scene, system};
	entt::registry &registry = scene.Registry();

	CHECK(registry.view<const Scene::CelestialBodyComponent>().size() == system.bodies.size());
	// Halcyon, Pell and the gate, the sky and the camera.
	CHECK(registry.view<const Scene::BuiltComponent>().size() == 5);
	const entt::entity ostra = Named(registry, "ostra");
	CHECK_FALSE(registry.all_of<Scene::BuiltComponent>(ostra));
	CHECK(registry.get<Scene::MeshComponent>(ostra).color == glm::vec3{0.16f, 0.3f, 0.55f});
	CHECK(Named(registry, "ostra-highport") == entt::null);
	// The camera opens on the station the file presents.
	const entt::entity camera = Named(registry, "halcyon-camera");
	CHECK(registry.get<Scene::OrbitCameraComponent>(camera).target == Named(registry, "highport"));

	// Saved, the scene is the file: what was built is the system's, made again on the next load.
	CHECK(scene.SaveText() == authored);
	systemScene.Unload();
	CHECK(registry.view<const Scene::BuiltComponent>().empty());
	CHECK(registry.view<const Scene::CelestialBodyComponent>().size() == 2);
	CHECK(scene.SaveText() == authored);
}

TEST_CASE("System scene: a scene of a system alone saves as that system and no entities")
{
	Scene::Scene scene;
	scene.LoadSystem("systems/halcyon.yaml");
	Scene::SystemScene systemScene{scene, Halcyon()};
	const std::string text = scene.SaveText();
	CHECK(text.find("system: systems/halcyon.yaml") != std::string::npos);
	CHECK(text.find("halcyon-camera") == std::string::npos);
	Scene::Scene loaded;
	loaded.LoadText(text);
	CHECK(loaded.StarSystem() == "systems/halcyon.yaml");
	CHECK(loaded.Registry().view<const Scene::SceneEntityId>().empty());
	CHECK_THROWS_AS(scene.LoadSystem("../systems/halcyon.yaml"), std::runtime_error);
}

TEST_CASE("System scene: a body the system does not have is refused, and a clash of ids")
{
	const Celestial::StarSystem system = Halcyon();
	Scene::Scene scene;
	scene.LoadText(R"(version: 2
system: systems/halcyon.yaml
entities:
  - id: nowhere
    celestial:
      body: Nowhere
)");
	CHECK_THROWS_WITH_AS(
	    Scene::SystemScene(scene, system), doctest::Contains("'Nowhere'"), std::runtime_error);

	// An entity of the file has the id a built body would: refused, and nothing is built.
	scene.LoadText(R"(version: 2
system: systems/halcyon.yaml
entities:
  - id: pell
    transform:
      translation: [1, 2, 3]
)");
	CHECK_THROWS_WITH_AS(
	    Scene::SystemScene(scene, system), doctest::Contains("'pell'"), std::runtime_error);
	CHECK(scene.Registry().view<const Scene::SceneEntityId>().size() == 1);
	CHECK(scene.Registry().view<const Scene::BuiltComponent>().empty());
}

TEST_CASE("System scene: a body's placement is its orbit, and the authored transform is left alone")
{
	const Celestial::StarSystem system = Halcyon();
	entt::registry scene;
	const entt::entity station = scene.create();
	scene.emplace<Scene::TransformComponent>(station);
	scene.emplace<Scene::CelestialBodyComponent>(station, "Ostra Highport");
	Scene::PlaceCelestialBodies(scene, system, 1234.5);
	const auto &placement = scene.get<Scene::PlacementComponent>(station);
	const auto index = static_cast<std::size_t>(system.Find("Ostra Highport"));
	CHECK(placement.position == Celestial::EvaluateBody(system, index, 1234.5).metres);
	CHECK(scene.get<Scene::TransformComponent>(station).translation == glm::dvec3{0.0});
	Scene::SceneUpdateSystem{}.Update(scene, 0.0f);
	CHECK(WorldPosition(scene, station) == placement.position);
}

TEST_CASE("System scene: the sky is the system's own, the same every time it is built")
{
	Scene::Scene first;
	first.LoadSystem("systems/halcyon.yaml");
	const Scene::SystemScene firstScene{first, Halcyon()};
	Scene::Scene second;
	second.LoadSystem("systems/halcyon.yaml");
	const Scene::SystemScene secondScene{second, Halcyon()};
	const auto sky = [](const Scene::Scene &scene)
	{
		const auto view = scene.Registry().view<const Scene::SkyComponent>();
		return scene.Registry().get<Scene::SkyComponent>(view.front());
	};
	CHECK(sky(first) == sky(second));
	CHECK(glm::length(sky(first).bandNormal) == doctest::Approx(1.0f));

	Celestial::StarSystem other = Halcyon();
	other.name = "Tessaly";
	Scene::Scene third;
	third.LoadSystem("systems/tessaly.yaml");
	const Scene::SystemScene thirdScene{third, std::move(other)};
	CHECK(sky(third).seed != sky(first).seed);
	CHECK(sky(third).bandNormal != sky(first).bandNormal);
}

TEST_CASE("System scene: a hidden layer is not drawn, and children are in their parent's layer")
{
	Scene::Scene scene;
	scene.LoadText(kDressed);
	Scene::SystemScene systemScene{scene, Halcyon()};
	entt::registry &registry = scene.Registry();
	const Scene::SceneUpdateSystem update;
	systemScene.Place(0.0);
	update.Update(registry, 0.0f);
	CHECK(registry.view<const Scene::HiddenComponent>().empty());

	scene.SetLayerVisible(Layer::Effects, false);
	CHECK_FALSE(scene.IsLayerVisible(Layer::Effects));
	CHECK(scene.IsLayerVisible(Layer::Environment));
	update.Update(registry, 0.0f);
	CHECK(registry.all_of<Scene::HiddenComponent>(Named(registry, "highport-beacon")));
	CHECK(registry.all_of<Scene::HiddenComponent>(Named(registry, "halcyon-gate")));
	CHECK_FALSE(registry.all_of<Scene::HiddenComponent>(Named(registry, "highport")));
	CHECK(registry.view<const Scene::HiddenComponent>().size() == 2);

	// A station's part with no layer of its own is where the station is.
	scene.SetLayerVisible(Layer::Effects, true);
	scene.SetLayerVisible(Layer::Environment, false);
	update.Update(registry, 0.0f);
	const entt::entity part = registry.create();
	registry.emplace<Scene::TransformComponent>(part);
	registry.emplace<Scene::ParentComponent>(part, Named(registry, "highport"));
	update.Update(registry, 0.0f);
	CHECK(registry.all_of<Scene::HiddenComponent>(part));
	CHECK(registry.all_of<Scene::HiddenComponent>(Named(registry, "ostra")));
	CHECK_FALSE(registry.all_of<Scene::HiddenComponent>(Named(registry, "highport-beacon")));

	// The visibility outlasts a load, as an editor's would; showing every layer again clears it.
	scene.LoadText(kDressed);
	CHECK_FALSE(scene.IsLayerVisible(Layer::Environment));
	scene.SetLayerVisible(Layer::Environment, true);
	update.Update(registry, 0.0f);
	CHECK(registry.view<const Scene::HiddenComponent>().empty());
}

TEST_CASE("System scene: an entity's layer is in its file, and a layer the file names is checked")
{
	Scene::Scene scene;
	scene.LoadText(kDressed);
	const std::string text = scene.SaveText();
	CHECK(text.find("layer: effects") != std::string::npos);
	scene.LoadText(text);
	CHECK(scene.SaveText() == text);
	CHECK_THROWS_AS(
	    scene.LoadText(R"(version: 2
entities:
  - id: thing
    layer: background
)"),
	    std::exception);
}

TEST_CASE("System scene: the simulation's entities are the traffic layer, made and unmade with them")
{
	Scene::Scene scene;
	scene.LoadText(kDressed);
	Scene::SystemScene systemScene{scene, Halcyon()};
	entt::registry &registry = scene.Registry();
	const std::size_t built = registry.view<const Scene::BuiltComponent>().size();

	const auto ship = entt::entity{3};
	const auto buoy = entt::entity{4};
	int asked = 0;
	entt::entity madeHull{entt::null};
	entt::entity madeEngine{entt::null};
	systemScene.SetProxyFactory(
	    [&](entt::registry &presentation, entt::entity simEntity) -> entt::entity
	    {
		    ++asked;
		    if (simEntity == buoy)
			    return entt::null;
		    const entt::entity hull = presentation.create();
		    Scene::MeshComponent mesh;
		    mesh.shape = Scene::Shape::Box;
		    presentation.emplace<Scene::MeshComponent>(hull, mesh);
		    const entt::entity engine = presentation.create();
		    presentation.emplace<Scene::TransformComponent>(engine, glm::dvec3{0.0, 0.0, 5.0});
		    presentation.emplace<Scene::ParentComponent>(engine, hull);
		    madeHull = hull;
		    madeEngine = engine;
		    return hull;
	    });

	const Sim::Snapshot first{
	    1, 0.1, {{ship, {{1.0e11, 0.0, 0.0}, {}}}, {buoy, {{2.0e11, 0.0, 0.0}, {}}}}};
	const Sim::Snapshot second{
	    2, 0.2, {{ship, {{1.0e11 + 8.0, 0.0, 0.0}, {}}}, {buoy, {{2.0e11, 0.0, 0.0}, {}}}}};
	systemScene.SyncTraffic(first, first, 0.0);
	systemScene.SyncTraffic(first, second, 0.5);
	CHECK(asked == 2);
	const auto proxies = registry.view<const Scene::SimProxyComponent>();
	REQUIRE(proxies.size() == 1);
	const entt::entity hull = proxies.front();
	CHECK(registry.get<Scene::SimProxyComponent>(hull).simEntity == ship);
	CHECK(registry.get<Scene::LayerComponent>(hull).layer == Layer::Traffic);
	CHECK(registry.all_of<Scene::BuiltComponent>(hull));
	CHECK(registry.get<Scene::TransformComponent>(hull).translation.x == 1.0e11 + 4.0);

	// The traffic layer hidden hides the ship and its engine.
	scene.SetLayerVisible(Layer::Traffic, false);
	Scene::SceneUpdateSystem{}.Update(registry, 0.0f);
	CHECK(registry.view<const Scene::HiddenComponent>().size() == 2);
	scene.SetLayerVisible(Layer::Traffic, true);

	// The ship leaves the snapshots: its proxy goes, and its engine with it. The buoy, declined
	// once, is not asked for again until it has left and come back.
	const Sim::Snapshot third{3, 0.3, {{buoy, {{2.0e11, 0.0, 0.0}, {}}}}};
	systemScene.SyncTraffic(second, third, 0.0);
	CHECK(asked == 2);
	CHECK(registry.view<const Scene::SimProxyComponent>().empty());
	CHECK_FALSE(registry.valid(madeHull));
	CHECK_FALSE(registry.valid(madeEngine));
	CHECK(registry.view<const Scene::BuiltComponent>().size() == built);
	Scene::SceneUpdateSystem{}.Update(registry, 0.0f);

	// Unloaded, the scene is the file again, proxies and all gone.
	systemScene.SyncTraffic(third, first, 0.0);
	CHECK(asked == 3);
	systemScene.Unload();
	CHECK(registry.view<const Scene::BuiltComponent>().empty());
	CHECK(registry.view<const Scene::SimProxyComponent>().empty());
	CHECK(registry.view<const Scene::SceneEntityId>().size() == 3);
}

TEST_CASE(
    "System scene: a proxy shows its simulation entity's interpolated transform, in double precision")
{
	const auto ship = entt::entity{7};
	const glm::dquat from = glm::angleAxis(0.2, glm::normalize(glm::dvec3{1.0, 2.0, 3.0}));
	const glm::dquat to = glm::angleAxis(1.4, glm::normalize(glm::dvec3{-3.0, 1.0, 0.5}));
	// A system position a float cannot hold to the metre: the scene keeps it whole.
	const Sim::Snapshot previous{4, 0.4, {{ship, {{1.0e12, 2.0, -4.0}, from}}}};
	const Sim::Snapshot current{5, 0.5, {{ship, {{1.0e12 + 10.0, 4.0, -8.0}, to}}}};

	entt::registry presentation;
	const entt::entity proxy = presentation.create();
	presentation.emplace<Scene::SimProxyComponent>(proxy, ship);
	presentation.emplace<Scene::TransformComponent>(proxy);
	// A proxy whose entity the snapshots do not hold stays where it was.
	const entt::entity orphan = presentation.create();
	presentation.emplace<Scene::SimProxyComponent>(orphan, entt::entity{8});
	presentation.emplace<Scene::TransformComponent>(orphan, glm::dvec3{1.0, 2.0, 3.0});

	Scene::SyncSimProxies(presentation, previous, current, 0.25);
	const auto &shown = presentation.get<Scene::TransformComponent>(proxy);
	CHECK(shown.translation.x == 1.0e12 + 2.5);
	CHECK(shown.translation.y == doctest::Approx(2.5));
	CHECK(shown.translation.z == doctest::Approx(-5.0));
	CHECK(presentation.get<Scene::TransformComponent>(orphan).translation == glm::dvec3{1.0, 2.0, 3.0});

	// The Euler angles written are the ones the scene composes back into the same rotation.
	Scene::SceneUpdateSystem{}.Update(presentation, 0.0f);
	const glm::dquat expected{glm::quat{glm::slerp(from, to, 0.25)}};
	const glm::dmat4 world =
	    glm::translate(glm::dmat4{1.0}, glm::dvec3{1.0e12 + 2.5, 2.5, -5.0}) * glm::mat4_cast(expected);
	CHECK(Near(presentation.get<Scene::WorldTransformComponent>(proxy).matrix, world));
}
