#include <jettison/app/CelestialPresentation.h>

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <jettison/celestial/StarSystem.h>
#include <jettison/render/Camera.h>
#include <jettison/render/Scene.h>
#include <jettison/render/SceneSystems.h>

using namespace Jettison;

namespace
{
constexpr double kWidth = 1920.0;
constexpr double kHeight = 1080.0;
constexpr double kStep = 1.0 / 60.0;

Celestial::StarSystem SampleSystem()
{
	std::ifstream file{JETTISON_ASSET_SOURCE_DIR "/systems/halcyon.yaml"};
	std::stringstream text;
	text << file.rdbuf();
	return Celestial::ReadStarSystem(text.str());
}

// Where a point lands on a 1920x1080 screen, in pixels, from clip coordinates; nothing for a point
// behind the camera or well off screen.
std::optional<glm::dvec2> ToPixels(const glm::dvec4 &clip)
{
	if (clip.w <= 0.0)
		return std::nullopt;
	const glm::dvec2 ndc{clip.x / clip.w, clip.y / clip.w};
	if (std::abs(ndc.x) > 1.2 || std::abs(ndc.y) > 1.2)
		return std::nullopt;
	return glm::dvec2{ndc.x * kWidth * 0.5, ndc.y * kHeight * 0.5};
}

// The reference: the point taken through the same transforms in double precision throughout.
std::optional<glm::dvec2> Reference(
    const glm::dmat4 &world, const glm::vec3 &local, const Render::CameraView &camera)
{
	const glm::dvec3 system{world * glm::dvec4{glm::dvec3{local}, 1.0}};
	const glm::dvec3 view = glm::conjugate(camera.orientation) * (system - camera.position);
	const double focal = 1.0 / std::tan(double{camera.verticalFov} * 0.5);
	return ToPixels(
	    {focal / (kWidth / kHeight) * view.x, -focal * view.y, double{camera.nearPlane}, -view.z});
}

// What the GPU computes: the draw's render-space transform, the view and the projection, in float,
// in the order the mesh program multiplies them.
std::optional<glm::dvec2> Engine(
    const glm::mat4 &renderTransform, const glm::vec3 &local, const glm::mat4 &view,
    const glm::mat4 &projection)
{
	return ToPixels(glm::dvec4{projection * view * renderTransform * glm::vec4{local, 1.0f}});
}

// The control: every position made a float in the system frame first, and the camera taken off
// afterwards, as a renderer without a floating origin would.
std::optional<glm::dvec2> Naive(
    const glm::dmat4 &world, const glm::vec3 &local, const Render::CameraView &camera,
    const glm::mat4 &projection)
{
	const glm::mat4 floatWorld{world};
	const glm::mat4 floatView = glm::mat4_cast(glm::quat{glm::conjugate(camera.orientation)}) *
	                            glm::translate(glm::mat4{1.0f}, -glm::vec3{camera.position});
	return ToPixels(glm::dvec4{projection * floatView * floatWorld * glm::vec4{local, 1.0f}});
}

struct Errors
{
	double station{0.0};
	double planet{0.0};
	double naive{0.0};
	int stationSamples{0};
	int planetSamples{0};
};

// Runs the presentation path a frame at a time and measures every sample point's distance, in
// pixels, from where it should be.
Errors Measure(entt::registry &scene, const Celestial::StarSystem &system, double startTime, int frames)
{
	const Render::SceneUpdateSystem update;
	const Render::SceneRenderSystem render;
	const auto model = std::make_shared<Render::Model>(nullptr);
	// The station's boxes' corners, and points on the planet: under the station, on the limb, and
	// between.
	std::array<glm::vec3, 8> corners;
	for (int corner = 0; corner < 8; ++corner)
		corners[static_cast<std::size_t>(corner)] = {
		    corner & 1 ? 1.0f : -1.0f, corner & 2 ? 1.0f : -1.0f, corner & 4 ? 1.0f : -1.0f};

	Errors errors;
	for (int frame = 0; frame < frames; ++frame)
	{
		const double time = startTime + frame * kStep;
		App::SyncCelestialBodies(scene, system, time);
		update.Update(scene, static_cast<float>(kStep));
		Render::UpdateOrbitCameras(scene, kStep);
		const Render::CameraView camera = Render::ActiveCameraView(scene);
		const glm::mat4 view = Render::RenderViewMatrix(camera);
		const glm::mat4 projection = Render::ReversedInfinitePerspective(
		    camera.verticalFov, static_cast<float>(kWidth / kHeight), camera.nearPlane);

		// The draws as the renderer collects them, in the registry's order.
		const auto draws = render.Collect(scene, camera.position);
		std::size_t drawIndex = 0;
		for (const auto [entity, mesh, world] :
		     scene.view<const Render::MeshComponent, const Render::WorldTransformComponent>().each())
		{
			REQUIRE(drawIndex < draws.size());
			const glm::mat4 renderTransform = draws[drawIndex++].renderTransform;
			const bool planet = mesh.shape == Render::Shape::Sphere;
			std::vector<glm::vec3> points(corners.begin(), corners.end());
			if (planet)
			{
				points.clear();
				const glm::dvec3 centre{world.matrix[3]};
				const glm::dvec3 up = glm::normalize(camera.position - centre);
				const glm::dvec3 across = glm::normalize(glm::cross(up, glm::dvec3{0.0, 1.0, 0.0}));
				for (const double angle : {0.0, 0.05, 0.1, 0.2, 0.3, 0.35})
					for (const double side : {-1.0, 1.0})
					{
						const glm::dvec3 direction =
						    glm::normalize(up * std::cos(angle) + across * (side * std::sin(angle)));
						// Into the planet's own frame, where the mesh's unit sphere is.
						points.emplace_back(
						    glm::inverse(glm::dmat3{world.matrix}) * direction *
						    glm::length(glm::dvec3{world.matrix[0]}));
					}
			}
			for (const glm::vec3 &point : points)
			{
				const auto reference = Reference(world.matrix, point, camera);
				const auto engine = Engine(renderTransform, point, view, projection);
				if (!reference || !engine)
					continue;
				const double error = glm::length(*engine - *reference);
				(planet ? errors.planet : errors.station) =
				    std::max(planet ? errors.planet : errors.station, error);
				if (const auto naive = Naive(world.matrix, point, camera, projection))
					errors.naive = std::max(errors.naive, glm::length(*naive - *reference));
				++(planet ? errors.planetSamples : errors.stationSamples);
			}
		}
	}
	return errors;
}

// A procedural mesh with a model that has no device: the draw list reads its transform, not its
// buffers.
Render::MeshComponent Mesh(Render::Shape shape)
{
	Render::MeshComponent mesh;
	mesh.shape = shape;
	mesh.model = std::make_shared<Render::Model>(nullptr);
	return mesh;
}

entt::entity Body(entt::registry &scene, const char *body, const glm::vec3 &scale, Render::Shape shape)
{
	const entt::entity entity = scene.create();
	scene.emplace<Render::TransformComponent>(entity, glm::dvec3{0.0}, glm::vec3{0.0f}, scale);
	scene.emplace<Render::CelestialBodyComponent>(entity, body);
	scene.emplace<Render::MeshComponent>(entity, Mesh(shape));
	return entity;
}
} // namespace

