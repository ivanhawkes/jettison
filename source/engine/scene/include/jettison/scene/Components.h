#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <jettison/animation/Rig.h>
#include <jettison/resource/ResourceHandle.h>
#include <jettison/scene/CameraRigs.h>
#include <jettison/scene/Shape.h>
#include <jettison/scene/Wave.h>

// The presentation scene's components (docs/architecture.md, "Simulation and presentation"): what is
// drawn, lit and looked through, in the scene's own registry. They name no device: the renderer
// reads them, and finds what a mesh draws with itself. The scene file (Scene::Scene) stores the
// authored fields; the runtime ones are documented as such, and never reach the file.
namespace Jettison::Scene
{
struct SceneEntityId
{
	std::string value;
};

// The content layers within a system's scene (docs/scope.md, "Scene: layers"): for loading in
// stages and for editor visibility.
enum class Layer : std::uint8_t
{
	Environment, // Bodies, stations, the sky: what the system description places.
	Traffic,     // Ships, cargo, debris: what the simulation places.
	Effects      // Beacons, markers, transient visuals.
};
inline constexpr std::size_t kLayerCount = 3;

// The layer an entity is in, as authored. An entity without one is in its parent's layer, and a root
// without one is in the environment, so a station's parts are where the station is.
struct LayerComponent
{
	Layer layer{Layer::Environment};
};

// Which layers are shown: held in the scene registry's context, where Scene::SetLayerVisible writes
// it and the scene update reads it. A registry without one shows every layer.
struct LayerVisibility
{
	std::uint8_t visible{0xFF};

