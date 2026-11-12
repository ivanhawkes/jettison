#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>
#include <jettison/scene/SceneSystems.h>

#include <doctest/doctest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <jettison/animation/Components.h>
#include <jettison/animation/RigLoader.h>
#include <jettison/jobs/IoWorker.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison::Scene;

namespace
{
// A scene with no models: everything in it loads and saves without assets, which is what lets the
// round trip run without a resource manager.
constexpr const char *kSceneWithoutModels = R"(version: 2
entities:
  - id: root
    transform:
      translation: [0, 0, 0]
      rotation: [0, 0, 0]
      scale: [1, 1, 1]
  - id: camera
    camera:
      position: [2, 2, 2]
      rotation: [-0.6, 0, 0.8]
      fov: 45
      near: 0.1
      active: true
  - id: light
    transform:
      translation: [-2, 2.5, 1]
      rotation: [0, 0, 0]
      scale: [1, 1, 1]
    light:
      color: [1, 0.9, 0.8]
      intensity: 18
      range: 12
      active: true
  - id: floodlight
    transform:
      translation: [0, 3, 0]
      rotation: [-1.5707963, 0, 0]
    light:
      type: spot
      color: [1, 1, 0.9]
      intensity: 400
      range: 30
      innerConeAngle: 0.3
      outerConeAngle: 0.6
      active: true
  - id: sky
    sky:
      seed: 9
      stars: 2500
      starFlux: 2.5e-5
      band:
        normal: [0, 0.8, 0.6]
        width: 0.3
        color: [0.1, 0.09, 0.08]
      nebula:
        color: [0.02, 0.01, 0.03]
        scale: 3
  - id: ring
    parent: root
    mesh:
      shape: ring
      color: [0.5, 0.5, 0.5]
      innerRadius: 0.6
      material: materials/panels.material.yaml
  - id: spinner
    parent: root
    transform:
      translation: [1, 0, 0]
      rotation: [0, 0.3, 0]
      scale: [1, 1, 1]
    transformAnimation:
      axis: [0, 1, 0]
      speed: 0.785398163
)";

// One entity's block, refused for the reason named.
void CheckRefused(Scene &scene, const std::string &entity, const char *reason)
{
	CAPTURE(entity);
	CHECK_THROWS_WITH_AS(
	    scene.LoadText("version: 2\nentities:\n" + entity), doctest::Contains(reason),
	    std::runtime_error);
}
} // namespace

TEST_CASE("Scene: a saved scene loads back to the same file, with the frame loop's phase unsaved")
{
	Scene scene;
	scene.LoadText(kSceneWithoutModels);
	const std::string first = scene.SaveText();
	CHECK(first.find("rotation: [0, 0.3, 0]") != std::string::npos);
	CHECK(first.find("shape: ring") != std::string::npos);
	CHECK(first.find("innerRadius: 0.6") != std::string::npos);
	const auto skies = scene.Registry().view<const SkyComponent>();
	REQUIRE(skies.size() == 1);
	const auto &sky = skies.get<const SkyComponent>(skies.front());
	CHECK(sky.seed == 9u);
	CHECK(sky.stars == 2500.0f);
	CHECK(sky.bandNormal == glm::vec3{0.0f, 0.8f, 0.6f});
	CHECK(sky.nebulaScale == 3.0f);

	// Ten seconds of animation: the rotation's phase moves, and none of it may reach the file.
	const SceneUpdateSystem update;
	for (int frame = 0; frame < 100; ++frame)
		update.Update(scene.Registry(), 0.1f);
	CHECK(scene.SaveText() == first);

	// What the writer produced, the loader accepts, and writes back unchanged.
	scene.LoadText(first);
	CHECK(scene.SaveText() == first);
}

TEST_CASE("Scene: translations and the camera's position are doubles, in the file and in the world")
{
	Scene scene;
	scene.LoadText(R"(version: 2
entities:
  - id: planet
    transform:
      translation: [149600000000.125, 0, -3.5]
  - id: plate
    parent: planet
    transform:
      translation: [0.001, 0, 0]
  - id: camera
    camera:
      position: [149600000000.0625, 1, 2]
      active: true
)");
	SceneUpdateSystem{}.Update(scene.Registry(), 0.0f);
	const auto &registry = scene.Registry();
	for (const auto [entity, id, world] :
	     registry.view<const SceneEntityId, const WorldTransformComponent>().each())
	{
		if (id.value == "plate")
			CHECK(world.matrix[3].x == 149600000000.125 + 0.001);
	}

	Scene again;
	again.LoadText(scene.SaveText());
	const std::string saved = again.SaveText();
	CHECK(saved.find("149600000000.125") != std::string::npos);
	CHECK(saved.find("149600000000.0625") != std::string::npos);
}

