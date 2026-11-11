#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <string>

#include <jettison/animation/Rig.h>
#include <jettison/resource/ResourceHandle.h>
#include <jettison/scene/CameraRigs.h>
#include <jettison/scene/Shape.h>

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
struct LightComponent
{
	LightType type{LightType::Point};
	glm::vec3 color{1.0f};
	float intensity{6.0f};
	float range{10.0f};
	float innerConeAngle{0.0f};
	float outerConeAngle{0.785398163f};
	bool active{true};
};

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
// presentation time, and its rotation the body's spin. The scene names the system file; the app
// evaluates it (engine/celestial).
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
} // namespace Jettison::Scene