	bool IsVisible(Layer layer) const
	{
		return (visible & (1u << static_cast<unsigned>(layer))) != 0;
	}
};

// Runtime: the entity's layer is hidden, so the renderer neither draws it nor lights anything with
// it. The scene update adds and removes it from the layers' visibility; the sun and the sky are the
// system's, and are shown whatever their layer.
struct HiddenComponent
{
};

// Runtime: an entity the system scene made rather than the scene file authored (a body the file does
// not present, the sky or camera it lacks, a simulation entity's proxy). The writer leaves it out, so
// a scene saved from a running engine is the file that was loaded.
struct BuiltComponent
{
};

// A presentation entity that shows a simulation entity. Presentation holds the simulation entity's
// identifier, never a reference into the world, and finds it in the snapshots.
struct SimProxyComponent
{
	entt::entity simEntity{entt::null};
};

// Relative to the parent, or to the system frame's origin for an entity without one. The
// translation is metres in double precision, so a scene can place a planet a hundred million
// kilometres out and a hull plate on a station beside it; rotation and scale stay float, since
// neither grows with the distance from the origin.
struct TransformComponent
{
	glm::dvec3 translation{0.0};
	glm::vec3 rotation{0.0f};
	glm::vec3 scale{1.0f};
};

// Where something outside the scene puts an entity this frame: the star system, for a celestial
// body. It replaces the transform's translation and rotation, in double precision, and keeps its
// scale. Runtime state, not written to the scene file, so the transform stays as authored and a
// scene saved from a running engine is the scene that was loaded.
struct PlacementComponent
{
	glm::dvec3 position{0.0};
	glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
	// The body's radius in metres, or 0 when it has none: the sky draws the sun's disc this size.
	double radius{0.0};
};

struct ParentComponent
{
	entt::entity entity{entt::null};
};

// The transform in the system frame, composed in double precision. The renderer takes the
// camera's position off it before anything becomes a float (Render::ToRenderSpace).
struct WorldTransformComponent
{
	glm::dmat4 matrix{1.0};
};

// The camera as authored. There is no far plane: the projection is reversed-Z with an infinite far
// plane (Render::ReversedInfinitePerspective).
struct CameraComponent
{
	glm::dvec3 position{2.0, 2.0, 2.0};
	// Scene coordinates are right-handed and Y-up. Radians: pitch about X, roll about view, yaw about
	// Y.
	glm::vec3 rotation{-0.615479709f, 0.0f, 0.785398163f};
	float verticalFovDegrees{45.0f};
	float nearPlane{0.1f};
	bool active{true};
};

// Where a camera is this frame, when something other than its authored pose moves it: a camera
// rig writes it in the presentation phase. Runtime state, not written to the scene file; a camera
// without one is where CameraComponent puts it.
struct CameraPoseComponent
{
	glm::dvec3 position{0.0};
	glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
};

enum class LightType
{
	Point,
	Spot
};

// A local light (docs/architecture.md, "Rules that follow"): a point or a spot, shining from the
// entity's position, with a finite range past which it lights nothing, so the cluster passes can
// assign it to the clusters it reaches and no others. A spot points along the entity's -Z, and its
// cone angles are half-angles from that axis in radians, full inside the inner and dark outside the
// outer, as glTF's KHR_lights_punctual has them. Intensity is the irradiance at one metre, in the
// HDR target's linear units, falling off with the inverse square of the distance and windowed to
// reach zero at the range. The star's light is SunComponent's, and the ambient light the sky's.
//
// Its intensity can move with presentation time, as a beacon's blinks: scaled from one share of it
// to another along a wave, as a material's animated parameter moves (render/Material.h), so a lamp's
// light and its glow, given the same wave, period and phase, keep in step.
struct LightAnimation
{
	Wave wave{Wave::Square};
	// Seconds per cycle, above zero, and where in its cycle the wave starts, in cycles.
	double period{1.0};
	double phase{0.0};
	// The share of the intensity at the wave's 0 and at its 1.
	float from{0.0f};
	float to{1.0f};
};

struct LightComponent
{
	LightType type{LightType::Point};
	glm::vec3 color{1.0f};
	float intensity{6.0f};
	float range{10.0f};
	float innerConeAngle{0.0f};
	float outerConeAngle{0.785398163f};
	bool active{true};
	std::optional<LightAnimation> animation;
};

// The intensity a light shines with at a presentation time, in seconds: its own, or as its animation
// has it then.
float LightIntensityAt(const LightComponent &light, double time);

// What an entity draws: a model file, or a shape made in code in one colour. Both are by name, and
// the renderer loads or makes what they name. The model is a virtual path (models/Fox.glb), as the
// resource manager loads it, so a mod's mount can shadow it. A procedural shape is unit-sized (a
// sphere of radius one, a cube from -1 to 1), and the transform's scale sizes it. The material,
// when there is one, is a virtual path to a material file, which every primitive of the model draws
// with; without one, a model draws with its own textures and a shape with its colour, through the
// default mesh program.
struct MeshComponent
{
	std::string model;
	std::optional<Shape> shape;
	glm::vec3 color{1.0f};
	// A ring's inner radius, against its outer radius of one.
	float innerRadius{0.5f};
	std::string material;
};

// A model file's rig (animation/Rig.h), which its clip player samples and which places the mesh
// within the file. Runtime state, not written to the scene file: the scene loads it for every
// model mesh, and releases it with the scene. The revision is the rig's that the entity's clip was
// last looked up in, and playable says whether the rig has that clip and a skin for it to move, so
// a rig that reloads is looked in again.
struct RigComponent
{
	Resource::ResourceHandle<Animation::Rig> rig;
	std::uint32_t revision{0};
	bool playable{false};
};

// An entity the star system places: each frame its translation is the body's position at the
// presentation time, and its rotation the body's spin. The scene names the system file, and the
// system scene (scene/SystemScene.h) evaluates it.
struct CelestialBodyComponent
{
	std::string body;
};

// The star system's one directional light (docs/architecture.md, "Rules that follow"): it shines
// from the entity's position, which is normally the star's, towards whatever the camera sees, and
// lives in the frame constants rather than the light list. Linear colour times intensity.
struct SunComponent
{
	glm::vec3 color{1.0f};
	float intensity{1.0f};
};

// The star system's sky (assets/shaders/include/sky.glsl): a starfield, the galaxy's band and a
// nebula, made from a seed rather than painted. It is drawn behind the scene, and it is the
// environment the scene is lit by: the renderer captures it into a cube map and prefilters that
// for image-based lighting when the scene loads or the sky changes. A scene has at most one;
// without one the sky is black and lights nothing. Radiance and flux are in the HDR target's linear
// units, the sun's intensity being irradiance in the same units.
struct SkyComponent
{
	std::uint32_t seed{1};
	// About how many stars the sky has, over the whole sphere.
	float stars{6000.0f};
	// The brightest star's flux: the irradiance it delivers, which the faintest are a tiny part of.
	float starFlux{3.0e-5f};
	// The galaxy's plane, seen edge-on as a band: its normal, its angular half-width in radians, and
	// its radiance along its centre line.
	glm::vec3 bandNormal{0.0f, 1.0f, 0.0f};
	float bandWidth{0.2f};
	glm::vec3 bandColor{0.0f};
	glm::vec3 nebulaColor{0.0f};
	// How many times the nebula's pattern repeats around the sky: larger is smaller clouds.
	float nebulaScale{2.0f};