TEST_CASE("Scene: a file of another version, or with a field the scene does not save, is refused")
{
	Scene scene;
	CHECK_THROWS_WITH_AS(
	    scene.LoadText("version: 1\nentities: []\n"), doctest::Contains("version 1"),
	    Jettison::Serialise::FormatVersionError);
	// A far plane and an ambient colour were retired before version 2; now they are fields like any
	// the scene does not know, named with the line they are on.
	CHECK_THROWS_WITH_AS(
	    scene.LoadText("version: 2\nentities:\n  - id: camera\n    camera:\n      far: 10\n"),
	    doctest::Contains(
	        "Scene.entities[0].camera.far: 'Scene.Camera' has no saved field 'far' (line 5)"),
	    Jettison::Serialise::FormatError);
	CHECK_THROWS_WITH_AS(
	    scene.LoadText(
	        "version: 2\nentities:\n  - id: m\n    animation:\n      clip: Walk\n      time: 3\n"),
	    doctest::Contains("no saved field 'time'"), Jettison::Serialise::FormatError);
	// A file on disk is named in its error.
	const auto path = std::filesystem::temp_directory_path() /
	                  ("jettison-scene-tests-" + std::to_string(::getpid()) + ".yaml");
	std::ofstream{path} << "version: 2\nentities: 3\n";
	CHECK_THROWS_WITH_AS(scene.Load(path), doctest::Contains(path.string()), std::runtime_error);
	std::filesystem::remove(path);
}

TEST_CASE("Scene: the star system, bodies, sun and orbit camera load and save as authored")
{
	Scene scene;
	scene.LoadText(R"(version: 2
system: systems/test.yaml
entities:
  - id: star
    celestial:
      body: Sun
    sun:
      color: [1, 0.9, 0.8]
      intensity: 1.5
  - id: station
    celestial:
      body: Dock
  - id: camera
    camera:
      fov: 50
      near: 0.5
      active: true
      orbit:
        target: station
        distance: 150
        yaw: 0.5
        pitch: 0.25
        minDistance: 20
)");
	CHECK(scene.StarSystem() == "systems/test.yaml");
	const auto &registry = scene.Registry();
	CHECK(registry.view<const CelestialBodyComponent>().size() == 2);
	CHECK(registry.view<const SunComponent>().size() == 1);
	for (const auto [entity, orbit] : registry.view<const OrbitCameraComponent>().each())
	{
		CHECK(orbit.distance == 150.0);
		CHECK(registry.get<SceneEntityId>(orbit.target).value == "station");
	}

	// A placement and a camera pose are runtime state: neither reaches the file.
	for (const entt::entity entity : registry.view<const CelestialBodyComponent>())
		scene.Registry().emplace<PlacementComponent>(entity, glm::dvec3{1.0e11, 2.0, 3.0});
	UpdateOrbitCameras(scene.Registry(), 0.1);
	const std::string first = scene.SaveText();
	scene.LoadText(first);
	CHECK(scene.SaveText() == first);
	CHECK(first.find("system: systems/test.yaml") != std::string::npos);
	CHECK(first.find("1e+11") == std::string::npos);
	CHECK(first.find("100000000000") == std::string::npos);
}

