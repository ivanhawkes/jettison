#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/animation/Clip.h>
#include <jettison/math/Easing.h>
#include <jettison/scene/CameraRigs.h>

namespace Jettison::Resource
{
class ResourceManager;
}

namespace Jettison::Scene
{
// A cinematic (docs/architecture.md, "Cinematics"): shots of a camera on rails, cut or blended one
// into the next, over the game's own camera and back to it, with a fade, letterbox bars and events
// on the same timeline. It is presentation alone: nothing in it touches the simulation, and the
// game's camera rigs run under it the whole time, so it hands the camera back to wherever they have
// it by then. A shot's rail and what it looks at are in the frames of roles, names the game binds to
// the scene's entities as it plays the sequence (SequencePlayerComponent): a rail round a pad rides
// the station as it orbits, in double precision. A file at cinematics/<name>.sequence.yaml:
//
//   version: 1
//   letterbox: 0.1            # each bar's share of the frame's height, eased in and out with it
//   blendOut: 1.5             # seconds over the end of the last shot back to the game's camera
//   fadeColor: [0, 0, 0]
//   fade:                     # the fade's amount, keyed by time and linear between, held past
//     - {time: 3.6, amount: 0}
//     - {time: 4.0, amount: 1}
//   shots:
//     - duration: 4           # seconds
//       blend: 1.5            # seconds over its start from what was shown before it: the game's
//                             # camera, or the shot before held at its end; 0 cuts
//       frame: pad            # the role whose frame the rail is in; none for the system's
//       rail: [[30, 8, -20], [10, 5, -35]]   # metres, flown through at an even speed
//       easing: inOutCubic    # how the shot's time is spent along the rail (Math::Easing)
//       look: ship            # the role looked at, at lookAt in its frame; none for lookAt in the
//       lookAt: [0, 1, 0]     # shot's frame
//       fov: [40, 30]         # degrees, from the shot's start to its end; none for the camera's own
//   events:                   # names for the game, heard as the timeline crosses them
//     - {time: 2.5, name: dock.closing}
//
// The camera's up is its frame's +Y. A sequence lasts as long as its shots.
inline constexpr std::uint32_t kSequenceVersion = 1;
inline constexpr std::size_t kNoRole = std::numeric_limits<std::size_t>::max();

// Samples of a rail's spline a span, which its even speed is measured over.
inline constexpr std::size_t kRailSamples = 32;

struct SequenceShot
{
	double duration{0.0};
	double blend{0.0};
	// Indices into Sequence::roles, or kNoRole.
	std::size_t frame{kNoRole};
	std::size_t look{kNoRole};
	// The rail's points, and the length along it at each of its samples, kRailSamples a span and the
	// last point's: a rail of one point holds the camera there.
	std::vector<glm::dvec3> rail;
	std::vector<double> lengths;
	Math::Easing easing{Math::Easing::InOutCubic};
	glm::dvec3 lookAt{0.0};
	// Degrees at the start and the end; 0 for the camera's own.
	float fovFrom{0.0f};
	float fovTo{0.0f};
};

struct SequenceFadeKey
{
	double time{0.0};
	float amount{0.0f};
};

struct Sequence
{
	// Every role the shots name, in the order first named.
	std::vector<std::string> roles;
	std::vector<SequenceShot> shots;
	double duration{0.0};
	double blendOut{0.0};
	float letterbox{0.0f};
	glm::vec3 fadeColor{0.0f};
	std::vector<SequenceFadeKey> fade;
	// The events, as a clip of no tracks, so playback crosses them as it crosses a clip's
	// (Animation::AdvanceClip). Its duration is the sequence's.
	Animation::Clip timeline;
};

// The file's sequence, checked: at least one shot, each of a duration above zero and a blend no
// longer than it, a rail of at least one point and no two in a row the same, a field of view from 0
// to 180 degrees; the blend out no longer than the sequence, the bars under half the frame, the fade's
// times rising within the sequence and its amounts from 0 to 1, each event named and in the sequence.
// Throws Serialise::FormatError naming the file.
Sequence ReadSequence(std::string_view text, std::string_view name);

// The point a share of the way along a shot's rail by its length, from 0 to 1: a centripetal
// Catmull-Rom spline through its points, which neither overshoots nor loops where they bunch, flown
// at an even speed. In the shot's frame.
glm::dvec3 RailPoint(const SequenceShot &shot, double share);

// Where the sequence has the camera, and what it shows over it, at a time: the game's camera blended
// in over the first shot's blend and out over the blend out, by weight; each shot from the one before
// over its own blend; the bars by the same weight. A blend moves the camera along a line and the
// point it looks at along another, so a shot's subject stays in view: the game camera's point is
// along its view, as far as the shot's subject is from the shot's camera.
struct SequenceView
{
	CameraPose pose;
	// The game camera's where no shot sets one.
	float verticalFovDegrees{0.0f};
	// How much of the camera is the sequence's, from 0 (the game's) to 1.
	float weight{0.0f};
	float fade{0.0f};
	float letterbox{0.0f};
};

// The roles' frames are their entities' world transforms, by index into Sequence::roles; their scale
// is taken off. The game's camera is the pose its rigs left this frame, and its field of view.
SequenceView SampleSequence(
    const Sequence &sequence, double time, std::span<const glm::dmat4> roles, const CameraPose &game,
    float gameFovDegrees);

// Registers the loader of Scene::Sequence, from its virtual path, with the resource manager: the
// bytes are read on the I/O worker and the YAML parsed on the main thread, as a clip file's are, since
// the archive reads the reflection tables that thread registers into. Once per manager.
void RegisterSequenceLoader(Resource::ResourceManager &resources);
} // namespace Jettison::Scene