	friend bool operator==(const SkyComponent &, const SkyComponent &) = default;
};

// A celestial body's atmosphere (docs/architecture.md, "Atmospheres"): air over the surface of the
// sphere the entity's mesh draws, which scatters the sun's light towards the camera, as a limb about
// the body seen from space and a haze over its surface, and reddens the sunlight that reaches the
// surface at the terminator. Single scattering by Rayleigh (the air) and Mie (haze), each of a
// density falling off exponentially with height; the surface's radius is the mesh's, the
// transform's smallest scale. SI units: metres, and per metre at the surface for the coefficients.
// The defaults are Earth's, as Hillaire has them (2020), which the haze of another world is a change
// of.
struct AtmosphereComponent
{
	// Above the surface, where the air is taken to end.
	double height{100000.0};
	glm::vec3 rayleigh{5.802e-6f, 13.558e-6f, 33.1e-6f};
	float rayleighHeight{8000.0f};
	// Mie scattering, and its extinction, which is scattering and absorption both and is not less
	// than it.
	float mie{3.996e-6f};
	float mieExtinction{4.44e-6f};
	float mieHeight{1200.0f};
	// The phase function's anisotropy, from -1 to 1: towards 1, the glow about the sun is tighter.
	float mieAnisotropy{0.8f};

	friend bool operator==(const AtmosphereComponent &, const AtmosphereComponent &) = default;
};

// A celestial body's rings (docs/architecture.md, "Atmospheres"): a flat annulus of ice and dust in
// the plane of the body's equator, the transform's XZ plane, between two radii from its centre in
// metres. Its bands and gaps are drawn from the seed; the opacity is the densest band's, the share
// of the light behind it that it stops. Lit by the sun, in the body's shadow, and casting its own
// onto the body.
struct RingsComponent
{
	double innerRadius{0.0};
	double outerRadius{0.0};
	glm::vec3 color{0.8f, 0.74f, 0.64f};
	float opacity{0.8f};
	std::uint32_t seed{1};