TEST_CASE(
    "Large world: a station orbiting a planet at real scale is drawn within a hundredth of a pixel")
{
	const Celestial::StarSystem system = SampleSystem();
	entt::registry scene;
	const entt::entity planet = Body(scene, "Ostra", glm::vec3{6.371e6f}, Render::Shape::Sphere);
	// The station's hub, 60 metres long, with a panel 30 metres off its side.
	const entt::entity station = Body(scene, "Ostra Highport", {6.0f, 6.0f, 30.0f}, Render::Shape::Box);
	const entt::entity panel = scene.create();
	scene.emplace<Render::TransformComponent>(
	    panel, glm::dvec3{30.0, 0.0, 0.0}, glm::vec3{0.0f}, glm::vec3{8.0f, 0.2f, 4.0f});
	scene.emplace<Render::ParentComponent>(panel, station);
	scene.emplace<Render::MeshComponent>(panel, Mesh(Render::Shape::Box));

	const entt::entity camera = scene.create();
	scene.emplace<Render::CameraComponent>(camera, glm::dvec3{0.0}, glm::vec3{0.0f}, 50.0f, 0.5f, true);
	// Out along the station's radial, looking back past it, so the planet beneath it is in the
	// frame too.
	constexpr double kStart = 1.0e8;
	const glm::dvec3 radial = glm::normalize(
	    Celestial::EvaluateBody(system, static_cast<std::size_t>(system.Find("Ostra Highport")), kStart)
	        .metres -
	    Celestial::EvaluateBody(system, static_cast<std::size_t>(system.Find("Ostra")), kStart).metres);
	Render::OrbitCameraComponent orbit;
	orbit.targetId = "station";
	orbit.target = station;
	orbit.distance = 150.0;
	orbit.yaw = std::atan2(radial.x, radial.z);
	orbit.pitch = std::asin(radial.y);
	orbit.minDistance = 20.0;
	scene.emplace<Render::OrbitCameraComponent>(camera, std::move(orbit));
	App::ResolveCelestialBodies(scene, system);

	// Three years into the clock, ten seconds of frames, the camera following the station at
	// 7.7 km/s round a planet 150 million kilometres from its star.
	const Errors near = Measure(scene, system, kStart, 600);
	MESSAGE(
	    "following the station: station ", near.station, " px over ", near.stationSamples,
	    " samples, planet ", near.planet, " px over ", near.planetSamples,
	    "; without the floating origin ", near.naive, " px");
	// Every frame has station corners and points on the planet in view.
	CHECK(near.stationSamples >= 600 * 8);
	CHECK(near.planetSamples >= 600 * 3);
	CHECK(near.station < 0.01);
	CHECK(near.planet < 0.01);
	// The measure can see jitter: the same frames made floats in the system frame first are
	// thousands of pixels out.
	CHECK(near.naive > 100.0);

	// Following the planet from twenty thousand kilometres, the station passing beneath.
	auto &rig = scene.get<Render::OrbitCameraComponent>(camera);
	rig.target = planet;
	rig.distance = 2.0e7;
	rig.minDistance = 1.0e7;
	rig.placed = false;
	const Errors far = Measure(scene, system, kStart, 600);
	MESSAGE(
	    "following the planet: station ", far.station, " px over ", far.stationSamples,
	    " samples, planet ", far.planet, " px over ", far.planetSamples);
	CHECK(far.planetSamples >= 600 * 3);
	CHECK(far.station < 0.01);
	CHECK(far.planet < 0.01);
}

