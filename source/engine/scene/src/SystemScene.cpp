#include <jettison/scene/SystemScene.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <jettison/core/Hash.h>
#include <jettison/math/Random.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>

namespace Jettison::Scene
{
namespace
{
using Celestial::BodyKind;

// The star as the sample scene lights it: irradiance of about pi, so a white Lambertian surface
// facing it shows white.
constexpr SunComponent kSun{{1.0f, 0.95f, 0.88f}, 3.4f};

// A body drawn as its kind is. Sizes are in body radii, and a body with no radius (a station or a
// jump point the file leaves it out of) is drawn as one of the given size, in metres.
struct Look
{
	std::optional<Shape> shape;
	glm::vec3 color{1.0f};
	glm::vec3 scale{1.0f};
	double sizeWithoutRadius{1.0};
	float innerRadius{MeshComponent{}.innerRadius};
	Layer layer{Layer::Environment};
};

Look LookOf(BodyKind kind)
{
	switch (kind)
	{
		case BodyKind::Star:
			// The sky draws the star's disc, at the size its placement gives, and the sun lights the
			// rest: a sphere would be drawn over the disc and lit by the star it is.
			return {};
		case BodyKind::Planet: return {Shape::Sphere, {0.3f, 0.4f, 0.55f}, glm::vec3{1.0f}, 1.0e6};
		case BodyKind::Moon: return {Shape::CubeSphere, {0.45f, 0.44f, 0.42f}, glm::vec3{1.0f}, 1.0e5};
		case BodyKind::Station:
			// A hull along the station's z axis, the length of its radius either side.
			return {Shape::Box, {0.62f, 0.62f, 0.66f}, {0.25f, 0.25f, 1.0f}, 50.0};
		case BodyKind::JumpPoint:
			// A marker, a kilometre across, in the system's plane.
			return {Shape::Ring, {0.3f, 0.6f, 1.0f}, glm::vec3{1.0f}, 500.0, 0.9f, Layer::Effects};
	}
	return {};
}

// What the first station, or else the first planet, or else the primary is: the body a camera
// opens on.
std::size_t OpeningBody(const Celestial::StarSystem &system)
{
	for (const BodyKind kind : {BodyKind::Station, BodyKind::Planet})
		for (std::size_t body = 0; body < system.bodies.size(); ++body)
			if (system.bodies[body].kind == kind)
				return body;
	return 0;
}

// The sky a system's name makes: the sample's starfield, with the galaxy's band at an angle and a
// nebula of a hue that are the system's own.
SkyComponent SkyOf(const Celestial::StarSystem &system)
{
	const std::uint64_t hash = Core::Fnv1a64(system.name);
	Math::Random random{hash};
	SkyComponent sky;
	sky.seed = static_cast<std::uint32_t>(hash ^ (hash >> 32u));
	sky.stars = 6000.0f;
	sky.starFlux = 3.0e-5f;
	glm::vec3 normal{0.0f};
	while (glm::length(normal) < 0.1f || glm::length(normal) > 1.0f)
		normal = {random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f), random.Range(-1.0f, 1.0f)};
	sky.bandNormal = glm::normalize(normal);
	sky.bandWidth = 0.22f;
	sky.bandColor = {0.055f, 0.048f, 0.04f};
	const glm::vec3 hue{random.NextFloat(), random.NextFloat(), random.NextFloat()};
	sky.nebulaColor = 0.035f * hue / std::max({hue.x, hue.y, hue.z, 0.01f});
	sky.nebulaScale = 2.0f;
	return sky;
}

// The entities, and every entity parented to one of them however far down, destroyed.
void DestroyWithDescendants(Scene &scene, std::unordered_set<entt::entity> doomed)
{
	entt::registry &registry = scene.Registry();
	for (bool grew = true; grew;)
	{
		grew = false;
		for (const auto [entity, parent] : registry.view<const ParentComponent>().each())
			if (!doomed.contains(entity) && doomed.contains(parent.entity))
				grew = doomed.insert(entity).second;
	}
	for (const entt::entity entity : doomed)
		if (registry.valid(entity))
			scene.Destroy(entity);
}
} // namespace

void ResolveCelestialBodies(const entt::registry &presentation, const Celestial::StarSystem &system)
{
	for (const auto [entity, celestial] : presentation.view<const CelestialBodyComponent>().each())
		if (system.Find(celestial.body) < 0)
			throw std::runtime_error(
			    "The scene names celestial body '" + celestial.body + "', which star system '" +
			    system.name + "' does not have");
}