	friend bool operator==(const RingsComponent &, const RingsComponent &) = default;
};

// A particle emitter (docs/architecture.md, "Particles"): sprites simulated on the GPU, born about
// the entity's position and sent along its -Z, as a spot shines, within a cone, then slowed by drag,
// and drawn in the transparent pass as discs facing the camera that fade towards their edges. Each
// is born with the first colour and size and dies with the end ones, mixed linearly over its life.
// It adds light, its colour times the intensity in the HDR target's linear units, and hides its
// opacity's share of what is behind it; with an albedo it is lit as well, by the sun, the clusters'
// lights and the sky, as a small dusty sphere would be.
//
// Particles are at rest in the frame of the entity named, its position and its turn: by default the
// emitter's own, so they keep to it as it moves and turns, as a nozzle's glow does. A ship's exhaust,
// left behind as it flies, is in the frame of what it flies by. Presentation's alone, as a mesh is:
// nothing the simulation decides waits on one.
struct ParticleEmitterComponent
{
	// Particles a second while it is active, and how many at once when it is first drawn active and
	// each time pulses goes up, which the game counts.
	float rate{10.0f};
	std::uint32_t burst{0};
	std::uint32_t pulses{0};
	// Seconds each lives, and the share by which each is shortened at random, from 0 to 1.
	float lifetime{1.0f};
	float lifetimeJitter{0.0f};
	// Metres a second at birth, and the share by which each is slowed at random, from 0 to 1.
	float speed{1.0f};
	float speedJitter{0.0f};
	// The cone's half-angle about -Z, in radians, from 0 (a line) to pi (every way).
	float spread{0.0f};
	// Born anywhere within a sphere of this radius about the entity, in metres.
	float radius{0.0f};
	// The share of its speed each loses a second, as an exponential: 1 is about two thirds.
	float drag{0.0f};
	// Its diameter in metres at birth and at death.
	glm::vec2 size{0.1f, 0.1f};
	// The seconds of its path behind it that it is drawn out along, in perspective, for streaks and
	// sparks; 0 for a round sprite.
	float stretch{0.0f};
	// rgb: linear colour, a: intensity, at birth and at death.
	glm::vec4 color{1.0f};
	glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
	// The share of what is behind it that it hides, at its middle: 0 adds light alone, as a flame.
	float opacity{0.0f};
	// The share of the light on it that it scatters, by colour: 0 for one that is not lit.
	glm::vec3 albedo{0.0f};
	// The most alive at once: a new particle past it takes the oldest's place.
	std::uint32_t capacity{256};
	std::uint32_t seed{1};
	bool active{true};
	// The entity whose frame the particles are at rest in, null for the emitter's own, and the file's
	// id for it; the entity is runtime state, as an orbit camera's target is.
	std::string frameId;
	entt::entity frame{entt::null};
};

// A mesh outlined on the screen, and every mesh under it, as one shape (docs/architecture.md,
// "Outlines"): the selected, by a crisp line a pixel wide just outside it, or the targeted, by a soft
// glow that fades out over its width. Either reads at any distance, since both are measured in
// pixels, and is drawn over whatever is about the shape, under the UI. Presentation's alone: the game
// says what is selected or targeted, and marks it.
enum class OutlineStyle : std::uint8_t
{
	Selected,
	Targeted,
};

struct OutlineComponent
{
	OutlineStyle style{OutlineStyle::Selected};
	// rgb: linear colour, a: intensity, in the HDR target's linear units.
	glm::vec4 color{1.0f, 0.75f, 0.2f, 1.0f};
	// The glow's width in pixels, from 1 to kMaxOutlineWidth; the selected line is a pixel wide.
	float width{12.0f};
};

// The widest glow: the jump flood (render/PostProcess.h) reaches 31 pixels, and a glow this wide has
// faded to nothing well inside that.
inline constexpr float kMaxOutlineWidth = 24.0f;

// The marks a mesh, and every mesh under it, leaves in the depth target's stencil for the frame's
// later passes (docs/architecture.md, "Masks"): whether it is the cockpit the camera sits in, which
// outlines are not drawn over; whether post-processing leaves it as it is shaded, as a display a
// game draws in the world wants; and five bits that are the game's, for its own passes. The engine's
// third bit, the outlined, follows from OutlineComponent.
struct MaskComponent
{
	bool cockpit{false};
	bool unprocessed{false};
	// Bits 3 to 7 of the stencil, as a number from 0 to 31.
	std::uint8_t game{0};
};

// The most particles an emitter may keep alive at once.
inline constexpr std::uint32_t kMaxEmitterParticles = 65536;

// A camera that orbits another entity. The authored fields are where it starts; the rig and the
// resolved target are runtime state, not written to the scene file.
struct OrbitCameraComponent
{
	std::string targetId;
	double distance{10.0};
	double yaw{0.0};
	double pitch{0.3};
	double minDistance{1.0};

	entt::entity target{entt::null};
	// Braced, so the component can be value-initialised: the rig's constructor is explicit.
	OrbitCamera rig{};
	CameraTransition transition;
	bool placed{false};
};

// Which of a pilot camera's rigs places the camera.
enum class PilotView : std::uint8_t
{
	Chase,
	Cockpit
};

// Runtime: a camera on a piloted ship, which the game adds and the scene file never holds, since
// what it follows is a simulation entity's proxy (SimProxyComponent) that the system scene makes.
// Both rigs follow the target's world transform every frame, so switching between them is a
// transition from where one has the camera to where the other has it, never a swing as the chase
// rig catches up. The target's world transform is interpolated between the simulation's steps, so
// the camera moves as smoothly as the display does. While it has one, a camera's pose is this
// component's rather than its orbit rig's.
//
// The frame is the entity the ship flies by, the station whose frame its physics is in: what the
// sun's shadow grid is fixed to (Render::CameraView::frameOrigin), as an orbit camera's target is.
struct PilotCameraComponent
{
	entt::entity target{entt::null};
	entt::entity frame{entt::null};
	PilotView view{PilotView::Chase};
	ChaseCamera chase{};
	CockpitCamera cockpit{};
	CameraTransition transition;
};
} // namespace Jettison::Scene