TEST_CASE("Large world: a body the system does not have is refused at load")
{
	const Celestial::StarSystem system = SampleSystem();
	entt::registry scene;
	Body(scene, "Nowhere", glm::vec3{1.0f}, Render::Shape::Box);
	CHECK_THROWS_WITH_AS(
	    App::ResolveCelestialBodies(scene, system), doctest::Contains("'Nowhere'"), std::runtime_error);
}

TEST_CASE("Large world: a body's placement is its orbit, and the authored transform is left alone")
{
	const Celestial::StarSystem system = SampleSystem();
	entt::registry scene;
	const entt::entity station = Body(scene, "Ostra Highport", glm::vec3{1.0f}, Render::Shape::Box);
	App::SyncCelestialBodies(scene, system, 1234.5);
	const auto &placement = scene.get<Render::PlacementComponent>(station);
	const auto index = static_cast<std::size_t>(system.Find("Ostra Highport"));
	CHECK(placement.position == Celestial::EvaluateBody(system, index, 1234.5).metres);
	CHECK(scene.get<Render::TransformComponent>(station).translation == glm::dvec3{0.0});
	Render::SceneUpdateSystem{}.Update(scene, 0.0f);
	CHECK(
	    glm::dvec3{scene.get<Render::WorldTransformComponent>(station).matrix[3]} ==
	    placement.position);
}