void PlaceCelestialBodies(
    entt::registry &presentation, const Celestial::StarSystem &system, double simSeconds)
{
	for (const auto [entity, celestial] : presentation.view<const CelestialBodyComponent>().each())
	{
		const int index = system.Find(celestial.body);
		if (index < 0)
			continue;
		const auto body = static_cast<std::size_t>(index);
		presentation.emplace_or_replace<PlacementComponent>(
		    entity, Celestial::EvaluateBody(system, body, simSeconds).metres,
		    Celestial::Orientation(system.bodies[body].rotation, simSeconds),
		    system.bodies[body].radius);
	}
}

void SyncSimProxies(
    entt::registry &presentation, const Sim::Snapshot &previous, const Sim::Snapshot &current,
    double alpha)
{
	for (auto [entity, proxy, transform] :
	     presentation.view<const SimProxyComponent, TransformComponent>().each())
	{
		const std::optional<Sim::Transform> shown =
		    Sim::Interpolate(previous, current, proxy.simEntity, alpha);
		if (!shown)
			continue;
		transform.translation = shown->position;
		// The scene's rotation is Euler angles applied about x, then y, then z, which is the
		// order extractEulerAngleXYZ reads them back in.
		const glm::mat4 rotation = glm::mat4_cast(glm::quat{shown->orientation});
		glm::extractEulerAngleXYZ(
		    rotation, transform.rotation.x, transform.rotation.y, transform.rotation.z);
	}
}

std::string BodyEntityId(std::string_view bodyName)
{
	std::string id;
	bool separate = false;
	for (const char character : bodyName)
	{
		const auto byte = static_cast<unsigned char>(character);
		if (std::isalnum(byte) == 0)
		{
			separate = !id.empty();
			continue;
		}
		if (separate)
			id += '-';
		separate = false;
		id += static_cast<char>(std::tolower(byte));
	}
	return id;
}

SystemScene::SystemScene(Scene &scene, Celestial::StarSystem system)
    : m_pScene{&scene}, m_system{std::move(system)}
{
	try
	{
		Build();
	}
	catch (...)
	{
		Unload();
		throw;
	}
}

void SystemScene::Build()
{
	entt::registry &registry = m_pScene->Registry();
	ResolveCelestialBodies(registry, m_system);

	std::vector<entt::entity> bodies(m_system.bodies.size(), entt::entity{entt::null});
	for (const auto [entity, celestial] : registry.view<const CelestialBodyComponent>().each())
	{
		const auto body = static_cast<std::size_t>(m_system.Find(celestial.body));
		if (bodies[body] == entt::null)
			bodies[body] = entity;
	}
	std::unordered_set<std::string> ids;
	for (const auto [entity, id] : registry.view<const SceneEntityId>().each())
		ids.insert(id.value);
	const auto claim = [&ids](const std::string &id)
	{
		if (!ids.insert(id).second)
			throw std::runtime_error(
			    "The system scene builds an entity '" + id +
			    "', and the scene already has an entity of that id");
	};
	// Every id is claimed before anything is built, so a clash leaves the scene untouched.
	for (std::size_t body = 0; body < bodies.size(); ++body)
		if (bodies[body] == entt::null)
			claim(BodyEntityId(m_system.bodies[body].name));
	const bool sky = registry.view<const SkyComponent>().empty();
	if (sky)
		claim(BodyEntityId(m_system.name) + "-sky");
	bool camera = true;
	for (const auto [entity, component] : registry.view<const CameraComponent>().each())
		camera = camera && !component.active;
	if (camera)
		claim(BodyEntityId(m_system.name) + "-camera");

	const bool sun = registry.view<const SunComponent>().empty();
	for (std::size_t body = 0; body < bodies.size(); ++body)
		if (bodies[body] == entt::null)
		{
			bodies[body] = BuildBody(body);
			if (sun && body == 0 && m_system.bodies[body].kind == BodyKind::Star)
				registry.emplace<SunComponent>(bodies[body], kSun);
		}
	if (sky)
		BuildSky();
	if (camera && !bodies.empty())
	{
		const std::size_t body = OpeningBody(m_system);
		const entt::entity target = bodies[body];
		const double radius = std::max(m_system.bodies[body].radius, 50.0);
		const entt::entity entity = registry.create();
		registry.emplace<BuiltComponent>(entity);
		registry.emplace<SceneEntityId>(entity, BodyEntityId(m_system.name) + "-camera");
		registry.emplace<TransformComponent>(entity);
		registry.emplace<CameraComponent>(entity);
		OrbitCameraComponent orbit;
		orbit.targetId = registry.get<SceneEntityId>(target).value;
		orbit.target = target;
		// Out to three of the body's radii, as the shell's next-body key frames one.
		orbit.distance = radius * 3.0;
		orbit.minDistance = radius * 1.1;
		registry.emplace<OrbitCameraComponent>(entity, std::move(orbit));
	}
}