TEST_CASE("Scene: references, paths, cameras and meshes are checked as the file loads")
{
	Scene scene;
	CheckRefused(scene, "  - id: a\n    celestial:\n      body: Sun\n", "names no system");
	CheckRefused(
	    scene, "  - id: c\n    camera:\n      orbit:\n        target: nobody\n",
	    "orbit target 'nobody'");
	CheckRefused(scene, "  - id: a\n    parent: a\n", "Invalid parent 'a'");
	CheckRefused(scene, "  - id: a\n  - id: a\n", "unique");
	CheckRefused(scene, "  - id: c\n    camera:\n      fov: 180\n", "projection settings");
	CheckRefused(
	    scene, "  - id: a\n    camera: {}\n  - id: b\n    camera: {}\n", "only one active camera");
	CheckRefused(scene, "  - id: m\n    mesh: {}\n", "a model or a shape, not both");
	CheckRefused(scene, "  - id: m\n    mesh:\n      model: ../models/Fox.glb\n", "not a virtual path");
	CheckRefused(
	    scene, "  - id: m\n    mesh:\n      shape: box\n      innerRadius: 0.5\n", "for a ring");
	CheckRefused(
	    scene, "  - id: m\n    mesh:\n      model: models/Fox.glb\n      color: [1, 0, 0]\n",
	    "color is for a shape");
	CheckRefused(
	    scene, "  - id: m\n    mesh:\n      shape: box\n    animation:\n      clip: Walk\n",
	    "needs a model");
	CheckRefused(
	    scene, "  - id: m\n    transformAnimation:\n      axis: [0, 0, 0]\n", "cannot be zero");
	// A model's rig loads through the resource manager, which this scene was not given.
	CheckRefused(scene, "  - id: m\n    mesh:\n      model: models/Fox.glb\n", "resource manager");
	CHECK_THROWS_WITH_AS(
	    scene.LoadText("version: 2\nentities:\n  - id: m\n    mesh:\n      shape: pyramid\n"),
	    doctest::Contains("'pyramid' is not a 'Scene.Shape'"), Jettison::Serialise::FormatError);
}

TEST_CASE(
    "Scene: every shape a file can name reads back as itself, by the name the renderer keys it on")
{
	Scene scene;
	for (const Shape shape :
	     {Shape::Sphere, Shape::Box, Shape::IcoSphere, Shape::CubeSphere, Shape::Ring, Shape::Cylinder})
	{
		CAPTURE(ShapeName(shape));
		scene.LoadText(
		    "version: 2\nentities:\n  - id: m\n    mesh:\n      shape: " +
		    std::string{ShapeName(shape)} + "\n");
		const auto meshes = scene.Registry().view<const MeshComponent>();
		REQUIRE(meshes.size() == 1);
		CHECK(meshes.get<const MeshComponent>(meshes.front()).shape == shape);
	}
}

TEST_CASE("Scene: lights are points or spots with a range, and as many as the scene has")
{
	Scene scene;
	scene.LoadText(kSceneWithoutModels);
	int points = 0;
	int spots = 0;
	for (const auto [entity, light] : scene.Registry().view<const LightComponent>().each())
	{
		if (light.type == LightType::Spot)
		{
			++spots;
			CHECK(light.range == 30.0f);
			CHECK(light.innerConeAngle == 0.3f);
			CHECK(light.outerConeAngle == 0.6f);
		}
		else
		{
			++points;
			CHECK(light.range == 12.0f);
		}
	}
	CHECK(points == 1);
	CHECK(spots == 1);

	const auto refuses = [&](const std::string &light, const char *reason)
	{ CheckRefused(scene, "  - id: l\n    light:\n" + light, reason); };
	refuses("      intensity: 4\n", "needs a range");
	refuses("      range: 0\n", "must be positive");
	refuses("      range: 5\n      outerConeAngle: 0.5\n", "only a spot");
	refuses(
	    "      type: spot\n      range: 5\n      innerConeAngle: 0.5\n      outerConeAngle: 0.5\n",
	    "innerConeAngle < outerConeAngle");
	refuses("      type: spot\n      range: 5\n      outerConeAngle: 2\n", "pi / 2");
	CHECK_THROWS_WITH_AS(
	    scene.LoadText("version: 2\nentities:\n  - id: l\n    light:\n      type: area\n"),
	    doctest::Contains("'area' is not a 'Scene.LightType'"), Jettison::Serialise::FormatError);
}

TEST_CASE("Scene: a scene has one sky, with a band and a nebula it can draw")
{
	Scene scene;
	CheckRefused(scene, "  - id: a\n    sky: {}\n  - id: b\n    sky: {}\n", "only one sky");
	CheckRefused(scene, "  - id: a\n    sky:\n      band:\n        width: 0\n", "must be positive");
	CheckRefused(
	    scene, "  - id: a\n    sky:\n      band:\n        normal: [0, 0, 0]\n", "cannot be zero");
	CheckRefused(scene, "  - id: a\n    sky:\n      stars: -1\n", "cannot be negative");
}

