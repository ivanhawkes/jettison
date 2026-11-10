#include <jettison/render/Scene.h>
#include <jettison/render/SceneSystems.h>

#include <doctest/doctest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using Jettison::Render::Scene;
using Jettison::Render::SceneUpdateSystem;

namespace
{
// A scene with no meshes: everything in it loads and saves without a device, which is what lets
// the round trip run in a unit test rather than only in the render test.
constexpr const char *kSceneWithoutModels = R"(version: 1
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
      ambient: [0.1, 0.1, 0.1]
      active: true
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

std::string ReadFile(const std::filesystem::path &path)
{
	std::ifstream input{path};
	std::ostringstream contents;
	contents << input.rdbuf();
	return contents.str();
}

class TemporaryDirectory
{
  public:
	TemporaryDirectory()
	    : m_path{
	          std::filesystem::temp_directory_path() /
	          ("jettison-render-tests-" + std::to_string(::getpid()))}
	{
		std::filesystem::create_directories(m_path);
	}
	~TemporaryDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(m_path, error);
	}
	TemporaryDirectory(const TemporaryDirectory &) = delete;
	TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

	const std::filesystem::path &Path() const
	{
		return m_path;
	}

  private:
	std::filesystem::path m_path;
};
} // namespace

TEST_CASE("Scene: a saved scene loads back to the same file, with the frame loop's phase unsaved")
{
	const TemporaryDirectory directory;
	const auto authored = directory.Path() / "authored.yaml";
	const auto first = directory.Path() / "first.yaml";
	const auto second = directory.Path() / "second.yaml";

	const auto third = directory.Path() / "third.yaml";
	std::ofstream{authored} << kSceneWithoutModels;

	Scene scene{nullptr};
	scene.Load(authored);
	scene.Save(first);
	CHECK(ReadFile(first).find("rotation: [0, 0.3, 0]") != std::string::npos);

	// Ten seconds of animation: the rotation's phase moves, and none of it may reach the file.
	const SceneUpdateSystem update;
	for (int frame = 0; frame < 100; ++frame)
		update.Update(scene.Registry(), 0.1f);
	scene.Save(second);
	CHECK(ReadFile(first) == ReadFile(second));

	// What the writer produced, the loader accepts, and writes back unchanged.
	scene.Load(second);
	scene.Save(third);
	CHECK(ReadFile(second) == ReadFile(third));
}

TEST_CASE("Scene: translations and the camera's position are doubles, in the file and in the world")
{
	const TemporaryDirectory directory;
	const auto authored = directory.Path() / "far.yaml";
	const auto saved = directory.Path() / "far-saved.yaml";
	std::ofstream{authored} << R"(version: 1
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
)";
	Scene scene{nullptr};
	scene.Load(authored);
	SceneUpdateSystem{}.Update(scene.Registry(), 0.0f);
	const auto &registry = scene.Registry();
	for (const auto [entity, id, world] : registry
	                                          .view<
	                                              const Jettison::Render::SceneEntityId,
	                                              const Jettison::Render::WorldTransformComponent>()
	                                          .each())
	{
		if (id.value == "plate")
			CHECK(world.matrix[3].x == 149600000000.125 + 0.001);
	}
	const auto camera = Jettison::Render::ActiveCameraView(registry);
	CHECK(camera.position.x == 149600000000.0625);

	scene.Save(saved);
	Scene again{nullptr};
	again.Load(saved);
	again.Save(saved);
	CHECK(ReadFile(saved).find("149600000000.125") != std::string::npos);
	CHECK(ReadFile(saved).find("149600000000.0625") != std::string::npos);
}

TEST_CASE("Scene: a far plane is refused, since the projection has none")
{
	const TemporaryDirectory directory;
	const auto authored = directory.Path() / "old.yaml";
	std::ofstream{authored} << R"(version: 1
entities:
  - id: camera
    camera:
      far: 10
)";
	Scene scene{nullptr};
	CHECK_THROWS_WITH_AS(
	    scene.Load(authored), doctest::Contains("infinite far plane"), std::runtime_error);
}
