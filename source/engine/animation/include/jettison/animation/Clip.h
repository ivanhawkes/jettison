#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Animation
{
enum class TrackPath
{
	Translation,
	Rotation,
	Scale
};

enum class Interpolation
{
	Linear,
	Step,
	CubicSpline
};

// Keyframes for one property of one node. Rotations are stored x, y, z, w; translations and
// scales use the first three components. A cubic-spline track holds three values per key, in-tangent,
// value and out-tangent, as glTF lays them out. The node is an index into a rig's nodes for a
// glTF clip, and into its file's node names for a clip file's (ClipFile.h).
struct Track
{
	std::size_t nodeIndex{};
	TrackPath path{TrackPath::Translation};
	Interpolation interpolation{Interpolation::Linear};
	std::vector<float> times;
	std::vector<glm::vec4> values;
};

// A moment in a clip that presentation hears of as playback crosses it (AdvanceClip): a clamp
// closing, for a sound. The name is the game's to give meaning to.
struct ClipEvent
{
	float time{};
	std::string name;
};

// Tracks, and the events in time order. The duration is the last key's or event's time.
struct Clip
{
	std::string name;
	float duration{};
	std::vector<Track> tracks;
	std::vector<ClipEvent> events;
};

// The track's value at time seconds: held before its first key and after its last, rotations
// slerped by the shorter way and normalised.
glm::vec4 Sample(const Track &track, float time);

// The clip of that name among the clips, or null.
const Clip *FindClip(const std::vector<Clip> &clips, std::string_view name);
} // namespace Jettison::Animation