TEST_CASE("Scene: an atmosphere and rings are over a shape, and saved as they were given")
{
	Scene scene;
	scene.LoadText(R"(version: 2
entities:
  - id: planet
    mesh:
      shape: sphere
    atmosphere:
      height: 60000
      mieAnisotropy: 0.7
    rings:
      innerRadius: 8.0e7
      outerRadius: 1.3e8
      opacity: 0.6
      seed: 4
)");
	auto &registry = scene.Registry();
	const auto planet = registry.view<const RingsComponent>().front();
	CHECK(registry.get<AtmosphereComponent>(planet).height == 60000.0);
	CHECK(
	    registry.get<AtmosphereComponent>(planet).rayleighHeight ==
	    AtmosphereComponent{}.rayleighHeight);
	CHECK(registry.get<RingsComponent>(planet).outerRadius == 1.3e8);
	CHECK(registry.get<RingsComponent>(planet).seed == 4u);
	const std::string saved = scene.SaveText();
	Scene again;
	again.LoadText(saved);
	CHECK(again.SaveText() == saved);

	CheckRefused(scene, "  - id: a\n    atmosphere: {}\n", "needs a mesh of one");
	CheckRefused(
	    scene, "  - id: a\n    mesh:\n      shape: sphere\n    atmosphere:\n      mieHeight: 0\n",
	    "must be positive");
	CheckRefused(
	    scene, "  - id: a\n    mesh:\n      shape: sphere\n    atmosphere:\n      mieAnisotropy: 1\n",
	    "between -1 and 1");
	CheckRefused(
	    scene, "  - id: a\n    rings:\n      innerRadius: 1\n      outerRadius: 2\n",
	    "need a mesh of one");
	CheckRefused(
	    scene,
	    "  - id: a\n    mesh:\n      shape: sphere\n    rings:\n      innerRadius: 2\n      "
	    "outerRadius: 1\n",
	    "less than outerRadius");
	CheckRefused(
	    scene,
	    "  - id: a\n    mesh:\n      shape: sphere\n    rings:\n      innerRadius: 1\n      "
	    "outerRadius: 2\n      opacity: 1.5\n",
	    "between 0 and 1");
}

TEST_CASE("Scene: the shipped scenes load, and each model is posed once its rig is in")
{
	namespace Animation = Jettison::Animation;
	Jettison::Vfs::FileSystem assets;
	assets.AddMount({"base", JETTISON_ASSET_SOURCE_DIR});
	Jettison::Jobs::IoWorker worker;
	Jettison::Resource::ResourceManager resources{
	    assets, worker, {.hotReload = false, .pollInterval = {}}};
	Animation::RegisterRigLoader(resources);

	Scene orbit{&resources};
	orbit.Load(std::filesystem::path{JETTISON_ASSET_SOURCE_DIR} / "scene" / "orbit.yaml");
	CHECK(orbit.StarSystem() == "systems/halcyon.yaml");
	CHECK(assets.Exists(orbit.StarSystem()));

	Scene scene{&resources};
	scene.Load(std::filesystem::path{JETTISON_ASSET_SOURCE_DIR} / "scene" / "main.yaml");
	const std::string authored = scene.SaveText();
	const SceneUpdateSystem update{&resources};
	auto &registry = scene.Registry();
	// Until the rigs are in, nothing is posed, so nothing is drawn.
	update.Update(registry, 0.0f);
	CHECK(registry.view<const Animation::SkeletonComponent>().empty());

	worker.WaitIdle();
	resources.Update();
	update.Update(registry, 0.5f);
	std::size_t skinned = 0;
	for (const auto [entity, mesh] : registry.view<const MeshComponent>().each())
	{
		if (mesh.model.empty())
			continue;
		CAPTURE(mesh.model);
		const auto *skeleton = registry.try_get<Animation::SkeletonComponent>(entity);
		REQUIRE(skeleton != nullptr);
		if (!skeleton->jointMatrices.empty())
		{
			++skinned;
			// The fox walks: its clip has moved on from the start.
			CHECK(registry.get<Animation::AnimationPlayerComponent>(entity).time > 0.0f);
		}
	}
	CHECK(skinned == 1);
	// None of it reaches the file.
	CHECK(scene.SaveText() == authored);

	// The scene releases its rigs as it goes, and a stale handle is how a cache would learn so.
	const auto rig = registry.get<RigComponent>(registry.view<const RigComponent>().front()).rig;
	scene.Clear();
	orbit.Clear();
	CHECK(resources.StatusOf(rig) == Jettison::Resource::Status::Invalid);
}
