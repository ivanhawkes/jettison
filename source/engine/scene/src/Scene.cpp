#include <jettison/scene/Scene.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <jettison/animation/Components.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/scene/Components.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

namespace Jettison::Scene
{
namespace
{
using Animation::AnimationPlayerComponent;
using Animation::RotationAnimationComponent;
using Reflect::Attribute;

// ---- The file's records ---------------------------------------------------------------------
//
// What the file says, field for field, where the scene's components hold it differently: a part an
// entity may not have is an optional, so the archive leaves it out when it is absent and reads it as
// present when the file names it. A component whose fields are the file's is registered as itself.

struct OrbitRecord
{
	std::string target;
	double distance{OrbitCameraComponent{}.distance};
	double yaw{OrbitCameraComponent{}.yaw};
	double pitch{OrbitCameraComponent{}.pitch};
	double minDistance{OrbitCameraComponent{}.minDistance};
};

struct CameraRecord
{
	glm::dvec3 position{CameraComponent{}.position};
	glm::vec3 rotation{CameraComponent{}.rotation};
	float fov{CameraComponent{}.verticalFovDegrees};
	float nearPlane{CameraComponent{}.nearPlane};
	bool active{CameraComponent{}.active};
	std::optional<OrbitRecord> orbit;
};

// Every light needs its range, since nothing past it is lit and no default would be right for both a
// cabin lamp and a floodlight; only a spot has cone angles.
struct LightRecord
{
	LightType type{LightType::Point};
	glm::vec3 color{LightComponent{}.color};
	float intensity{LightComponent{}.intensity};
	std::optional<float> range;
	std::optional<float> innerConeAngle;
	std::optional<float> outerConeAngle;
	bool active{LightComponent{}.active};
	std::optional<LightAnimation> animation;
};

// The game's stencil bits as a number the archive reads, rather than a byte.
struct MaskRecord
{
	bool cockpit{MaskComponent{}.cockpit};
	bool unprocessed{MaskComponent{}.unprocessed};
	std::uint32_t game{MaskComponent{}.game};
};

struct BandRecord
{
	glm::vec3 normal{SkyComponent{}.bandNormal};
	float width{SkyComponent{}.bandWidth};
	glm::vec3 color{SkyComponent{}.bandColor};
};

struct NebulaRecord
{
	glm::vec3 color{SkyComponent{}.nebulaColor};
	float scale{SkyComponent{}.nebulaScale};
};

struct SkyRecord
{
	std::uint32_t seed{SkyComponent{}.seed};
	float stars{SkyComponent{}.stars};
	float starFlux{SkyComponent{}.starFlux};
	BandRecord band;
	NebulaRecord nebula;
};

// A model or a shape; a shape's colour, and a ring's inner radius.
struct MeshRecord
{
	std::optional<std::string> model;
	std::optional<Shape> shape;
	std::optional<glm::vec3> color;
	std::optional<float> innerRadius;
	std::optional<std::string> material;
};

struct TransformAnimationRecord
{
	glm::vec3 axis{RotationAnimationComponent{}.rotationAxis};
	float speed{RotationAnimationComponent{}.rotationSpeed};
};

// No time: the player's time is the phase the clip is at, which the frame loop owns, and a file that
// names it is refused as runtime state. Written, it started a saved-and-reloaded scene out of step
// with the scene that was authored, and in a long session it wrote a phase that grew with the clock.
struct AnimationRecord
{
	std::string clip;
	float speed{AnimationPlayerComponent{}.playbackSpeed};
	bool loop{AnimationPlayerComponent{}.looping};
	bool playing{AnimationPlayerComponent{}.playing};
};

// A clip file's clip on the scene's entities, each of the clip's nodes bound to an entity by its id.
// No time, as for a rig's clip.
struct ClipNodeRecord
{
	std::string node;
	std::string entity;
};

struct ClipRecord
{
	std::string file;
	std::string name;
	std::vector<ClipNodeRecord> nodes;
	float speed{ClipPlayerComponent{}.speed};
	bool loop{ClipPlayerComponent{}.looping};
	bool playing{ClipPlayerComponent{}.playing};
};

// An emitter's fields, the frame it names by id, and not the pulses a game counts at run time.
struct ParticlesRecord
{
	float rate{ParticleEmitterComponent{}.rate};
	std::uint32_t burst{ParticleEmitterComponent{}.burst};
	float lifetime{ParticleEmitterComponent{}.lifetime};
	float lifetimeJitter{ParticleEmitterComponent{}.lifetimeJitter};
	float speed{ParticleEmitterComponent{}.speed};
	float speedJitter{ParticleEmitterComponent{}.speedJitter};
	float spread{ParticleEmitterComponent{}.spread};
	float radius{ParticleEmitterComponent{}.radius};
	float drag{ParticleEmitterComponent{}.drag};
	glm::vec2 size{ParticleEmitterComponent{}.size};
	float stretch{ParticleEmitterComponent{}.stretch};
	glm::vec4 color{ParticleEmitterComponent{}.color};
	glm::vec4 colorEnd{ParticleEmitterComponent{}.colorEnd};
	float opacity{ParticleEmitterComponent{}.opacity};
	glm::vec3 albedo{ParticleEmitterComponent{}.albedo};
	std::uint32_t capacity{ParticleEmitterComponent{}.capacity};
	std::uint32_t seed{ParticleEmitterComponent{}.seed};
	bool active{ParticleEmitterComponent{}.active};
	std::optional<std::string> frame;
};

// Every entity has a transform, written whether or not the file gave one.
struct EntityRecord
{
	std::string id;
	std::optional<std::string> parent;
	std::optional<Layer> layer;
	TransformComponent transform;
	std::optional<CameraRecord> camera;
	std::optional<SunComponent> sun;
	std::optional<SkyRecord> sky;
	std::optional<CelestialBodyComponent> celestial;
	std::optional<AtmosphereComponent> atmosphere;
	std::optional<RingsComponent> rings;
	std::optional<LightRecord> light;
	std::optional<MeshRecord> mesh;
	std::optional<OutlineComponent> outline;
	std::optional<MaskRecord> mask;
	std::optional<ParticlesRecord> particles;
	std::optional<TransformAnimationRecord> transformAnimation;
	std::optional<AnimationRecord> animation;
	std::optional<ClipRecord> clip;
};

struct SceneDocument
{
	std::optional<std::string> system;
	std::vector<EntityRecord> entities;
};

// The reflection tables are global, and every scene shares them: registered once, by the first
// scene to read or write a file.
void RegisterTypes()
{
	static const bool registered = []
	{
		using Reflect::RegisterEnum;
		using Reflect::RegisterType;
		constexpr Attribute kSaved = Attribute::Saved;
		RegisterEnum<Shape>("Scene.Shape")
		    .Value<Shape::Sphere>("sphere")
		    .Value<Shape::Box>("box")
		    .Value<Shape::IcoSphere>("icosphere")
		    .Value<Shape::CubeSphere>("cubesphere")
		    .Value<Shape::Ring>("ring")
		    .Value<Shape::Cylinder>("cylinder");
		RegisterEnum<Layer>("Scene.Layer")
		    .Value<Layer::Environment>("environment")
		    .Value<Layer::Traffic>("traffic")
		    .Value<Layer::Effects>("effects");
		RegisterEnum<LightType>("Scene.LightType")
		    .Value<LightType::Point>("point")
		    .Value<LightType::Spot>("spot");
		RegisterWave();
		RegisterEnum<OutlineStyle>("Scene.OutlineStyle")
		    .Value<OutlineStyle::Selected>("selected")
		    .Value<OutlineStyle::Targeted>("targeted");
		RegisterType<TransformComponent>("Scene.Transform")
		    .Field<&TransformComponent::translation>("translation", kSaved)
		    .Field<&TransformComponent::rotation>("rotation", kSaved)
		    .Field<&TransformComponent::scale>("scale", kSaved);
		RegisterType<OrbitRecord>("Scene.Orbit")
		    .Field<&OrbitRecord::target>("target", kSaved)
		    .Field<&OrbitRecord::distance>("distance", kSaved)
		    .Field<&OrbitRecord::yaw>("yaw", kSaved)
		    .Field<&OrbitRecord::pitch>("pitch", kSaved)
		    .Field<&OrbitRecord::minDistance>("minDistance", kSaved);
		RegisterType<CameraRecord>("Scene.Camera")
		    .Field<&CameraRecord::position>("position", kSaved)
		    .Field<&CameraRecord::rotation>("rotation", kSaved)
		    .Field<&CameraRecord::fov>("fov", kSaved)
		    .Field<&CameraRecord::nearPlane>("near", kSaved)
		    .Field<&CameraRecord::active>("active", kSaved)
		    .Field<&CameraRecord::orbit>("orbit", kSaved);
		RegisterType<SunComponent>("Scene.Sun")
		    .Field<&SunComponent::color>("color", kSaved)
		    .Field<&SunComponent::intensity>("intensity", kSaved);
		RegisterType<BandRecord>("Scene.Band")
		    .Field<&BandRecord::normal>("normal", kSaved)
		    .Field<&BandRecord::width>("width", kSaved)
		    .Field<&BandRecord::color>("color", kSaved);
		RegisterType<NebulaRecord>("Scene.Nebula")
		    .Field<&NebulaRecord::color>("color", kSaved)
		    .Field<&NebulaRecord::scale>("scale", kSaved);
		RegisterType<SkyRecord>("Scene.Sky")
		    .Field<&SkyRecord::seed>("seed", kSaved)
		    .Field<&SkyRecord::stars>("stars", kSaved)
		    .Field<&SkyRecord::starFlux>("starFlux", kSaved)
		    .Field<&SkyRecord::band>("band", kSaved)
		    .Field<&SkyRecord::nebula>("nebula", kSaved);
		RegisterType<CelestialBodyComponent>("Scene.Celestial")
		    .Field<&CelestialBodyComponent::body>("body", kSaved);
		RegisterType<AtmosphereComponent>("Scene.Atmosphere")
		    .Field<&AtmosphereComponent::height>("height", kSaved)
		    .Field<&AtmosphereComponent::rayleigh>("rayleigh", kSaved)
		    .Field<&AtmosphereComponent::rayleighHeight>("rayleighHeight", kSaved)
		    .Field<&AtmosphereComponent::mie>("mie", kSaved)
		    .Field<&AtmosphereComponent::mieExtinction>("mieExtinction", kSaved)
		    .Field<&AtmosphereComponent::mieHeight>("mieHeight", kSaved)
		    .Field<&AtmosphereComponent::mieAnisotropy>("mieAnisotropy", kSaved);
		RegisterType<RingsComponent>("Scene.Rings")
		    .Field<&RingsComponent::innerRadius>("innerRadius", kSaved)
		    .Field<&RingsComponent::outerRadius>("outerRadius", kSaved)
		    .Field<&RingsComponent::color>("color", kSaved)
		    .Field<&RingsComponent::opacity>("opacity", kSaved)
		    .Field<&RingsComponent::seed>("seed", kSaved);
		RegisterType<LightRecord>("Scene.Light")
		    .Field<&LightRecord::type>("type", kSaved)
		    .Field<&LightRecord::color>("color", kSaved)
		    .Field<&LightRecord::intensity>("intensity", kSaved)
		    .Field<&LightRecord::range>("range", kSaved)
		    .Field<&LightRecord::innerConeAngle>("innerConeAngle", kSaved)
		    .Field<&LightRecord::outerConeAngle>("outerConeAngle", kSaved)
		    .Field<&LightRecord::active>("active", kSaved)
		    .Field<&LightRecord::animation>("animation", kSaved);
		RegisterType<LightAnimation>("Scene.LightAnimation")
		    .Field<&LightAnimation::wave>("wave", kSaved)
		    .Field<&LightAnimation::period>("period", kSaved)
		    .Field<&LightAnimation::phase>("phase", kSaved)
		    .Field<&LightAnimation::from>("from", kSaved)
		    .Field<&LightAnimation::to>("to", kSaved);
		RegisterType<OutlineComponent>("Scene.Outline")
		    .Field<&OutlineComponent::style>("style", kSaved)
		    .Field<&OutlineComponent::color>("color", kSaved)
		    .Field<&OutlineComponent::width>("width", kSaved);
		RegisterType<MaskRecord>("Scene.Mask")
		    .Field<&MaskRecord::cockpit>("cockpit", kSaved)
		    .Field<&MaskRecord::unprocessed>("unprocessed", kSaved)
		    .Field<&MaskRecord::game>("game", kSaved);
		RegisterType<MeshRecord>("Scene.Mesh")
		    .Field<&MeshRecord::model>("model", kSaved)
		    .Field<&MeshRecord::shape>("shape", kSaved)
		    .Field<&MeshRecord::color>("color", kSaved)
		    .Field<&MeshRecord::innerRadius>("innerRadius", kSaved)
		    .Field<&MeshRecord::material>("material", kSaved);
		RegisterType<ParticlesRecord>("Scene.Particles")
		    .Field<&ParticlesRecord::rate>("rate", kSaved)
		    .Field<&ParticlesRecord::burst>("burst", kSaved)
		    .Field<&ParticlesRecord::lifetime>("lifetime", kSaved)
		    .Field<&ParticlesRecord::lifetimeJitter>("lifetimeJitter", kSaved)
		    .Field<&ParticlesRecord::speed>("speed", kSaved)
		    .Field<&ParticlesRecord::speedJitter>("speedJitter", kSaved)
		    .Field<&ParticlesRecord::spread>("spread", kSaved)
		    .Field<&ParticlesRecord::radius>("radius", kSaved)
		    .Field<&ParticlesRecord::drag>("drag", kSaved)
		    .Field<&ParticlesRecord::size>("size", kSaved)
		    .Field<&ParticlesRecord::stretch>("stretch", kSaved)
		    .Field<&ParticlesRecord::color>("color", kSaved)
		    .Field<&ParticlesRecord::colorEnd>("colorEnd", kSaved)
		    .Field<&ParticlesRecord::opacity>("opacity", kSaved)
		    .Field<&ParticlesRecord::albedo>("albedo", kSaved)
		    .Field<&ParticlesRecord::capacity>("capacity", kSaved)
		    .Field<&ParticlesRecord::seed>("seed", kSaved)
		    .Field<&ParticlesRecord::active>("active", kSaved)
		    .Field<&ParticlesRecord::frame>("frame", kSaved);
		RegisterType<TransformAnimationRecord>("Scene.TransformAnimation")
		    .Field<&TransformAnimationRecord::axis>("axis", kSaved)
		    .Field<&TransformAnimationRecord::speed>("speed", kSaved);
		RegisterType<AnimationRecord>("Scene.Animation")
		    .Field<&AnimationRecord::clip>("clip", kSaved)
		    .Field<&AnimationRecord::speed>("speed", kSaved)
		    .Field<&AnimationRecord::loop>("loop", kSaved)
		    .Field<&AnimationRecord::playing>("playing", kSaved);
		RegisterType<ClipNodeRecord>("Scene.ClipNode")
		    .Field<&ClipNodeRecord::node>("node", kSaved)
		    .Field<&ClipNodeRecord::entity>("entity", kSaved);
		RegisterType<ClipRecord>("Scene.Clip")
		    .Field<&ClipRecord::file>("file", kSaved)
		    .Field<&ClipRecord::name>("name", kSaved)
		    .Field<&ClipRecord::nodes>("nodes", kSaved)
		    .Field<&ClipRecord::speed>("speed", kSaved)
		    .Field<&ClipRecord::loop>("loop", kSaved)
		    .Field<&ClipRecord::playing>("playing", kSaved);
		RegisterType<EntityRecord>("Scene.Entity")
		    .Field<&EntityRecord::id>("id", kSaved)
		    .Field<&EntityRecord::parent>("parent", kSaved)
		    .Field<&EntityRecord::layer>("layer", kSaved)
		    .Field<&EntityRecord::transform>("transform", kSaved)
		    .Field<&EntityRecord::camera>("camera", kSaved)
		    .Field<&EntityRecord::sun>("sun", kSaved)
		    .Field<&EntityRecord::sky>("sky", kSaved)
		    .Field<&EntityRecord::celestial>("celestial", kSaved)
		    .Field<&EntityRecord::atmosphere>("atmosphere", kSaved)
		    .Field<&EntityRecord::rings>("rings", kSaved)
		    .Field<&EntityRecord::light>("light", kSaved)
		    .Field<&EntityRecord::mesh>("mesh", kSaved)
		    .Field<&EntityRecord::outline>("outline", kSaved)
		    .Field<&EntityRecord::mask>("mask", kSaved)
		    .Field<&EntityRecord::particles>("particles", kSaved)
		    .Field<&EntityRecord::transformAnimation>("transformAnimation", kSaved)
		    .Field<&EntityRecord::animation>("animation", kSaved)
		    .Field<&EntityRecord::clip>("clip", kSaved);
		RegisterType<SceneDocument>("Scene")
		    .Field<&SceneDocument::system>("system", kSaved)
		    .Field<&SceneDocument::entities>("entities", kSaved);
		return true;
	}();
	(void)registered;
}

// ---- What a file may say --------------------------------------------------------------------

[[noreturn]] void Refuse(const std::string &message)
{
	throw std::runtime_error(message);
}

void CheckVirtualPath(const std::string &path, const char *what, const std::string &id)
{
	if (!Vfs::FileSystem::IsValidPath(path))
		Refuse(
		    std::format(
		        "Entity '{}' has {} '{}', which is not a virtual path (models/name.glb, relative and "
		        "forward-slashed)",
		        id, what, path));
}

void CheckLightAnimation(const LightRecord &light, const std::string &id)
{
	if (!light.animation)
		return;
	const LightAnimation &animation = *light.animation;
	if (!(animation.period > 0.0) || !std::isfinite(animation.period) ||
	    !std::isfinite(animation.phase))
		Refuse("Light '" + id + "' animates with a period that is not positive and finite");
	if (!(animation.from >= 0.0f) || !(animation.to >= 0.0f) || !std::isfinite(animation.from) ||
	    !std::isfinite(animation.to))
		Refuse("Light '" + id + "' animates its intensity by a share that is negative or not finite");
}

void CheckOutline(const OutlineComponent &outline, const std::string &id)
{
	if (!(outline.width >= 1.0f) || !(outline.width <= kMaxOutlineWidth))
		Refuse(
		    std::format(
		        "Entity '{}' has an outline {} pixels wide; it is 1 to {}", id, outline.width,
		        kMaxOutlineWidth));
	for (int channel = 0; channel < 4; ++channel)
		if (!(outline.color[channel] >= 0.0f) || !std::isfinite(outline.color[channel]))
			Refuse("Entity '" + id + "' has an outline colour that is negative or not finite");
}

void CheckLight(const LightRecord &light, const std::string &id)
{
	if (!light.range)
		Refuse("Light '" + id + "' needs a range: a local light lights nothing past it");
	if (light.intensity < 0.0f)
		Refuse("Light intensity cannot be negative: " + id);
	if (!(*light.range > 0.0f) || !std::isfinite(*light.range))
		Refuse("Light range must be positive and finite: " + id);
	const bool hasCone = light.innerConeAngle || light.outerConeAngle;
	CheckLightAnimation(light, id);
	if (light.type != LightType::Spot)
	{
		if (hasCone)
			Refuse("Light '" + id + "' has cone angles, which only a spot has");
		return;
	}
	const float inner = light.innerConeAngle.value_or(LightComponent{}.innerConeAngle);
	const float outer = light.outerConeAngle.value_or(LightComponent{}.outerConeAngle);
	// KHR_lights_punctual's rule, and the cone test's: the culling assumes a cone no wider than a
	// hemisphere.
	constexpr float kQuarterTurn = 1.5707964f;
	if (!(inner >= 0.0f) || !(inner < outer) || !(outer <= kQuarterTurn))
		Refuse(
		    "Spot '" + id +
		    "' needs 0 <= innerConeAngle < outerConeAngle <= pi / 2, in radians from its axis");
}

void CheckSky(const SkyRecord &sky, const std::string &id)
{
	const auto negative = [](const glm::vec3 &value)
	{ return value.x < 0.0f || value.y < 0.0f || value.z < 0.0f; };
	if (!(sky.stars >= 0.0f) || !(sky.starFlux >= 0.0f) || !(sky.band.width > 0.0f) ||
	    !(sky.nebula.scale > 0.0f) || negative(sky.band.color) || negative(sky.nebula.color))
		Refuse(
		    "Sky stars, flux and colours cannot be negative, and the band's width and the nebula's "
		    "scale must be positive: " +
		    id);
	if (glm::length(sky.band.normal) == 0.0f)
		Refuse("Sky band normal cannot be zero: " + id);
}

void CheckAtmosphere(const AtmosphereComponent &atmosphere, const std::string &id)
{
	const glm::vec3 &rayleigh = atmosphere.rayleigh;
	if (!(atmosphere.height > 0.0) || !(atmosphere.rayleighHeight > 0.0f) ||
	    !(atmosphere.mieHeight > 0.0f) || !(rayleigh.x >= 0.0f) || !(rayleigh.y >= 0.0f) ||
	    !(rayleigh.z >= 0.0f) || !(atmosphere.mie >= 0.0f) ||
	    !(atmosphere.mieExtinction >= atmosphere.mie))
		Refuse(
		    "Atmosphere height and scale heights must be positive, its coefficients not negative, and "
		    "its Mie extinction not less than its scattering: " +
		    id);
	if (!(atmosphere.mieAnisotropy > -1.0f) || !(atmosphere.mieAnisotropy < 1.0f))
		Refuse("Atmosphere mieAnisotropy must be between -1 and 1: " + id);
}

void CheckRings(const RingsComponent &rings, const std::string &id)
{
	if (!(rings.innerRadius > 0.0) || !(rings.outerRadius > rings.innerRadius))
		Refuse("Rings innerRadius must be positive and less than outerRadius: " + id);
	if (!(rings.opacity >= 0.0f) || !(rings.opacity <= 1.0f) || !(rings.color.x >= 0.0f) ||
	    !(rings.color.y >= 0.0f) || !(rings.color.z >= 0.0f))
		Refuse("Rings opacity must be between 0 and 1, and its colour not negative: " + id);
}

void CheckMesh(const MeshRecord &mesh, const std::string &id)
{
	if (mesh.model.has_value() == mesh.shape.has_value())
		Refuse("Entity mesh needs a model or a shape, not both: " + id);
	if (mesh.model)
	{
		CheckVirtualPath(*mesh.model, "a model", id);
		if (mesh.color)
			Refuse("Entity mesh color is for a shape; a model draws its own: " + id);
	}
	if (mesh.innerRadius)
	{
		if (mesh.shape != Shape::Ring)
			Refuse("Entity mesh innerRadius is for a ring: " + id);
		if (std::isnan(*mesh.innerRadius) || *mesh.innerRadius < 0.0f || *mesh.innerRadius >= 1.0f)
			Refuse("Entity mesh innerRadius is from 0 up to, not including, 1: " + id);
	}
	if (mesh.material)
		CheckVirtualPath(*mesh.material, "a material", id);
}

void CheckParticles(const ParticlesRecord &particles, const std::string &id)
{
	const auto share = [](float value) { return value >= 0.0f && value <= 1.0f; };
	const auto colour = [](const glm::vec4 &value)
	{ return value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f && value.w >= 0.0f; };
	if (!(particles.rate >= 0.0f) || !std::isfinite(particles.rate) || !(particles.lifetime > 0.0f) ||
	    !std::isfinite(particles.lifetime) || !(particles.speed >= 0.0f) ||
	    !std::isfinite(particles.speed) || !(particles.radius >= 0.0f) || !(particles.drag >= 0.0f) ||
	    !(particles.stretch >= 0.0f) || !(particles.size.x >= 0.0f) || !(particles.size.y >= 0.0f))
		Refuse(
		    "Particles' lifetime must be positive and finite, and their rate, speed, radius, drag, "
		    "stretch and sizes not negative: " +
		    id);
	if (!share(particles.lifetimeJitter) || !share(particles.speedJitter) || !share(particles.opacity))
		Refuse("Particles' lifetimeJitter, speedJitter and opacity are from 0 to 1: " + id);
	constexpr float kHalfTurn = 3.14159265f;
	if (!(particles.spread >= 0.0f) || !(particles.spread <= kHalfTurn))
		Refuse("Particles' spread is a half-angle, from 0 to pi radians: " + id);
	if (!colour(particles.color) || !colour(particles.colorEnd) ||
	    !colour(glm::vec4{particles.albedo, 0.0f}))
		Refuse("Particles' colours, intensities and albedo cannot be negative: " + id);
	if (particles.capacity == 0 || particles.capacity > kMaxEmitterParticles)
		Refuse(
		    std::format(
		        "Particles' capacity is from 1 to {}, the most an emitter keeps alive: {}",
		        kMaxEmitterParticles, id));
}

// Every rule a scene file is held to that its types do not already say, in one place: the loader
// runs it on what it read and the writer on what it is about to write, so neither accepts what the
// other refuses. The scene's loader and writer once each enforced a rule the other did not know.
void Validate(const SceneDocument &document)
{
	if (document.system)
		CheckVirtualPath(*document.system, "a system", "(the scene)");

	std::unordered_map<std::string, std::size_t> ids;
	for (std::size_t index = 0; index < document.entities.size(); ++index)
	{
		const std::string &id = document.entities[index].id;
		if (id.empty() || !ids.emplace(id, index).second)
			Refuse("Scene entity ids must be non-empty and unique: " + id);
	}
	const auto exists = [&ids](const std::string &other, const std::string &self)
	{ return other != self && ids.contains(other); };

	std::size_t activeCameras = 0;
	std::size_t suns = 0;
	std::size_t skies = 0;
	for (const EntityRecord &entity : document.entities)
	{
		const std::string &id = entity.id;
		if (entity.parent && !exists(*entity.parent, id))
			Refuse("Invalid parent '" + *entity.parent + "' in scene");
		if (const auto &camera = entity.camera)
		{
			if (camera->fov <= 0.0f || camera->fov >= 180.0f || camera->nearPlane <= 0.0f)
				Refuse("Invalid camera projection settings: " + id);
			if (camera->active && ++activeCameras > 1)
				Refuse("A scene can have only one active camera");
			if (const auto &orbit = camera->orbit)
			{
				if (!exists(orbit->target, id))
					Refuse("Invalid camera orbit target '" + orbit->target + "' in scene");
				if (!(orbit->minDistance > 0.0) || !(orbit->distance >= orbit->minDistance))
					Refuse(
					    "Camera orbit distances must be positive, and the distance at least the "
					    "minimum: " +
					    id);
			}
		}
		if (entity.light)
			CheckLight(*entity.light, id);
		if (entity.outline)
			CheckOutline(*entity.outline, id);
		if (entity.mask && entity.mask->game > 31)
			Refuse(
			    std::format(
			        "Entity '{}' has the game's stencil bits as {}; there are five, 0 to 31", id,
			        entity.mask->game));
		if (entity.sun)
		{
			if (entity.sun->intensity < 0.0f)
				Refuse("Sun intensity cannot be negative: " + id);
			if (++suns > 1)
				Refuse("A scene can have only one sun");
		}
		if (entity.sky)
		{
			CheckSky(*entity.sky, id);
			if (++skies > 1)
				Refuse("A scene can have only one sky");
		}
		if (entity.atmosphere)
		{
			CheckAtmosphere(*entity.atmosphere, id);
			if (!entity.mesh || !entity.mesh->shape)
				Refuse("An atmosphere is over a shape's surface, and needs a mesh of one: " + id);
		}
		if (entity.rings)
		{
			CheckRings(*entity.rings, id);
			if (!entity.mesh || !entity.mesh->shape)
				Refuse("Rings are about a shape's centre, and need a mesh of one: " + id);
		}
		if (entity.celestial)
		{
			if (entity.celestial->body.empty())
				Refuse("Entity celestial needs a body name: " + id);
			if (!document.system)
				Refuse("Entity '" + id + "' names a celestial body, but the scene names no system");
		}
		if (entity.mesh)
			CheckMesh(*entity.mesh, id);
		if (const auto &particles = entity.particles)
		{
			CheckParticles(*particles, id);
			if (particles->frame && !exists(*particles->frame, id))
				Refuse("Invalid particles frame '" + *particles->frame + "' in scene");
		}
		// A mesh is not part of what a rotation animation runs on: the update system walks the
		// transform, so an entity with nothing to draw can still spin. The clip player is the one
		// that needs a model, for a rig to sample.
		if (entity.transformAnimation && glm::length(entity.transformAnimation->axis) == 0.0f)
			Refuse("Animation rotation axis cannot be zero: " + id);
		if (entity.animation)
		{
			if (!entity.mesh || !entity.mesh->model)
				Refuse("Entity animation needs a model to play its clip on: " + id);
			if (entity.animation->clip.empty())
				Refuse("Animation clip name cannot be empty: " + id);
		}
		if (const auto &clip = entity.clip)
		{
			CheckVirtualPath(clip->file, "a clip file", id);
			if (clip->name.empty())
				Refuse("Entity '" + id + "' plays a clip of no name");
			if (!std::isfinite(clip->speed))
				Refuse("Entity '" + id + "' plays its clip at a speed that is not finite");
			// A node bound to the player's own entity is a rig of one.
			for (const ClipNodeRecord &node : clip->nodes)
				if (node.node.empty() || !ids.contains(node.entity))
					Refuse(
					    std::format(
					        "Entity '{}' binds clip node '{}' to '{}', which is not an entity of the "
					        "scene",
					        id, node.node, node.entity));
		}
	}
}

// ---- Between the file and the registry ------------------------------------------------------

LightComponent ToComponent(const LightRecord &record)
{
	LightComponent light;
	light.type = record.type;
	light.color = record.color;
	light.intensity = record.intensity;
	light.range = *record.range;
	light.innerConeAngle = record.innerConeAngle.value_or(light.innerConeAngle);
	light.outerConeAngle = record.outerConeAngle.value_or(light.outerConeAngle);
	light.active = record.active;
	light.animation = record.animation;
	return light;
}

LightRecord ToRecord(const LightComponent &light)
{
	LightRecord record;
	record.type = light.type;
	record.color = light.color;
	record.intensity = light.intensity;
	record.range = light.range;
	if (light.type == LightType::Spot)
	{
		record.innerConeAngle = light.innerConeAngle;
		record.outerConeAngle = light.outerConeAngle;
	}
	record.active = light.active;
	record.animation = light.animation;
	return record;
}

SkyComponent ToComponent(const SkyRecord &record)
{
	return {record.seed,       record.stars,      record.starFlux,     record.band.normal,
	        record.band.width, record.band.color, record.nebula.color, record.nebula.scale};
}

SkyRecord ToRecord(const SkyComponent &sky)
{
	return {
	    sky.seed,
	    sky.stars,
	    sky.starFlux,
	    {sky.bandNormal, sky.bandWidth, sky.bandColor},
	    {sky.nebulaColor, sky.nebulaScale}};
}

MeshComponent ToComponent(const MeshRecord &record)
{
	MeshComponent mesh;
	mesh.model = record.model.value_or("");
	mesh.shape = record.shape;
	mesh.color = record.color.value_or(mesh.color);
	mesh.innerRadius = record.innerRadius.value_or(mesh.innerRadius);
	mesh.material = record.material.value_or("");
	return mesh;
}

MeshRecord ToRecord(const MeshComponent &mesh)
{
	MeshRecord record;
	if (mesh.shape)
	{
		record.shape = mesh.shape;
		record.color = mesh.color;
		if (*mesh.shape == Shape::Ring)
			record.innerRadius = mesh.innerRadius;
	}
	else
		record.model = mesh.model;
	if (!mesh.material.empty())
		record.material = mesh.material;
	return record;
}

ParticleEmitterComponent ToComponent(const ParticlesRecord &record)
{
	ParticleEmitterComponent emitter;
	emitter.rate = record.rate;
	emitter.burst = record.burst;
	emitter.lifetime = record.lifetime;
	emitter.lifetimeJitter = record.lifetimeJitter;
	emitter.speed = record.speed;
	emitter.speedJitter = record.speedJitter;
	emitter.spread = record.spread;
	emitter.radius = record.radius;
	emitter.drag = record.drag;
	emitter.size = record.size;
	emitter.stretch = record.stretch;
	emitter.color = record.color;
	emitter.colorEnd = record.colorEnd;
	emitter.opacity = record.opacity;
	emitter.albedo = record.albedo;
	emitter.capacity = record.capacity;
	emitter.seed = record.seed;
	emitter.active = record.active;
	emitter.frameId = record.frame.value_or("");
	return emitter;
}

ParticlesRecord ToRecord(const ParticleEmitterComponent &emitter)
{
	ParticlesRecord record;
	record.rate = emitter.rate;
	record.burst = emitter.burst;
	record.lifetime = emitter.lifetime;
	record.lifetimeJitter = emitter.lifetimeJitter;
	record.speed = emitter.speed;
	record.speedJitter = emitter.speedJitter;
	record.spread = emitter.spread;
	record.radius = emitter.radius;
	record.drag = emitter.drag;
	record.size = emitter.size;
	record.stretch = emitter.stretch;
	record.color = emitter.color;
	record.colorEnd = emitter.colorEnd;
	record.opacity = emitter.opacity;
	record.albedo = emitter.albedo;
	record.capacity = emitter.capacity;
	record.seed = emitter.seed;
	record.active = emitter.active;
	if (!emitter.frameId.empty())
		record.frame = emitter.frameId;
	return record;
}

std::string ReadFile(const std::filesystem::path &path)
{
	std::ifstream file{path};
	if (!file)
		throw std::runtime_error("Failed to open scene file: " + path.string());
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}
} // namespace

struct SceneFile::Document
{
	SceneDocument document;
};

SceneFile::SceneFile(std::unique_ptr<Document> pDocument) : m_pDocument{std::move(pDocument)}
{
}

SceneFile::SceneFile(SceneFile &&) noexcept = default;
SceneFile &SceneFile::operator=(SceneFile &&) noexcept = default;
SceneFile::~SceneFile() = default;

SceneFile SceneFile::Read(std::string_view text)
{
	RegisterTypes();
	auto pDocument = std::make_unique<Document>();
	pDocument->document = Serialise::YamlArchive::Read<SceneDocument>(
	    text, Serialise::FormatVersion{Scene::kFormatVersion});
	Validate(pDocument->document);
	return SceneFile{std::move(pDocument)};
}

const std::string &SceneFile::StarSystem() const
{
	static const std::string kNone;
	return m_pDocument->document.system ? *m_pDocument->document.system : kNone;
}

std::vector<std::string> SceneFile::CelestialBodies() const
{
	std::vector<std::string> bodies;
	for (const EntityRecord &entity : m_pDocument->document.entities)
		if (entity.celestial)
			bodies.push_back(entity.celestial->body);
	return bodies;
}

Scene::~Scene()
{
	Clear();
}

void Scene::Load(const std::filesystem::path &path)
{
	try
	{
		LoadText(ReadFile(path));
	}
	catch (const std::exception &error)
	{
		throw std::runtime_error(std::format("Scene '{}': {}", path.string(), error.what()));
	}
}

void Scene::LoadText(std::string_view text)
{
	std::optional<SceneFile> file;
	try
	{
		file.emplace(SceneFile::Read(text));
	}
	catch (...)
	{
		Clear();
		throw;
	}
	Load(*file);
}

void Scene::Load(const SceneFile &file)
{
	Clear();
	const SceneDocument &document = file.m_pDocument->document;

	try
	{
		m_starSystem = document.system.value_or("");
		std::unordered_map<std::string, entt::entity> entities;
		for (const EntityRecord &record : document.entities)
		{
			const entt::entity entity = m_registry.create();
			entities.emplace(record.id, entity);
			m_registry.emplace<SceneEntityId>(entity, record.id);
			m_registry.emplace<TransformComponent>(entity, record.transform);
			if (record.layer)
				m_registry.emplace<LayerComponent>(entity, *record.layer);
			if (const auto &camera = record.camera)
			{
				m_registry.emplace<CameraComponent>(
				    entity, camera->position, camera->rotation, camera->fov, camera->nearPlane,
				    camera->active);
				if (const auto &orbit = camera->orbit)
				{
					OrbitCameraComponent component;
					component.targetId = orbit->target;
					component.distance = orbit->distance;
					component.yaw = orbit->yaw;
					component.pitch = orbit->pitch;
					component.minDistance = orbit->minDistance;
					m_registry.emplace<OrbitCameraComponent>(entity, std::move(component));
				}
			}
			if (record.light)
				m_registry.emplace<LightComponent>(entity, ToComponent(*record.light));
			if (record.outline)
				m_registry.emplace<OutlineComponent>(entity, *record.outline);
			if (const auto &mask = record.mask)
				m_registry.emplace<MaskComponent>(
				    entity, mask->cockpit, mask->unprocessed, static_cast<std::uint8_t>(mask->game));
			if (record.sun)
				m_registry.emplace<SunComponent>(entity, *record.sun);
			if (record.sky)
				m_registry.emplace<SkyComponent>(entity, ToComponent(*record.sky));
			if (record.celestial)
				m_registry.emplace<CelestialBodyComponent>(entity, *record.celestial);
			if (record.atmosphere)
				m_registry.emplace<AtmosphereComponent>(entity, *record.atmosphere);
			if (record.rings)
				m_registry.emplace<RingsComponent>(entity, *record.rings);
			if (record.mesh)
			{
				MeshComponent mesh = ToComponent(*record.mesh);
				if (!mesh.model.empty())
				{
					if (m_pResources == nullptr)
						throw std::runtime_error(
						    "Entity '" + record.id +
						    "' draws a model, whose rig loads through a resource manager this scene "
						    "was not given");
					m_registry.emplace<RigComponent>(
					    entity, m_pResources->Load<Animation::Rig>(mesh.model));
				}
				m_registry.emplace<MeshComponent>(entity, std::move(mesh));
			}
			if (record.particles)
				m_registry.emplace<ParticleEmitterComponent>(entity, ToComponent(*record.particles));
			if (const auto &animation = record.transformAnimation)
			{
				RotationAnimationComponent rotation;
				rotation.rotationAxis = animation->axis;
				rotation.rotationSpeed = animation->speed;
				m_registry.emplace<RotationAnimationComponent>(entity, rotation);
			}
			if (const auto &animation = record.animation)
			{
				AnimationPlayerComponent player;
				player.clipName = animation->clip;
				player.playbackSpeed = animation->speed;
				player.looping = animation->loop;
				player.playing = animation->playing;
				m_registry.emplace<AnimationPlayerComponent>(entity, std::move(player));
			}
		}

		// Validate has checked that every parent and target exists.
		for (const EntityRecord &record : document.entities)
		{
			const entt::entity entity = entities.at(record.id);
			if (const auto &clip = record.clip)
			{
				ClipPlayerComponent player;
				player.file = clip->file;
				player.clip = clip->name;
				player.speed = clip->speed;
				player.looping = clip->loop;
				player.playing = clip->playing;
				for (const ClipNodeRecord &node : clip->nodes)
					player.nodes.push_back({node.node, entities.at(node.entity)});
				m_registry.emplace<ClipPlayerComponent>(entity, std::move(player));
			}
			if (record.parent)
				m_registry.emplace<ParentComponent>(entity, entities.at(*record.parent));
			if (auto *orbit = m_registry.try_get<OrbitCameraComponent>(entity))
				orbit->target = entities.at(orbit->targetId);
			if (auto *emitter = m_registry.try_get<ParticleEmitterComponent>(entity);
			    emitter != nullptr && !emitter->frameId.empty())
				emitter->frame = entities.at(emitter->frameId);
		}
	}
	catch (...)
	{
		Clear();
		throw;
	}
}

std::string Scene::SaveText() const
{
	RegisterTypes();
	std::vector<entt::entity> entities;
	// What the system scene built is the system's, made again when the file is loaded.
	const auto view =
	    m_registry.view<const SceneEntityId, const TransformComponent>(entt::exclude<BuiltComponent>);
	entities.assign(view.begin(), view.end());
	std::ranges::sort(
	    entities, {}, [this](entt::entity entity) -> const std::string &
	    { return m_registry.get<SceneEntityId>(entity).value; });

	SceneDocument document;
	if (!m_starSystem.empty())
		document.system = m_starSystem;
	for (const entt::entity entity : entities)
	{
		EntityRecord record;
		record.id = m_registry.get<SceneEntityId>(entity).value;
		if (const auto *parent = m_registry.try_get<ParentComponent>(entity))
			record.parent = m_registry.get<SceneEntityId>(parent->entity).value;
		if (const auto *layer = m_registry.try_get<LayerComponent>(entity))
			record.layer = layer->layer;
		record.transform = m_registry.get<TransformComponent>(entity);
		if (const auto *camera = m_registry.try_get<CameraComponent>(entity))
		{
			CameraRecord &out = record.camera.emplace();
			out.position = camera->position;
			out.rotation = camera->rotation;
			out.fov = camera->verticalFovDegrees;
			out.nearPlane = camera->nearPlane;
			out.active = camera->active;
			if (const auto *orbit = m_registry.try_get<OrbitCameraComponent>(entity))
				out.orbit = OrbitRecord{
				    orbit->targetId, orbit->distance, orbit->yaw, orbit->pitch, orbit->minDistance};
		}
		if (const auto *sun = m_registry.try_get<SunComponent>(entity))
			record.sun = *sun;
		if (const auto *sky = m_registry.try_get<SkyComponent>(entity))
			record.sky = ToRecord(*sky);
		if (const auto *celestial = m_registry.try_get<CelestialBodyComponent>(entity))
			record.celestial = *celestial;
		if (const auto *atmosphere = m_registry.try_get<AtmosphereComponent>(entity))
			record.atmosphere = *atmosphere;
		if (const auto *rings = m_registry.try_get<RingsComponent>(entity))
			record.rings = *rings;
		if (const auto *light = m_registry.try_get<LightComponent>(entity))
			record.light = ToRecord(*light);
		if (const auto *mesh = m_registry.try_get<MeshComponent>(entity))
			record.mesh = ToRecord(*mesh);
		if (const auto *outline = m_registry.try_get<OutlineComponent>(entity))
			record.outline = *outline;
		if (const auto *mask = m_registry.try_get<MaskComponent>(entity))
			record.mask = MaskRecord{mask->cockpit, mask->unprocessed, mask->game};
		if (const auto *emitter = m_registry.try_get<ParticleEmitterComponent>(entity))
			record.particles = ToRecord(*emitter);
		if (const auto *rotation = m_registry.try_get<RotationAnimationComponent>(entity))
			record.transformAnimation =
			    TransformAnimationRecord{rotation->rotationAxis, rotation->rotationSpeed};
		if (const auto *player = m_registry.try_get<AnimationPlayerComponent>(entity))
			record.animation = AnimationRecord{
			    player->clipName, player->playbackSpeed, player->looping, player->playing};
		if (const auto *player = m_registry.try_get<ClipPlayerComponent>(entity))
		{
			ClipRecord &clip = record.clip.emplace();
			clip.file = player->file;
			clip.name = player->clip;
			clip.speed = player->speed;
			clip.loop = player->looping;
			clip.playing = player->playing;
			// A binding to an entity the file does not hold, one the system scene built, is the
			// builder's, made again as the file is loaded.
			for (const ClipBinding &binding : player->nodes)
				if (const auto *bound = m_registry.valid(binding.entity)
				                            ? m_registry.try_get<SceneEntityId>(binding.entity)
				                            : nullptr;
				    bound != nullptr && !m_registry.all_of<BuiltComponent>(binding.entity))
					clip.nodes.push_back({binding.node, bound->value});
		}
		document.entities.push_back(std::move(record));
	}
	Validate(document);
	return Serialise::YamlArchive::Write(document, Serialise::FormatVersion{kFormatVersion});
}

void Scene::Save(const std::filesystem::path &path) const
{
	const std::string text = SaveText();
	const auto scenePath = std::filesystem::absolute(path).lexically_normal();
	std::filesystem::create_directories(scenePath.parent_path());
	std::ofstream output{scenePath};
	if (!output)
		throw std::runtime_error("Failed to open scene for writing: " + scenePath.string());
	output << text;
	if (!output)
		throw std::runtime_error("Failed to write scene: " + scenePath.string());
}

void Scene::LoadSystem(std::string_view starSystem)
{
	Clear();
	const std::string path{starSystem};
	CheckVirtualPath(path, "a system", "(the scene)");
	m_starSystem = path;
}

void Scene::Destroy(entt::entity entity)
{
	if (const auto *rig = m_registry.try_get<RigComponent>(entity); rig != nullptr && m_pResources)
		m_pResources->Release(rig->rig);
	m_registry.destroy(entity);
}

void Scene::SetLayerVisible(Layer layer, bool visible)
{
	auto &visibility = m_registry.ctx().contains<LayerVisibility>()
	                       ? m_registry.ctx().get<LayerVisibility>()
	                       : m_registry.ctx().emplace<LayerVisibility>();
	const auto bit = static_cast<std::uint8_t>(1u << static_cast<unsigned>(layer));
	visibility.visible =
	    static_cast<std::uint8_t>(visible ? visibility.visible | bit : visibility.visible & ~bit);
}

bool Scene::IsLayerVisible(Layer layer) const
{
	const auto *visibility = m_registry.ctx().find<LayerVisibility>();
	return visibility == nullptr || visibility->IsVisible(layer);
}

void Scene::Clear()
{
	++m_generation;
	if (m_pResources != nullptr)
		for (const auto [entity, rig] : m_registry.view<const RigComponent>().each())
			m_pResources->Release(rig.rig);
	m_registry.clear();
	m_starSystem.clear();
}
} // namespace Jettison::Scene
