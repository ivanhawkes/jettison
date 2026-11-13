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

TEST_CASE("Scene: a light's intensity can move with time, and is saved with its wave")
{
	const std::string text = "version: 2\nentities:\n"
	                         "  - id: beacon\n    light:\n      intensity: 4\n      range: 6\n"
	                         "      animation:\n        wave: Square\n        period: 1.2\n"
	                         "        phase: 0.25\n        from: 0.1\n        to: 1\n";
	Scene scene;
	scene.LoadText(text);
	const auto lights = scene.Registry().view<const LightComponent>();
	REQUIRE(lights.size() == 1);
	const LightComponent &light = lights.get<const LightComponent>(lights.front());
	REQUIRE(light.animation);
	CHECK(light.animation->wave == Wave::Square);
	CHECK(light.animation->period == 1.2);
	// A quarter of a cycle in, the square is low until halfway, then high; a whole cycle on, the same.
	CHECK(LightIntensityAt(light, 0.0) == doctest::Approx(0.4f));
	CHECK(LightIntensityAt(light, 0.4) == doctest::Approx(4.0f));
	CHECK(LightIntensityAt(light, 0.4 + 1.2 * 3.0) == doctest::Approx(4.0f));
	// A light without one shines as it is at any time.
	CHECK(LightIntensityAt(LightComponent{}, 12.5) == LightComponent{}.intensity);

	Scene again;
	again.LoadText(scene.SaveText());
	CHECK(again.SaveText() == scene.SaveText());

	const auto refuses = [&](const std::string &animation, const char *reason)
	{
		CheckRefused(
		    scene, "  - id: l\n    light:\n      range: 5\n      animation:\n" + animation, reason);
	};
	refuses("        period: 0\n", "period that is not positive");
	refuses("        from: -1\n", "negative or not finite");
}