entt::entity SystemScene::BuildBody(std::size_t body)
{
	entt::registry &registry = m_pScene->Registry();
	const Celestial::Body &data = m_system.bodies[body];
	const Look look = LookOf(data.kind);
	const entt::entity entity = registry.create();
	registry.emplace<BuiltComponent>(entity);
	registry.emplace<SceneEntityId>(entity, BodyEntityId(data.name));
	registry.emplace<CelestialBodyComponent>(entity, data.name);
	const double size = data.radius > 0.0 ? data.radius : look.sizeWithoutRadius;
	registry.emplace<TransformComponent>(
	    entity, glm::dvec3{0.0}, glm::vec3{0.0f}, look.scale * static_cast<float>(size));
	if (look.layer != Layer::Environment)
		registry.emplace<LayerComponent>(entity, look.layer);
	if (look.shape)
	{
		MeshComponent mesh;
		mesh.shape = look.shape;
		mesh.color = look.color;
		mesh.innerRadius = look.innerRadius;
		registry.emplace<MeshComponent>(entity, std::move(mesh));
	}
	return entity;
}

void SystemScene::BuildSky()
{
	entt::registry &registry = m_pScene->Registry();
	const entt::entity entity = registry.create();
	registry.emplace<BuiltComponent>(entity);
	registry.emplace<SceneEntityId>(entity, BodyEntityId(m_system.name) + "-sky");
	registry.emplace<TransformComponent>(entity);
	registry.emplace<SkyComponent>(entity, SkyOf(m_system));
}

void SystemScene::Place(double simSeconds)
{
	PlaceCelestialBodies(m_pScene->Registry(), m_system, simSeconds);
}

void SystemScene::SyncTraffic(const Sim::Snapshot &previous, const Sim::Snapshot &current, double alpha)
{
	entt::registry &registry = m_pScene->Registry();
	std::unordered_set<entt::entity> gone;
	for (auto proxy = m_proxies.begin(); proxy != m_proxies.end();)
		if (current.Find(proxy->first) == nullptr)
		{
			if (registry.valid(proxy->second))
				gone.insert(proxy->second);
			proxy = m_proxies.erase(proxy);
		}
		else
			++proxy;
	std::erase_if(
	    m_declined, [&current](entt::entity entity) { return current.Find(entity) == nullptr; });
	if (!gone.empty())
		DestroyWithDescendants(*m_pScene, std::move(gone));

	if (m_factory)
		for (const Sim::TransformSample &sample : current.transforms)
		{
			if (m_proxies.contains(sample.entity) || m_declined.contains(sample.entity))
				continue;
			const entt::entity proxy = m_factory(registry, sample.entity);
			if (proxy == entt::null || !registry.valid(proxy))
			{
				m_declined.insert(sample.entity);
				continue;
			}
			registry.emplace_or_replace<SimProxyComponent>(proxy, sample.entity);
			registry.emplace_or_replace<BuiltComponent>(proxy);
			if (!registry.all_of<LayerComponent>(proxy))
				registry.emplace<LayerComponent>(proxy, Layer::Traffic);
			if (!registry.all_of<TransformComponent>(proxy))
				registry.emplace<TransformComponent>(proxy);
			m_proxies.emplace(sample.entity, proxy);
		}

	SyncSimProxies(registry, previous, current, alpha);
}

void SystemScene::Unload()
{
	entt::registry &registry = m_pScene->Registry();
	const auto built = registry.view<const BuiltComponent>();
	DestroyWithDescendants(*m_pScene, {built.begin(), built.end()});
	m_proxies.clear();
	m_declined.clear();
}
} // namespace Jettison::Scene
