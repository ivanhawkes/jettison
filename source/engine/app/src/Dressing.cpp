#include "Dressing.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include <jettison/core/Log.h>
#include <jettison/physics/Flight.h>
#include <jettison/render/Clusters.h>
#include <jettison/scene/Components.h>

namespace Jettison::App
{
namespace
{
constexpr Core::Category kDressing{"stations"};

constexpr std::string_view kPlating = "materials/hull.material.yaml";
constexpr std::string_view kDeckPlating = "materials/deck.material.yaml";
constexpr std::string_view kLamp = "materials/lamp.material.yaml";
// A reserved pad's lamps, which blink by a time-driven parameter.
constexpr std::string_view kBlinkingLamp = "materials/lamp-blinking.material.yaml";
constexpr std::string_view kEngine = "materials/engine.material.yaml";
constexpr std::string_view kGlass = "materials/glass.material.yaml";

constexpr glm::vec3 kDeckColour{0.55f, 0.56f, 0.6f};
constexpr glm::vec3 kPadColour{0.16f, 0.17f, 0.2f};
constexpr glm::vec3 kLineColour{0.9f, 0.72f, 0.18f};
constexpr glm::vec3 kNumberColour{0.92f, 0.92f, 0.9f};
constexpr glm::vec3 kKeepClearColour{0.8f, 0.16f, 0.1f};
constexpr glm::vec3 kPoleColour{0.36f, 0.37f, 0.4f};
constexpr glm::vec3 kDoorColour{0.12f, 0.13f, 0.15f};
constexpr glm::vec3 kDoorLampColour{1.0f, 0.62f, 0.2f};
constexpr glm::vec3 kFloodColour{0.92f, 0.95f, 1.0f};
constexpr glm::vec3 kClampColour{0.86f, 0.62f, 0.1f};
constexpr glm::vec3 kJawColour{0.07f, 0.07f, 0.08f};

// How far out from the hull's side a clamp's root stands, in its units: the jaw's face is 0.92 out
// once closed (animations/pad-clamp.clip.yaml), and its far corner, tilted with the arm, 0.936.
constexpr float kClampReach = 0.94f;

// How thick a pad's plate is drawn, half above the deck and half in it: nothing a hull resting on
// the deck sinks into far enough to see, and apart from the deck's face, so the two do not fight.
constexpr float kPadThickness = 0.05f;
// The paint on it, as thick again and resting on it.
constexpr float kPaintThickness = 0.04f;
constexpr float kPaintHeight = kPadThickness * 0.5f + kPaintThickness * 0.5f;

// What a floodlight puts on the hull it is aimed at, in the HDR target's units: half the sun's
// irradiance (orbit.yaml's 3.4) from each, with a pad's four to eight overlapping.
constexpr float kFloodIrradiance = 1.6f;
// The reach past the point it is aimed at, and its cone's half-angles.
constexpr float kFloodReach = 1.8f;
constexpr float kFloodInner = 0.35f;
constexpr float kFloodOuter = 0.62f;

// A bay's door, its sill on the deck: wide enough for two buggies to pass and as tall as a pier's
// deck is above Corva's hub floor allows.
constexpr float kDoorWidth = 20.0f;
constexpr float kDoorHeight = 10.0f;

// A road's lane divider, painted down its middle.
constexpr float kRoadLine = 0.3f;

// The scene turns an entity about X, then Y, then Z (scene/SceneSystems.cpp).
glm::vec3 SceneRotation(const glm::dquat &orientation)
{
	glm::vec3 rotation{0.0f};
	glm::extractEulerAngleXYZ(
	    glm::mat4{glm::mat4_cast(glm::quat{orientation})}, rotation.x, rotation.y, rotation.z);
	return rotation;
}

entt::entity Node(
    entt::registry &scene, entt::entity parent, const glm::dvec3 &at, const glm::vec3 &rotation)
{
	const entt::entity entity = scene.create();
	scene.emplace<Scene::BuiltComponent>(entity);
	if (parent != entt::null)
		scene.emplace<Scene::ParentComponent>(entity, parent);
	scene.emplace<Scene::TransformComponent>(entity, at, rotation, glm::vec3{1.0f});
	return entity;
}

struct Look
{
	Scene::Shape shape{Scene::Shape::Box};
	glm::vec3 colour{1.0f};
	std::string_view material;
	std::optional<Scene::Layer> layer;
};

// A procedural shape spans its scale either side of its middle.
entt::entity Part(
    entt::registry &scene, entt::entity parent, const glm::dvec3 &at, const glm::vec3 &rotation,
    const glm::vec3 &scale, const Look &look)
{
	const entt::entity entity = Node(scene, parent, at, rotation);
	scene.get<Scene::TransformComponent>(entity).scale = scale;
	Scene::MeshComponent mesh;
	mesh.shape = look.shape;
	mesh.color = look.colour;
	mesh.material = std::string{look.material};
	scene.emplace<Scene::MeshComponent>(entity, std::move(mesh));
	if (look.layer)
		scene.emplace<Scene::LayerComponent>(entity, *look.layer);
	return entity;
}

// A painted stroke on a pad, in the pad's axes: from its middle, its half-width and half-length,
// turned about the normal.
void Stroke(
    entt::registry &scene, entt::entity pad, glm::vec2 middle, float halfWidth, float halfLength,
    float turn, const glm::vec3 &colour)
{
	Part(
	    scene, pad, {middle.x, kPaintHeight, middle.y}, {0.0f, turn, 0.0f},
	    {halfWidth, kPaintThickness * 0.5f, halfLength},
	    {Scene::Shape::Box, colour, {}, Scene::Layer::Effects});
}

// A stroke between two points on the pad.
void Line(
    entt::registry &scene, entt::entity pad, glm::vec2 from, glm::vec2 to, float width,
    const glm::vec3 &colour)
{
	const glm::vec2 along = to - from;
	Stroke(
	    scene, pad, (from + to) * 0.5f, width * 0.5f, glm::length(along) * 0.5f + width * 0.5f,
	    std::atan2(along.x, along.y), colour);
}

// A rectangle's outline on the pad, about its middle.
void Outline(
    entt::registry &scene, entt::entity pad, glm::vec2 middle, glm::vec2 half, float width,
    const glm::vec3 &colour)
{
	for (const float side : {-1.0f, 1.0f})
	{
		Line(
		    scene, pad, middle + glm::vec2{side * half.x, -half.y},
		    middle + glm::vec2{side * half.x, half.y}, width, colour);
		Line(
		    scene, pad, middle + glm::vec2{-half.x, side * half.y},
		    middle + glm::vec2{half.x, side * half.y}, width, colour);
	}
}

// The seven segments a digit lights, a to g: top, upper right, lower right, bottom, lower left, upper
// left, middle.
constexpr std::array<std::uint8_t, 10> kDigitSegments{0b0111111, 0b0000110, 0b1011011, 0b1001111,
                                                      0b1100110, 0b1101101, 0b1111101, 0b0000111,
                                                      0b1111111, 0b1101111};

// A digit in bar segments, its middle on the pad, read with the bow (-Z) up and +X to the right, as
// a pilot over the pad sees it.
void Digit(
    entt::registry &scene, entt::entity pad, glm::vec2 middle, float height, float width,
    unsigned digit)
{
	// Up the digit is -Z on the pad.
	const float halfW = width * 0.5f;
	const float halfH = height * 0.5f;
	const float stroke = height * 0.12f;
	const std::array<std::pair<glm::vec2, glm::vec2>, 7> segments{{
	    {{-halfW, -halfH}, {halfW, -halfH}},
	    {{halfW, -halfH}, {halfW, 0.0f}},
	    {{halfW, 0.0f}, {halfW, halfH}},
	    {{-halfW, halfH}, {halfW, halfH}},
	    {{-halfW, 0.0f}, {-halfW, halfH}},
	    {{-halfW, -halfH}, {-halfW, 0.0f}},
	    {{-halfW, 0.0f}, {halfW, 0.0f}},
	}};
	for (std::size_t segment = 0; segment < segments.size(); ++segment)
		if ((kDigitSegments[digit] >> segment) & 1u)
		{
			// Each a stroke short of the corners, so the segments read as bars.
			const glm::vec2 from = segments[segment].first;
			const glm::vec2 to = segments[segment].second;
			const glm::vec2 inset = glm::normalize(to - from) * (stroke * 0.6f);
			Line(scene, pad, middle + from + inset, middle + to - inset, stroke, kNumberColour);
		}
}

// The paint on a pad, in its axes: x across, z along with the bow at -Z.
void Paint(entt::registry &scene, entt::entity pad, const Dock::PadLayout &layout, unsigned number)
{
	const glm::vec2 size = Dock::PadSize(layout.padClass);
	const glm::vec3 largest = Dock::LargestHull(layout.padClass);
	const float width = size.x * 0.012f;
	const glm::vec2 half = size * 0.5f - glm::vec2{width * 2.0f};
	Outline(scene, pad, {0.0f, 0.0f}, half, width, kLineColour);

	// The number nearest the bow, then the chevrons pointing to it.
	const float digitHeight = std::min(size.y * 0.12f, size.x * 0.3f);
	const float digitWidth = digitHeight * 0.55f;
	const float numberAt = -half.y + size.y * 0.04f + digitHeight * 0.5f;
	const std::string digits = std::to_string(number);
	const float pitch = digitWidth * 1.5f;
	for (std::size_t index = 0; index < digits.size(); ++index)
		Digit(
		    scene, pad,
		    {(static_cast<float>(index) - static_cast<float>(digits.size() - 1) * 0.5f) * pitch,
		     numberAt},
		    digitHeight, digitWidth, static_cast<unsigned>(digits[index] - '0'));

	// Short enough on an S pad, as long as it is wide, to leave the centre line room.
	const float arm = std::min(size.x * 0.15f, size.y * 0.08f);
	const float back = arm * 0.7071f;
	float apex = numberAt + digitHeight * 0.5f + size.y * 0.05f;
	for (int chevron = 0; chevron < 3; ++chevron)
	{
		for (const float side : {-1.0f, 1.0f})
			Line(scene, pad, {0.0f, apex}, {side * back, apex + back}, width, kLineColour);
		apex += back * 1.2f;
	}

	// The keep-clear box over where a hatch is, aft of the middle under the hull, with its diagonals.
	const glm::vec2 keepClear{largest.y * 0.2f, largest.x * 0.1f};
	const glm::vec2 hatchAt{0.0f, largest.x * 0.21f};
	Outline(scene, pad, hatchAt, keepClear, width, kKeepClearColour);
	Line(scene, pad, hatchAt - keepClear, hatchAt + keepClear, width, kKeepClearColour);
	Line(
	    scene, pad, hatchAt + glm::vec2{-keepClear.x, keepClear.y},
	    hatchAt + glm::vec2{keepClear.x, -keepClear.y}, width, kKeepClearColour);

	// The centre line from the chevrons to the keep-clear box.
	const glm::vec2 lineFrom{0.0f, apex + size.y * 0.02f};
	const glm::vec2 lineTo{0.0f, hatchAt.y - keepClear.y - size.y * 0.03f};
	if (lineTo.y > lineFrom.y)
		Line(scene, pad, lineFrom, lineTo, width, kLineColour);
}

// The floodlight poles along a pad's long sides, in its axes, and their lamps. Returns the lamps.
std::vector<entt::entity> Floodlights(
    entt::registry &scene, entt::entity pad, const Dock::PadLayout &layout, bool display,
    std::size_t &floodlights)
{
	const glm::vec2 size = Dock::PadSize(layout.padClass);
	const glm::vec3 largest = Dock::LargestHull(layout.padClass);
	const float out = size.x * 0.04f;
	const float radius = size.x * 0.008f;
	const float height = largest.z * 1.25f + 1.0f;
	const int perSide = static_cast<int>(std::ceil(size.y / kPoleSpacing)) + 1;
	const float halfAlong = size.y * 0.5f + out;
	std::vector<entt::entity> lamps;
	for (const float side : {-1.0f, 1.0f})
		for (int pole = 0; pole < perSide; ++pole)
		{
			const float along = -halfAlong + 2.0f * halfAlong * static_cast<float>(pole) /
			                                     static_cast<float>(perSide - 1);
			const glm::vec3 foot{side * (size.x * 0.5f + out), 0.0f, along};
			Part(
			    scene, pad, glm::dvec3{foot + glm::vec3{0.0f, height * 0.5f, 0.0f}}, {},
			    {radius, height * 0.5f, radius}, {Scene::Shape::Cylinder, kPoleColour, {}, {}});

			// The head, leaning over the pad and aimed across it at half the hull's height, a little
			// towards the pad's middle along it.
			const glm::vec3 head = foot + glm::vec3{-side * radius * 2.0f, height, 0.0f};
			const glm::vec3 aim{0.0f, largest.z * 0.5f, along * 0.6f};
			const glm::vec3 towards = aim - head;
			const float distance = glm::length(towards);
			const glm::dquat facing{glm::quatLookAt(towards / distance, glm::vec3{0.0f, 1.0f, 0.0f})};
			Part(
			    scene, pad, glm::dvec3{head}, SceneRotation(facing),
			    {radius * 2.5f, radius * 1.6f, radius * 1.2f},
			    {Scene::Shape::Box, kPoleColour, {}, {}});

			Scene::LightComponent light;
			light.type = Scene::LightType::Spot;
			light.color = kFloodColour;
			light.range = distance * kFloodReach;
			light.intensity =
			    kFloodIrradiance / Render::RangeAttenuation(distance * distance, light.range);
			light.innerConeAngle = kFloodInner;
			light.outerConeAngle = kFloodOuter;
			// Just in front of the head, so its own box does not shadow it.
			const entt::entity flood = Node(
			    scene, pad, glm::dvec3{head + towards / distance * (radius * 1.6f)},
			    SceneRotation(facing));
			scene.emplace<Scene::LightComponent>(flood, light);
			++floodlights;

			// The lamp under the head, on the pole's pad side.
			lamps.push_back(Part(
			    scene, pad,
			    glm::dvec3{foot + glm::vec3{-side * radius * 1.4f, height - radius * 4.0f, 0.0f}}, {},
			    glm::vec3{radius * 1.8f},
			    {Scene::Shape::IcoSphere, LampColour(Dock::PadState::Free, display), kLamp,
			     Scene::Layer::Effects}));
		}
	return lamps;
}

// A bay's door, from its sill on the deck: in a wall facing along the deck, or a hatch in it.
void Door(entt::registry &scene, entt::entity station, const Dock::Bay &bay)
{
	const glm::dvec3 normal = glm::normalize(bay.normal);
	const bool hatch = std::abs(normal.y) > 0.7;
	// The door's axes: x along its sill, y up it (or for a hatch, along the deck), z out of it.
	const glm::dvec3 up = hatch ? glm::dvec3{0.0, 0.0, -1.0}
	                            : glm::normalize(glm::dvec3{0.0, 1.0, 0.0} - normal * normal.y);
	const glm::dvec3 across = glm::cross(up, normal);
	const glm::dquat facing = glm::quat_cast(glm::dmat3{across, up, normal});
	const entt::entity door = Node(scene, station, bay.position, SceneRotation(facing));

	const float frame = 0.6f;
	const Look panel{Scene::Shape::Box, kDoorColour, {}, {}};
	const Look trim{Scene::Shape::Box, kLineColour, {}, {}};
	const Look strip{Scene::Shape::Box, kDoorLampColour, kLamp, Scene::Layer::Effects};
	const float halfW = kDoorWidth * 0.5f;
	if (hatch)
	{
		// Flat on the deck, square, proud of it by its frame's thickness.
		Part(scene, door, {0.0, 0.0, 0.1}, {}, {halfW, halfW, 0.1f}, panel);
		for (const float side : {-1.0f, 1.0f})
		{
			Part(
			    scene, door, {side * (halfW + frame * 0.5f), 0.0, 0.15}, {},
			    {frame * 0.5f, halfW + frame, 0.15f}, trim);
			Part(
			    scene, door, {0.0, side * (halfW + frame * 0.5f), 0.15}, {},
			    {halfW, frame * 0.5f, 0.15f}, trim);
		}
		Part(scene, door, {0.0, halfW + frame * 2.0f, 0.15}, {}, {halfW * 0.6f, 0.3f, 0.15f}, strip);
		return;
	}
	const float halfH = kDoorHeight * 0.5f;
	Part(scene, door, {0.0, halfH, 0.15}, {}, {halfW, halfH, 0.15f}, panel);
	for (const float side : {-1.0f, 1.0f})
		Part(
		    scene, door, {side * (halfW + frame * 0.5f), halfH + frame * 0.5f, 0.25}, {},
		    {frame * 0.5f, halfH + frame * 0.5f, 0.25f}, trim);
	Part(
	    scene, door, {0.0, kDoorHeight + frame * 0.5f, 0.25}, {}, {halfW + frame, frame * 0.5f, 0.25f},
	    trim);
	Part(scene, door, {0.0, kDoorHeight + frame * 2.5f, 0.25}, {}, {halfW * 0.6f, 0.3f, 0.2f}, strip);
}

// The share of its thrust below which a port is taken as not firing.
constexpr float kIdleThrottle = 0.02f;
// A nozzle's glow at rest, as a share of its full glow firing, and the seconds over which its glow
// rises towards firing and falls back: a time constant each, so it is two thirds of the way after
// that long. Slow, as hot metal is; a jet's pulse of a few steps barely warms it.
constexpr float kIdleGlow = 0.03f;
constexpr double kGlowRise = 1.0;
constexpr double kGlowFall = 2.5;
// A manoeuvring jet at full: its births a second and its brightness at birth.
constexpr float kJetRate = 120.0f;
constexpr float kJetIntensity = 0.25f;
// A plume at full ahead: its births a second and its brightness at birth, in the HDR target's units,
// the core of a hundred-odd overlapping white, its edges the engine's blue.
constexpr float kPlumeRate = 220.0f;
constexpr float kPlumeIntensity = 0.5f;

// An engine's plume, from the nozzle of the radius given, aft along the hull's +Z: the emitter's -Z
// turned half about Y. Its particles leave at 40 m/s and are slowed to rest in the frame ShowEngines
// gives them, the station's, so a ship moving through it draws its plume out behind it.
entt::entity Plume(entt::registry &scene, entt::entity root, const glm::dvec3 &nozzle, float radius)
{
	const entt::entity plume = Node(scene, root, nozzle, {0.0f, 3.1415927f, 0.0f});
	scene.emplace<Scene::LayerComponent>(plume, Scene::Layer::Effects);
	Scene::ParticleEmitterComponent emitter;
	emitter.active = false;
	emitter.rate = 0.0f;
	emitter.lifetime = 0.35f;
	emitter.lifetimeJitter = 0.4f;
	emitter.speed = 40.0f;
	emitter.speedJitter = 0.3f;
	emitter.spread = 0.06f;
	emitter.radius = radius * 0.5f;
	emitter.drag = 3.0f;
	emitter.size = {radius * 1.6f, radius * 4.0f};
	emitter.stretch = 0.01f;
	emitter.color = {0.55f, 0.75f, 1.0f, kPlumeIntensity};
	emitter.colorEnd = {0.25f, 0.35f, 1.0f, 0.0f};
	emitter.capacity = 128;
	emitter.seed = static_cast<std::uint32_t>(entt::to_integral(plume)) + 1u;
	scene.emplace<Scene::ParticleEmitterComponent>(plume, emitter);
	return plume;
}

// A manoeuvring jet's puff, from a nozzle of the radius given, along the exhaust (in the hull's
// axes): cold gas, short and faint beside an engine's plume, at rest in the frame ShowThrust gives it.
entt::entity Jet(
    entt::registry &scene, entt::entity root, const glm::dvec3 &nozzle, const glm::vec3 &exhaust,
    float radius)
{
	// The emitter's -Z along the exhaust.
	const glm::dvec3 way{exhaust};
	const glm::dvec3 up = std::abs(way.y) > 0.9 ? glm::dvec3{1.0, 0.0, 0.0} : glm::dvec3{0.0, 1.0, 0.0};
	const entt::entity jet = Node(scene, root, nozzle, SceneRotation(glm::quatLookAtRH(way, up)));
	scene.emplace<Scene::LayerComponent>(jet, Scene::Layer::Effects);
	Scene::ParticleEmitterComponent emitter;
	emitter.active = false;
	emitter.rate = 0.0f;
	emitter.lifetime = 0.25f;
	emitter.lifetimeJitter = 0.4f;
	emitter.speed = 18.0f;
	emitter.speedJitter = 0.3f;
	emitter.spread = 0.2f;
	emitter.radius = radius * 0.5f;
	emitter.drag = 4.0f;
	emitter.size = {radius * 1.2f, radius * 5.0f};
	emitter.color = {0.9f, 0.93f, 1.0f, kJetIntensity};
	emitter.colorEnd = {0.7f, 0.75f, 0.85f, 0.0f};
	emitter.capacity = 48;
	emitter.seed = static_cast<std::uint32_t>(entt::to_integral(jet)) + 1u;
	scene.emplace<Scene::ParticleEmitterComponent>(jet, emitter);
	return jet;
}

// A proxy's part in the hull's own axes, as fractions of its half-length, half-beam and half-height
// (so -1 to 1 within its box): its middle, its half-extents along the hull's axes, and whether it is
// a cylinder turned onto the hull's length, whose own axis is its Y.
struct ProxyPart
{
	glm::vec3 at;
	glm::vec3 half;
	Scene::Shape shape;
	glm::vec3 colour;
	std::string_view material;
	bool turned{false};
};

constexpr glm::vec3 kHull{0.7f, 0.7f, 0.74f};
constexpr glm::vec3 kGrey{0.55f, 0.56f, 0.6f};
constexpr glm::vec3 kDark{0.3f, 0.3f, 0.32f};
constexpr glm::vec3 kCanopy{0.08f, 0.1f, 0.16f};
constexpr glm::vec3 kWhite{1.0f};
constexpr glm::vec3 kRust{0.62f, 0.3f, 0.14f};
constexpr glm::vec3 kSlate{0.2f, 0.32f, 0.45f};
constexpr auto kBox = Scene::Shape::Box;
constexpr auto kIco = Scene::Shape::IcoSphere;
constexpr auto kCylinder = Scene::Shape::Cylinder;

// The shell's shuttle, 9.8 x 6.4 x 1.8 m, from which this was measured: a hull, a canopy over its
// fore end, wings aft, and two engines whose nozzles glow.
std::vector<ProxyPart> SmallProxy()
{
	std::vector<ProxyPart> parts{
	    {{0.0f, 0.0f, 0.0f}, {0.375f, 0.611f, 0.898f}, kBox, kHull, kPlating},
	    {{0.0f, 0.5f, -0.388f}, {0.234f, 0.5f, 0.265f}, kIco, kCanopy, kGlass},
	    {{0.0f, -0.111f, 0.327f}, {1.0f, 0.133f, 0.367f}, kBox, kGrey, kPlating},
	};
	for (const float side : {-1.0f, 1.0f})
	{
		parts.push_back(
		    {{side * 0.234f, 0.0f, 0.857f}, {0.131f, 0.467f, 0.122f}, kCylinder, kDark, {}, true});
		parts.push_back(
		    {{side * 0.234f, 0.0f, 0.984f}, {0.106f, 0.378f, 0.012f}, kIco, kWhite, kEngine});
	}
	return parts;
}

// A transport: a cab with its canopy at the bow, a spine, four cargo pods along it and two engines.
std::vector<ProxyPart> MediumProxy()
{
	std::vector<ProxyPart> parts{
	    {{0.0f, 0.1f, -0.78f}, {0.4f, 0.6f, 0.2f}, kBox, kHull, kPlating},
	    {{0.0f, 0.3f, -0.95f}, {0.3f, 0.25f, 0.05f}, kIco, kCanopy, kGlass},
	    {{0.0f, 0.0f, 0.1f}, {0.18f, 0.3f, 0.8f}, kBox, kGrey, kPlating},
	};
	for (const float side : {-1.0f, 1.0f})
	{
		for (const float along : {-0.3f, 0.3f})
			parts.push_back(
			    {{side * 0.58f, 0.0f, along},
			     {0.4f, 0.8f, 0.26f},
			     kBox,
			     along < 0.0f ? kRust : kSlate,
			     {}});
		parts.push_back(
		    {{side * 0.3f, 0.0f, 0.92f}, {0.18f, 0.36f, 0.08f}, kCylinder, kDark, {}, true});
		parts.push_back({{side * 0.3f, 0.0f, 0.99f}, {0.14f, 0.28f, 0.01f}, kIco, kWhite, kEngine});
	}
	return parts;
}

// The largest transport: a spine under a bridge at the bow, two tiers of six containers either side
// of it, and an engine block of three.
std::vector<ProxyPart> LargeProxy()
{
	std::vector<ProxyPart> parts{
	    {{0.0f, 0.0f, 0.0f}, {0.14f, 0.22f, 0.94f}, kBox, kGrey, kPlating},
	    {{0.0f, 0.35f, -0.84f}, {0.36f, 0.6f, 0.1f}, kBox, kHull, kPlating},
	    {{0.0f, 0.0f, 0.86f}, {0.5f, 0.45f, 0.08f}, kBox, kHull, kPlating},
	};
	for (int slot = 0; slot < 6; ++slot)
		for (const float side : {-1.0f, 1.0f})
			for (const float tier : {-0.45f, 0.45f})
				parts.push_back(
				    {{side * 0.54f, tier, -0.62f + 0.248f * static_cast<float>(slot)},
				     {0.4f, 0.42f, 0.11f},
				     kBox,
				     (slot + (tier > 0.0f ? 1 : 0)) % 2 == 0 ? kRust : kSlate,
				     {}});
	for (const float across : {-0.32f, 0.0f, 0.32f})
	{
		parts.push_back({{across, 0.0f, 0.97f}, {0.13f, 0.36f, 0.03f}, kCylinder, kDark, {}, true});
		parts.push_back({{across, 0.0f, 0.995f}, {0.1f, 0.28f, 0.005f}, kIco, kWhite, kEngine});
	}
	return parts;
}

// A liner or a frigate: a long round hull with a rounded bow, a superstructure along its top, fins
// aft and three engines.
std::vector<ProxyPart> ExtraLargeProxy()
{
	std::vector<ProxyPart> parts{
	    {{0.0f, -0.05f, 0.05f}, {0.55f, 0.6f, 0.8f}, kCylinder, kHull, kPlating, true},
	    {{0.0f, -0.05f, -0.75f}, {0.55f, 0.6f, 0.24f}, kIco, kHull, kPlating},
	    {{0.0f, 0.62f, -0.05f}, {0.32f, 0.36f, 0.5f}, kBox, kHull, kPlating},
	    {{0.0f, -0.05f, 0.62f}, {1.0f, 0.05f, 0.14f}, kBox, kGrey, kPlating},
	    {{0.0f, 0.7f, 0.72f}, {0.04f, 0.28f, 0.12f}, kBox, kGrey, kPlating},
	};
	for (const float across : {-0.3f, 0.0f, 0.3f})
	{
		parts.push_back({{across, -0.05f, 0.9f}, {0.15f, 0.2f, 0.06f}, kCylinder, kDark, {}, true});
		parts.push_back({{across, -0.05f, 0.97f}, {0.12f, 0.16f, 0.01f}, kIco, kWhite, kEngine});
	}
	return parts;
}

std::vector<ProxyPart> ProxyOf(Dock::PadClass padClass)
{
	switch (padClass)
	{
		case Dock::PadClass::S: return SmallProxy();
		case Dock::PadClass::M: return MediumProxy();
		case Dock::PadClass::L: return LargeProxy();
		case Dock::PadClass::XL: return ExtraLargeProxy();
	}
	return {};
}

// One clamp of a pad's, stowed as the clip leaves it: put away at no size, its post flat in the deck
// and its arm upright, hinge 0.8 down, in the units FitClamps scales the root by; the parts' names
// are the clip's.
struct ClampParts
{
	entt::entity root;
	entt::entity clamp;
	entt::entity post;
	entt::entity arm;
	entt::entity jaw;
};

ClampParts Clamp(entt::registry &scene, entt::entity pad)
{
	ClampParts parts{};
	parts.root = Node(scene, pad, glm::dvec3{0.0}, {});
	parts.clamp = Node(scene, parts.root, glm::dvec3{0.0}, {});
	scene.get<Scene::TransformComponent>(parts.clamp).scale = glm::vec3{0.0f};
	parts.post = Node(scene, parts.clamp, glm::dvec3{0.0}, {});
	scene.get<Scene::TransformComponent>(parts.post).scale = {1.0f, 0.001f, 1.0f};
	Part(
	    scene, parts.post, {0.0, 0.5, 0.0}, {}, {0.14f, 0.5f, 0.22f},
	    {Scene::Shape::Box, kPoleColour, kPlating, {}});
	parts.arm = Node(scene, parts.clamp, {0.0, -0.8, 0.0}, {});
	Part(
	    scene, parts.arm, {0.0, 0.35, 0.0}, {}, {0.07f, 0.35f, 0.12f},
	    {Scene::Shape::Box, kClampColour, {}, {}});
	// The hinge's pin, across the arm: a cylinder's own Y turned a quarter about X onto Z.
	Part(
	    scene, parts.arm, glm::dvec3{0.0}, {glm::half_pi<float>(), 0.0f, 0.0f}, {0.1f, 0.14f, 0.1f},
	    {Scene::Shape::Cylinder, kPoleColour, {}, {}});
	parts.jaw = Node(scene, parts.arm, {0.0, 0.7, 0.0}, {});
	Part(
	    scene, parts.jaw, glm::dvec3{0.0}, {}, {0.12f, 0.08f, 0.3f},
	    {Scene::Shape::Box, kJawColour, {}, {}});
	return parts;
}

// A pad's four clamps, stowed, and the player that moves them, which shows them stowed, at the end
// of the open clip.
PadClamps Clamps(entt::registry &scene, entt::entity pad)
{
	PadClamps clamps;
	clamps.player = pad;
	Scene::ClipPlayerComponent player;
	player.file = std::string{kClampClip};
	player.clip = std::string{kClampOpen};
	player.time = std::numeric_limits<float>::max();
	for (entt::entity &clamp : clamps.clamps)
	{
		const ClampParts parts = Clamp(scene, pad);
		clamp = parts.root;
		player.nodes.push_back({"clamp", parts.clamp});
		player.nodes.push_back({"post", parts.post});
		player.nodes.push_back({"arm", parts.arm});
		player.nodes.push_back({"jaw", parts.jaw});
	}
	scene.emplace<Scene::ClipPlayerComponent>(pad, std::move(player));
	return clamps;
}

// Sets the pad's clamps playing the clip, from its start or, shown as they already are, its end.
void PlayClamps(entt::registry &scene, const PadClamps &clamps, bool closed, bool atEnd)
{
	auto *player =
	    scene.valid(clamps.player) ? scene.try_get<Scene::ClipPlayerComponent>(clamps.player) : nullptr;
	if (player == nullptr)
		return;
	player->clip = std::string{closed ? kClampClose : kClampOpen};
	player->time = atEnd ? std::numeric_limits<float>::max() : 0.0f;
	player->playing = true;
}
} // namespace

glm::vec3 LampColour(Dock::PadState state, bool display)
{
	if (display)
		return {0.3f, 0.5f, 1.0f};
	switch (state)
	{
		case Dock::PadState::Free: return {0.15f, 1.0f, 0.25f};
		case Dock::PadState::Reserved: return {1.0f, 0.6f, 0.05f};
		case Dock::PadState::Holding: return {1.0f, 0.08f, 0.04f};
	}
	return glm::vec3{1.0f};
}

void FitClamps(entt::registry &scene, const PadClamps &clamps, const Dock::HullDefinition &hull)
{
	const float size = hull.height * kClampSize;
	for (std::size_t index = 0; index < clamps.clamps.size(); ++index)
	{
		auto *transform = scene.valid(clamps.clamps[index])
		                      ? scene.try_get<Scene::TransformComponent>(clamps.clamps[index])
		                      : nullptr;
		if (transform == nullptr)
			continue;
		// Two on the hull's +X side, turned to face it, and two on its -X side; fore and aft.
		const double side = index < 2 ? 1.0 : -1.0;
		const double along = (index % 2 == 0 ? -0.25 : 0.25) * double{hull.length};
		transform->translation = {side * (hull.beam * 0.5 + kClampReach * size), 0.0, along};
		transform->rotation = {0.0f, side > 0.0 ? glm::pi<float>() : 0.0f, 0.0f};
		transform->scale = glm::vec3{size};
	}
}

StationDressing DressStation(
    entt::registry &scene, entt::entity station, const Dock::StationLayout &layout,
    const Dock::Docks &docks)
{
	StationDressing dressing;
	dressing.station = station;
	if (const auto *celestial = scene.try_get<Scene::CelestialBodyComponent>(station))
		dressing.body = celestial->body;

	for (const std::vector<Dock::Box> *boxes : {&layout.decks, &layout.piers})
		for (const Dock::Box &box : *boxes)
		{
			// A block as tall as a quarter of its width is a hull with windows; a flatter one is deck.
			const bool block = box.size.y >= 0.25 * std::min(box.size.x, box.size.z);
			Part(
			    scene, station, box.centre, {}, glm::vec3{box.size * 0.5},
			    {Scene::Shape::Box, kDeckColour, block ? kPlating : kDeckPlating, {}});
		}

	for (std::size_t index = 0; index < layout.pads.size(); ++index)
	{
		const Dock::PadLayout &pad = layout.pads[index];
		const auto number = static_cast<unsigned>(index + 1);
		const entt::entity node =
		    Node(scene, station, pad.position, SceneRotation(Dock::PadOrientation(pad)));
		const glm::vec2 size = Dock::PadSize(pad.padClass);
		Part(
		    scene, node, glm::dvec3{0.0}, {}, {size.x * 0.5f, kPadThickness * 0.5f, size.y * 0.5f},
		    {Scene::Shape::Box, kPadColour, {}, {}});
		Paint(scene, node, pad, number);
		dressing.lamps.push_back(Floodlights(scene, node, pad, pad.display, dressing.floodlights));
		dressing.clamps.push_back(Clamps(scene, node));

		if (pad.ship.empty())
			continue;
		const Dock::HullDefinition *hull = docks.FindHull(pad.ship);
		if (!hull || hull->pad > pad.padClass)
		{
			Core::Log(
			    Core::Severity::Warning, kDressing,
			    "Pad {} of '{}' shows '{}', which {}; it is left bare", number, layout.displayName,
			    pad.ship, hull ? "is too large for it" : "is not a hull");
			continue;
		}
		const entt::entity ship = MakeHullProxy(scene, *hull);
		scene.emplace<Scene::BuiltComponent>(ship);
		scene.emplace<Scene::ParentComponent>(ship, node);
		scene.get<Scene::TransformComponent>(ship).translation =
		    glm::dvec3{0.0, hull->height * 0.5 + kPadThickness * 0.5, 0.0};
		// Held as it is shown, from the first frame.
		PadClamps &clamps = dressing.clamps.back();
		FitClamps(scene, clamps, *hull);
		PlayClamps(scene, clamps, true, true);
		clamps.closed = true;
	}

	for (const Dock::Bay &bay : layout.bays)
		Door(scene, station, bay);

	// The roads' lane dividers, on the deck between their nodes; a pad's own centre line is its
	// paint's.
	const Dock::RoadGraph roads = Dock::Roads(layout);
	for (const auto &[from, to] : roads.edges)
	{
		if (roads.nodes[from].kind == Dock::RoadNodeKind::Hatch ||
		    roads.nodes[to].kind == Dock::RoadNodeKind::Hatch)
			continue;
		const glm::dvec3 start = roads.nodes[from].position;
		const glm::dvec3 way = roads.nodes[to].position - start;
		const double length = glm::length(way);
		Part(
		    scene, station, start + way * 0.5 + glm::dvec3{0.0, kPaintHeight, 0.0},
		    {0.0f, static_cast<float>(std::atan2(way.x, way.z)), 0.0f},
		    {kRoadLine * 0.5f, kPaintThickness * 0.5f, static_cast<float>(length * 0.5)},
		    {Scene::Shape::Box, kNumberColour, {}, Scene::Layer::Effects});
		++dressing.roads;
	}

	// A station whose buggies are parked shows them on the deck: a row across each pad's stern, on
	// its plate behind the largest hull it takes, facing across it.
	if (!layout.traffic.working)
		for (const Dock::PadLayout &pad : layout.pads)
		{
			if (pad.display)
				continue;
			const glm::vec2 size = Dock::PadSize(pad.padClass);
			const glm::dquat turn = Dock::PadOrientation(pad);
			for (std::uint32_t index = 0; index < layout.traffic.buggies; ++index)
			{
				const glm::dvec3 across{
				    -size.x * 0.5 + 3.0 + 3.2 * static_cast<double>(index), kPadThickness * 0.5,
				    size.y * 0.5 - 2.5};
				const entt::entity buggy = MakeBuggyProxy(scene, entt::null);
				scene.emplace<Scene::BuiltComponent>(buggy);
				scene.emplace<Scene::ParentComponent>(buggy, station);
				auto &transform = scene.get<Scene::TransformComponent>(buggy);
				transform.translation = pad.position + turn * across;
				transform.rotation = SceneRotation(
				    turn * glm::angleAxis(glm::half_pi<double>(), glm::dvec3{0.0, 1.0, 0.0}));
				++dressing.parked;
			}
		}
	return dressing;
}

std::size_t ShowPadStates(
    entt::registry &scene, StationDressing &dressing, const Sim::Snapshot &snapshot,
    entt::entity simStation, const Dock::Docks &docks)
{
	std::size_t found = 0;
	for (const Sim::ComponentSample<Dock::Pad> &sample : snapshot.Components<Dock::Pad>())
	{
		const Dock::Pad &pad = sample.value;
		if (pad.station != simStation || pad.number == 0 || pad.number > dressing.lamps.size())
			continue;
		++found;
		const glm::vec3 colour = LampColour(pad.state, pad.display);
		const std::string_view material = pad.state == Dock::PadState::Reserved ? kBlinkingLamp : kLamp;
		for (const entt::entity lamp : dressing.lamps[pad.number - 1])
			if (auto *mesh = scene.valid(lamp) ? scene.try_get<Scene::MeshComponent>(lamp) : nullptr)
			{
				if (mesh->color != colour)
					mesh->color = colour;
				if (mesh->material != material)
					mesh->material = material;
			}

		// A display pad's clamps hold the ship it shows, as it was dressed.
		if (pad.display || pad.number > dressing.clamps.size())
			continue;
		PadClamps &clamps = dressing.clamps[pad.number - 1];
		const auto *docking = pad.state == Dock::PadState::Holding && pad.ship != entt::null
		                          ? snapshot.Get<Dock::Docking>(pad.ship)
		                          : nullptr;
		const bool closed = docking != nullptr && docking->pad == sample.entity &&
		                    docking->stage == Dock::Stage::Docked;
		const entt::entity ship = closed ? pad.ship : entt::null;
		if (clamps.closed == closed && clamps.ship == ship)
			continue;
		// First shown, or holding another ship already clamped, as a world loaded over this one may
		// have: shown as they are, without the events that would have brought them there.
		const bool atEnd = !clamps.closed || (closed && *clamps.closed);
		if (closed)
		{
			const auto *hull = snapshot.Get<Dock::Hull>(ship);
			const Dock::HullDefinition *definition = hull ? docks.FindHull(hull->definition) : nullptr;
			Dock::HullDefinition largest;
			if (definition == nullptr)
			{
				const glm::vec3 size = Dock::LargestHull(pad.padClass);
				largest.beam = size.y;
				largest.height = size.z;
				largest.length = size.x;
				definition = &largest;
			}
			FitClamps(scene, clamps, *definition);
		}
		PlayClamps(scene, clamps, closed, atEnd);
		clamps.closed = closed;
		clamps.ship = ship;
	}
	return found;
}

entt::entity MakeHullProxy(entt::registry &scene, const Dock::HullDefinition &hull, entt::entity ship)
{
	const entt::entity root = scene.create();
	scene.emplace<Scene::TransformComponent>(root);
	if (!hull.model.empty())
	{
		// Under the root, which the traffic layer places, at the model's own size and axes.
		const entt::entity model = Node(scene, root, glm::dvec3{0.0}, {});
		Scene::MeshComponent mesh;
		mesh.model = hull.model;
		scene.emplace<Scene::MeshComponent>(model, std::move(mesh));
		return root;
	}
	constexpr float kQuarter = 1.5707963f;
	const glm::vec3 half = glm::vec3{hull.beam, hull.height, hull.length} * 0.5f;
	const glm::vec3 idle = kWhite * kIdleGlow;
	ShipThrusters thrusters{ship, {}};
	for (const ProxyPart &part : ProxyOf(hull.pad))
	{
		const glm::vec3 extent = part.half * half;
		// A cylinder turned a quarter about X has its own Y along the hull's Z, and its Z along Y.
		const glm::vec3 scale = part.turned ? glm::vec3{extent.x, extent.z, extent.y} : extent;
		const bool engine = part.material == kEngine;
		const entt::entity made = Part(
		    scene, root, glm::dvec3{part.at * half},
		    part.turned ? glm::vec3{kQuarter, 0.0f, 0.0f} : glm::vec3{0.0f}, scale,
		    {part.shape, engine ? idle : part.colour, part.material, {}});
		if (ship != entt::null && engine)
			thrusters.ports.push_back(
			    {part.at * half,
			     {0.0f, 0.0f, 1.0f},
			     true,
			     Plume(scene, root, glm::dvec3{part.at * half}, std::min(extent.x, extent.y)),
			     made,
			     0.0f});
	}
	if (ship == entt::null)
		return root;

	// The manoeuvring jets: a cluster at each side of the bow and of the stern, at the hull's beam,
	// each firing out to its side, up and down, and the bow's forward, to push the ship astern.
	const float radius = std::clamp(0.06f * std::min(hull.beam, hull.height), 0.12f, 2.0f);
	for (const float along : {-1.0f, 1.0f})
		for (const float side : {-1.0f, 1.0f})
		{
			const glm::vec3 at{side * half.x * 0.92f, 0.0f, along * half.z * 0.7f};
			std::vector<glm::vec3> exhausts{
			    {side, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
			if (along < 0.0f)
				exhausts.emplace_back(0.0f, 0.0f, -1.0f);
			for (const glm::vec3 &exhaust : exhausts)
			{
				const glm::vec3 nozzle = at + exhaust * radius;
				thrusters.ports.push_back(
				    {nozzle, exhaust, false, Jet(scene, root, glm::dvec3{nozzle}, exhaust, radius),
				     Part(
				         scene, root, glm::dvec3{nozzle}, {}, glm::vec3{radius},
				         {kIco, idle, kEngine, {}}),
				     0.0f});
			}
		}
	scene.emplace<ShipThrusters>(root, std::move(thrusters));
	return root;
}

float PortFiring(const ThrusterPort &port, const Physics::Thrust &thrust)
{
	// The way the port pushes the ship, against its exhaust.
	const glm::vec3 push = -port.exhaust;
	if (port.engine)
		return std::clamp(glm::dot(push, thrust.linear), 0.0f, 1.0f);
	float firing = std::max(glm::dot(push, thrust.linear), 0.0f);
	// The turn it makes about the hull's middle, as a share of each axis's: one off every axis it could
	// turn the ship about, a jet at the middle, turns nothing.
	const glm::vec3 turn = glm::cross(port.at, push);
	if (const float length = glm::length(turn); length > 1.0e-4f)
		firing += std::max(glm::dot(turn / length, thrust.angular), 0.0f);
	return std::min(firing, 1.0f);
}

void ShowThrust(
    entt::registry &scene, const Sim::Snapshot &snapshot, entt::entity frame, double seconds)
{
	const float rise = static_cast<float>(1.0 - std::exp(-std::max(seconds, 0.0) / kGlowRise));
	const float fall = static_cast<float>(1.0 - std::exp(-std::max(seconds, 0.0) / kGlowFall));
	for (const auto [entity, thrusters] : scene.view<ShipThrusters>().each())
	{
		const auto *thrust = snapshot.Get<Physics::Thrust>(thrusters.ship);
		for (ThrusterPort &port : thrusters.ports)
		{
			const float firing = thrust ? PortFiring(port, *thrust) : 0.0f;
			if (auto *emitter = scene.valid(port.emitter)
			                        ? scene.try_get<Scene::ParticleEmitterComponent>(port.emitter)
			                        : nullptr)
			{
				// Dense and bright as it fires: a puff of a jet, a plume of an engine.
				const float rate = port.engine ? kPlumeRate : kJetRate;
				const float intensity = port.engine ? kPlumeIntensity : kJetIntensity;
				emitter->active = firing > kIdleThrottle;
				emitter->rate = rate * firing;
				emitter->color.a = intensity * (0.4f + 0.6f * firing);
				emitter->frame = frame;
			}
			port.glow += (firing - port.glow) * (firing > port.glow ? rise : fall);
			if (auto *mesh = scene.valid(port.nozzle) ? scene.try_get<Scene::MeshComponent>(port.nozzle)
			                                          : nullptr)
				mesh->color = kWhite * (kIdleGlow + (1.0f - kIdleGlow) * port.glow);
		}
	}
}

entt::entity MakeBuggyProxy(entt::registry &scene, entt::entity buggy)
{
	constexpr float kQuarter = 1.5707963f;
	constexpr glm::vec3 kChassis{0.22f, 0.23f, 0.25f};
	constexpr glm::vec3 kCab{0.9f, 0.62f, 0.12f};
	constexpr glm::vec3 kTyre{0.06f, 0.06f, 0.07f};
	constexpr glm::vec3 kCrate{0.82f, 0.82f, 0.78f};
	const entt::entity root = scene.create();
	scene.emplace<Scene::TransformComponent>(root);
	Part(scene, root, {0.0, 0.55, 0.0}, {}, {1.2f, 0.2f, 2.0f}, {kBox, kChassis, kPlating, {}});
	Part(scene, root, {0.0, 1.2, -1.45}, {}, {1.1f, 0.45f, 0.55f}, {kBox, kCab, {}, {}});
	Part(scene, root, {0.0, 1.35, -1.98}, {}, {0.9f, 0.2f, 0.02f}, {kBox, kCanopy, kGlass, {}});
	Part(
	    scene, root, {0.0, 1.72, -1.45}, {}, glm::vec3{0.12f},
	    {kIco, kDoorLampColour, kLamp, Scene::Layer::Effects});
	for (const double side : {-1.15, 1.15})
		for (const double along : {-1.3, 1.3})
			Part(
			    scene, root, {side, 0.35, along}, {0.0f, 0.0f, kQuarter}, {0.35f, 0.15f, 0.35f},
			    {kCylinder, kTyre, {}, {}});
	// Its load: up to four crates on its bed, two by two, shown by ShowBuggyLoads.
	BuggyLoad load{buggy, {}};
	for (std::size_t index = 0; index < load.crates.size(); ++index)
	{
		const glm::dvec3 at{index % 2 == 0 ? -0.55 : 0.55, 1.25, index < 2 ? -0.15 : 1.0};
		load.crates[index] = Part(scene, root, at, {}, glm::vec3{0.0f}, {kBox, kCrate, {}, {}});
	}
	scene.emplace<BuggyLoad>(root, load);
	return root;
}

void ShowBuggyLoads(entt::registry &scene, const Sim::Snapshot &snapshot)
{
	for (const auto [entity, load] : scene.view<const BuggyLoad>().each())
	{
		const auto *buggy = snapshot.Get<Dock::Buggy>(load.buggy);
		const std::int64_t crates = buggy ? buggy->crates : 0;
		for (std::size_t index = 0; index < load.crates.size(); ++index)
		{
			auto &transform = scene.get<Scene::TransformComponent>(load.crates[index]);
			const glm::vec3 scale{static_cast<std::int64_t>(index) < crates ? 0.48f : 0.0f};
			if (transform.scale != scale)
				transform.scale = scale;
		}
	}
}
} // namespace Jettison::App