TEST_CASE("Scene: outlines and masks load as given, are saved, and are checked")
{
	const std::string text =
	    "version: 2\nentities:\n"
	    "  - id: crate\n    outline:\n      style: selected\n      color: [1, 0.5, 0, 2]\n"
	    "  - id: pod\n    outline:\n      style: targeted\n      width: 14\n"
	    "  - id: display\n    mask:\n      unprocessed: true\n      game: 21\n"
	    "  - id: seat\n    mask:\n      cockpit: true\n";
	Scene scene;
	scene.LoadText(text);
	auto &registry = scene.Registry();
	int selected = 0;
	int targeted = 0;
	for (const auto [entity, outline] : registry.view<const OutlineComponent>().each())
	{
		if (outline.style == OutlineStyle::Selected)
		{
			++selected;
			CHECK(outline.color == glm::vec4{1.0f, 0.5f, 0.0f, 2.0f});
		}
		else
		{
			++targeted;
			CHECK(outline.width == 14.0f);
		}
	}
	CHECK(selected == 1);
	CHECK(targeted == 1);
	int unprocessed = 0;
	int cockpits = 0;
	for (const auto [entity, mask] : registry.view<const MaskComponent>().each())
	{
		if (mask.unprocessed)
		{
			++unprocessed;
			CHECK(mask.game == 21);
		}
		cockpits += mask.cockpit ? 1 : 0;
	}
	CHECK(unprocessed == 1);
	CHECK(cockpits == 1);

	Scene again;
	again.LoadText(scene.SaveText());
	CHECK(again.SaveText() == scene.SaveText());

	CheckRefused(scene, "  - id: o\n    outline:\n      width: 40\n", "pixels wide");
	CheckRefused(scene, "  - id: o\n    outline:\n      color: [1, -1, 0, 1]\n", "negative");
	CheckRefused(scene, "  - id: m\n    mask:\n      game: 32\n", "five, 0 to 31");
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

TEST_CASE("Scene: an emitter loads with its frame found, and is saved as it was given")
{
	Scene scene;
	scene.LoadText(R"(version: 2
entities:
  - id: station
  - id: nozzle
    parent: station
    particles:
      rate: 120
      lifetime: 2.5
      spread: 0.2
      size: [0.4, 3]
      color: [0.5, 0.7, 1, 6]
      opacity: 0.2
      capacity: 512
      frame: station
)");
	auto &registry = scene.Registry();
	const auto nozzle = registry.view<const ParticleEmitterComponent>().front();
	const auto &emitter = registry.get<ParticleEmitterComponent>(nozzle);
	CHECK(emitter.rate == 120.0f);
	CHECK(emitter.size == glm::vec2{0.4f, 3.0f});
	CHECK(emitter.colorEnd == ParticleEmitterComponent{}.colorEnd);
	CHECK(emitter.capacity == 512u);
	REQUIRE(registry.valid(emitter.frame));
	CHECK(registry.get<SceneEntityId>(emitter.frame).value == "station");
	const std::string saved = scene.SaveText();
	CHECK(saved.find("frame: station") != std::string::npos);
	Scene again;
	again.LoadText(saved);
	CHECK(again.SaveText() == saved);

	// The emitter's own frame when it names none.
	scene.LoadText("version: 2\nentities:\n  - id: a\n    particles: {}\n");
	CHECK(
	    scene.Registry()
	        .get<ParticleEmitterComponent>(
	            scene.Registry().view<const ParticleEmitterComponent>().front())
	        .frame == entt::null);

	CheckRefused(scene, "  - id: a\n    particles:\n      lifetime: 0\n", "lifetime must be positive");
	CheckRefused(scene, "  - id: a\n    particles:\n      opacity: 2\n", "from 0 to 1");
	CheckRefused(scene, "  - id: a\n    particles:\n      spread: 4\n", "from 0 to pi");
	CheckRefused(scene, "  - id: a\n    particles:\n      capacity: 0\n", "capacity is from 1");
	CheckRefused(
	    scene, "  - id: a\n    particles:\n      color: [1, 1, -1, 1]\n", "cannot be negative");
	CheckRefused(scene, "  - id: a\n    particles:\n      frame: b\n", "Invalid particles frame");
	CheckRefused(scene, "  - id: a\n    particles:\n      frame: a\n", "Invalid particles frame");
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

namespace
{
// Two clamps of the pad clamp's clip (animations/pad-clamp.clip.yaml), bound to one player on the
// pad: each node bound to both clamps' parts, which move alike. Authored stowed but at full size, so
// the clip's put-away clamp node can be seen to take over.
constexpr const char *kClampScene = R"(version: 2
entities:
  - id: pad
    clip:
      file: animations/pad-clamp.clip.yaml
      name: close
      nodes:
        - {node: clamp, entity: clamp-a}
        - {node: clamp, entity: clamp-b}
        - {node: post, entity: post-a}
        - {node: arm, entity: arm-a}
        - {node: jaw, entity: jaw-a}
        - {node: post, entity: post-b}
        - {node: arm, entity: arm-b}
        - {node: jaw, entity: jaw-b}
  - id: clamp-a
    parent: pad
    transform:
      translation: [5, 0, 0]
  - id: post-a
    parent: clamp-a
    transform:
      scale: [1, 0.001, 1]
  - id: arm-a
    parent: clamp-a
    transform:
      translation: [0, -0.8, 0]
  - id: jaw-a
    parent: arm-a
    transform:
      translation: [0, 0.7, 0]
  - id: clamp-b
    parent: pad
    transform:
      translation: [-5, 0, 0]
      rotation: [0, 3.1415927, 0]
  - id: post-b
    parent: clamp-b
    transform:
      scale: [1, 0.001, 1]
  - id: arm-b
    parent: clamp-b
    transform:
      translation: [0, -0.8, 0]
  - id: jaw-b
    parent: arm-b
    transform:
      translation: [0, 0.7, 0]
)";

entt::entity Find(const entt::registry &registry, std::string_view id)
{
	for (const auto [entity, found] : registry.view<const SceneEntityId>().each())
		if (found.value == id)
			return entity;
	return entt::null;
}

glm::dvec3 WorldPosition(const entt::registry &registry, std::string_view id)
{
	return glm::dvec3{registry.get<WorldTransformComponent>(Find(registry, id)).matrix[3]};
}
} // namespace

TEST_CASE("Scene: a clip file's clip moves the entities bound to it, and its events are heard once")
{
	namespace Animation = Jettison::Animation;
	Jettison::Vfs::FileSystem assets;
	assets.AddMount({"base", JETTISON_ASSET_SOURCE_DIR});
	Jettison::Jobs::IoWorker worker;
	Jettison::Resource::ResourceManager resources{
	    assets, worker, {.hotReload = false, .pollInterval = {}}};
	Animation::RegisterClipFileLoader(resources);

	Scene scene{&resources};
	scene.LoadText(kClampScene);
	const std::string authored = scene.SaveText();
	auto &registry = scene.Registry();
	std::vector<ClipEventFired> events;
	{
		const SceneUpdateSystem update{&resources};
		// Until the file is in, the rig is as authored: stowed.
		update.Update(registry, 0.1f, &events);
		CHECK(registry.view<const NodePoseComponent>().empty());
		CHECK(WorldPosition(registry, "arm-a").y == doctest::Approx(-0.8));
		worker.WaitIdle();
		resources.Update();

		// Halfway up, at 0.4 s: the posts half out of the deck and the arms rising with them, each
		// clamp in its own frame.
		for (int frame = 0; frame < 4; ++frame)
			update.Update(registry, 0.1f, &events);
		const auto &player = registry.get<ClipPlayerComponent>(Find(registry, "pad"));
		CHECK(player.time == doctest::Approx(0.4f));
		CHECK(WorldPosition(registry, "arm-a").y == doctest::Approx(0.1).epsilon(1e-4));
		CHECK(WorldPosition(registry, "arm-b").y == doctest::Approx(0.1).epsilon(1e-4));
		const auto postScale = [&](std::string_view id)
		{
			return glm::length(
			    glm::dvec3{registry.get<WorldTransformComponent>(Find(registry, id)).matrix[1]});
		};
		CHECK(postScale("post-a") == doctest::Approx(0.5).epsilon(0.01));
		CHECK(events.empty());

		// Closed at 2 s: the arms swung over towards the middle, the jaws out along them, and the
		// close heard once, from the pad, however long it is held there.
		for (int frame = 0; frame < 30; ++frame)
			update.Update(registry, 0.1f, &events);
		CHECK(player.time == 2.0f);
		REQUIRE(events.size() == 1);
		CHECK(events[0].player == Find(registry, "pad"));
		CHECK(events[0].name == "clamp.closed");
		const glm::dvec3 jawA = WorldPosition(registry, "jaw-a");
		const glm::dvec3 jawB = WorldPosition(registry, "jaw-b");
		CHECK(jawA.x == doctest::Approx(5.0 + 0.837).epsilon(0.01));
		CHECK(jawA.y == doctest::Approx(0.852).epsilon(0.01));
		CHECK(jawB.x == doctest::Approx(-jawA.x));
		CHECK(jawB.y == doctest::Approx(jawA.y));
		// The pose and the phase are the runtime's, and the file is what was authored.
		CHECK(scene.SaveText() == authored);
		CHECK(registry.get<TransformComponent>(Find(registry, "arm-a")).translation.y == -0.8);

		// Set to open, it lets go at once and is heard so.
		auto &opening = registry.get<ClipPlayerComponent>(Find(registry, "pad"));
		opening.clip = "open";
		opening.time = 0.0f;
		events.clear();
		update.Update(registry, 0.1f, &events);
		REQUIRE(events.size() == 1);
		CHECK(events[0].name == "clamp.released");
		// Stowed at the open's end, the clamp is put away at no size, which is not drawn.
		for (int frame = 0; frame < 20; ++frame)
			update.Update(registry, 0.1f, &events);
		CHECK(
		    registry.get<WorldTransformComponent>(Find(registry, "jaw-a")).matrix[0] ==
		    glm::dvec4{0.0});

		// A clip the file does not have leaves the rig as authored, and a binding to a node the file
		// does not name keeps its clip from playing at all.
		opening.clip = "wave";
		update.Update(registry, 0.1f, &events);
		CHECK(registry.view<const NodePoseComponent>().empty());
		CHECK(WorldPosition(registry, "arm-a").y == doctest::Approx(-0.8));
		opening.clip = "close";
		opening.nodes.push_back({"elbow", Find(registry, "arm-a")});
		update.Update(registry, 0.1f, &events);
		CHECK_FALSE(opening.playable);
		CHECK(registry.view<const NodePoseComponent>().empty());
	}
	// The system held the file while it lived, and let it go with it.
	CHECK(resources.Loading() == 0);
}

TEST_CASE("Scene: a clip's file, name, speed and bindings are checked as the scene loads")
{
	Scene scene;
	CheckRefused(
	    scene, "  - id: a\n    clip:\n      file: ../clamp.clip.yaml\n      name: close\n",
	    "not a virtual path");
	CheckRefused(
	    scene, "  - id: a\n    clip:\n      file: animations/pad-clamp.clip.yaml\n", "clip of no name");
	CheckRefused(
	    scene,
	    "  - id: a\n    clip:\n      file: animations/pad-clamp.clip.yaml\n      name: close\n      "
	    "nodes:\n        - {node: arm, entity: b}\n",
	    "not an entity of the scene");
	CheckRefused(
	    scene,
	    "  - id: a\n    clip:\n      file: animations/pad-clamp.clip.yaml\n      name: close\n      "
	    "nodes:\n        - {entity: a}\n",
	    "not an entity of the scene");
	// A node bound to the player's own entity is a rig of one, and the binding is saved as written.
	scene.LoadText(
	    "version: 2\nentities:\n  - id: a\n    clip:\n      file: animations/pad-clamp.clip.yaml\n"
	    "      name: close\n      nodes:\n        - {node: arm, entity: a}\n");
	const std::string saved = scene.SaveText();
	CHECK(saved.find("node: arm") != std::string::npos);
	CHECK(saved.find("time") == std::string::npos);
}
