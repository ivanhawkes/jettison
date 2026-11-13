#include <jettison/scene/Sequence.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <utility>

#include <jettison/reflect/Reflect.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/serialise/Archive.h>

namespace Jettison::Scene
{
namespace
{
// ---- The file's records ---------------------------------------------------------------------

struct FadeKeyRecord
{
	double time{};
	float amount{};
};

struct EventRecord
{
	double time{};
	std::string name;
};

struct ShotRecord
{
	double duration{};
	double blend{};
	std::string frame;
	std::vector<glm::dvec3> rail;
	Math::Easing easing{Math::Easing::InOutCubic};
	std::string look;
	glm::dvec3 lookAt{0.0};
	std::vector<float> fov;
};

struct SequenceRecord
{
	float letterbox{};
	double blendOut{};
	glm::vec3 fadeColor{0.0f};
	std::vector<FadeKeyRecord> fade;
	std::vector<ShotRecord> shots;
	std::vector<EventRecord> events;
};

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		// The easings by the names of easings.net, which nothing else reads from a file yet.
		RegisterEnum<Math::Easing>("Math.Easing")
		    .Value<Math::Easing::Linear>("linear")
		    .Value<Math::Easing::InQuad>("inQuad")
		    .Value<Math::Easing::OutQuad>("outQuad")
		    .Value<Math::Easing::InOutQuad>("inOutQuad")
		    .Value<Math::Easing::InCubic>("inCubic")
		    .Value<Math::Easing::OutCubic>("outCubic")
		    .Value<Math::Easing::InOutCubic>("inOutCubic")
		    .Value<Math::Easing::OutBack>("outBack")
		    .Value<Math::Easing::OutElastic>("outElastic");
		RegisterType<FadeKeyRecord>("Scene.SequenceFadeKey")
		    .Field<&FadeKeyRecord::time>("time", Attribute::Saved)
		    .Field<&FadeKeyRecord::amount>("amount", Attribute::Saved);
		RegisterType<EventRecord>("Scene.SequenceEvent")
		    .Field<&EventRecord::time>("time", Attribute::Saved)
		    .Field<&EventRecord::name>("name", Attribute::Saved);
		RegisterType<ShotRecord>("Scene.SequenceShot")
		    .Field<&ShotRecord::duration>("duration", Attribute::Saved)
		    .Field<&ShotRecord::blend>("blend", Attribute::Saved)
		    .Field<&ShotRecord::frame>("frame", Attribute::Saved)
		    .Field<&ShotRecord::rail>("rail", Attribute::Saved)
		    .Field<&ShotRecord::easing>("easing", Attribute::Saved)
		    .Field<&ShotRecord::look>("look", Attribute::Saved)
		    .Field<&ShotRecord::lookAt>("lookAt", Attribute::Saved)
		    .Field<&ShotRecord::fov>("fov", Attribute::Saved);
		RegisterType<SequenceRecord>("Scene.Sequence", Attribute::Moddable)
		    .Field<&SequenceRecord::letterbox>("letterbox", Attribute::Saved)
		    .Field<&SequenceRecord::blendOut>("blendOut", Attribute::Saved)
		    .Field<&SequenceRecord::fadeColor>("fadeColor", Attribute::Saved)
		    .Field<&SequenceRecord::fade>("fade", Attribute::Saved)
		    .Field<&SequenceRecord::shots>("shots", Attribute::Saved)
		    .Field<&SequenceRecord::events>("events", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

[[noreturn]] void Refuse(std::string_view file, const std::string &what)
{
	throw Serialise::FormatError(std::format("Sequence '{}': {}", file, what));
}

bool Finite(const glm::dvec3 &value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// The role's index, added to the sequence's roles if it is new; none for no name.
std::size_t RoleIndex(Sequence &sequence, const std::string &role)
{
	if (role.empty())
		return kNoRole;
	const auto found = std::ranges::find(sequence.roles, role);
	if (found != sequence.roles.end())
		return static_cast<std::size_t>(found - sequence.roles.begin());
	sequence.roles.push_back(role);
	return sequence.roles.size() - 1;
}

// A point of the rail's span from point `span` to the next, a share u of the way by the spline's
// parameter: centripetal Catmull-Rom (Barry and Goldman's pyramid, the knots spaced by the square
// root of the distance between points), the ends' missing neighbours reflected through them so the
// rail starts and ends heading at the next point.
glm::dvec3 SplinePoint(const std::vector<glm::dvec3> &rail, std::size_t span, double u)
{
	const glm::dvec3 &p1 = rail[span];
	const glm::dvec3 &p2 = rail[span + 1];
	const glm::dvec3 p0 = span > 0 ? rail[span - 1] : 2.0 * p1 - p2;
	const glm::dvec3 p3 = span + 2 < rail.size() ? rail[span + 2] : 2.0 * p2 - p1;
	const auto knot = [](const glm::dvec3 &a, const glm::dvec3 &b)
	{ return std::sqrt(glm::length(b - a)); };
	const double t0 = 0.0;
	const double t1 = t0 + knot(p0, p1);
	const double t2 = t1 + knot(p1, p2);
	const double t3 = t2 + knot(p2, p3);
	const double t = t1 + (t2 - t1) * u;
	const auto lerp = [t](const glm::dvec3 &a, const glm::dvec3 &b, double ta, double tb)
	{ return a * ((tb - t) / (tb - ta)) + b * ((t - ta) / (tb - ta)); };
	const glm::dvec3 a1 = lerp(p0, p1, t0, t1);
	const glm::dvec3 a2 = lerp(p1, p2, t1, t2);
	const glm::dvec3 a3 = lerp(p2, p3, t2, t3);
	return lerp(lerp(a1, a2, t0, t2), lerp(a2, a3, t1, t3), t1, t2);
}

SequenceShot ToShot(
    Sequence &sequence, const ShotRecord &record, std::size_t index, std::string_view file)
{
	const std::string where = std::format("shot {}", index + 1);
	if (!std::isfinite(record.duration) || !(record.duration > 0.0))
		Refuse(file, where + " has no duration: it lasts more than no seconds");
	if (!std::isfinite(record.blend) || record.blend < 0.0 || record.blend > record.duration)
		Refuse(file, where + " blends in for longer than it lasts, or for less than no time");
	if (record.rail.empty())
		Refuse(file, where + " has no rail: a point at least, where the camera is");
	for (std::size_t point = 0; point < record.rail.size(); ++point)
	{
		if (!Finite(record.rail[point]))
			Refuse(file, where + " has a rail point that is not finite");
		if (point > 0 && record.rail[point] == record.rail[point - 1])
			Refuse(file, std::format("{} has rail point {} twice in a row", where, point + 1));
	}
	if (!Finite(record.lookAt))
		Refuse(file, where + " looks at a point that is not finite");
	if (record.fov.size() > 2 ||
	    std::ranges::any_of(record.fov, [](float fov) { return !(fov > 0.0f) || !(fov < 180.0f); }))
		Refuse(
		    file, where + " has a field of view of other than one or two angles from 0 to 180 degrees");

	SequenceShot shot;
	shot.duration = record.duration;
	shot.blend = record.blend;
	shot.frame = RoleIndex(sequence, record.frame);
	shot.look = RoleIndex(sequence, record.look);
	shot.rail = record.rail;
	shot.easing = record.easing;
	shot.lookAt = record.lookAt;
	if (!record.fov.empty())
	{
		shot.fovFrom = record.fov.front();
		shot.fovTo = record.fov.back();
	}
	// The length along the spline at each sample, which RailPoint walks to fly it evenly.
	shot.lengths.push_back(0.0);
	for (std::size_t span = 0; span + 1 < shot.rail.size(); ++span)
	{
		glm::dvec3 last = shot.rail[span];
		for (std::size_t sample = 1; sample <= kRailSamples; ++sample)
		{
			const glm::dvec3 point = SplinePoint(
			    shot.rail, span, static_cast<double>(sample) / static_cast<double>(kRailSamples));
			shot.lengths.push_back(shot.lengths.back() + glm::length(point - last));
			last = point;
		}
	}
	return shot;
}

// A frame's origin and its rotation, without the scale.
struct Frame
{
	glm::dvec3 origin{0.0};
	glm::dquat rotation{1.0, 0.0, 0.0, 0.0};
};

Frame FrameOf(std::size_t role, std::span<const glm::dmat4> roles)
{
	if (role == kNoRole)
		return {};
	const glm::dmat4 &world = roles[role];
	const glm::dmat3 basis{
	    glm::normalize(glm::dvec3{world[0]}), glm::normalize(glm::dvec3{world[1]}),
	    glm::normalize(glm::dvec3{world[2]})};
	return {glm::dvec3{world[3]}, glm::normalize(glm::quat_cast(basis))};
}

// A pose and the point it looks at, which a blend moves along a line as it moves the camera, so what a
// shot frames stays framed while it is blended to or from.
struct ShotPose
{
	CameraPose pose;
	glm::dvec3 target{0.0};
	float fov{0.0f};
};

ShotPose PoseAt(const SequenceShot &shot, double time, std::span<const glm::dmat4> roles, float gameFov)
{
	const double share =
	    Math::Ease(shot.easing, static_cast<float>(std::clamp(time / shot.duration, 0.0, 1.0)));
	const Frame frame = FrameOf(shot.frame, roles);
	const Frame lookFrame = shot.look == kNoRole ? frame : FrameOf(shot.look, roles);
	const glm::dvec3 position = frame.origin + frame.rotation * RailPoint(shot, share);
	const glm::dvec3 target = lookFrame.origin + lookFrame.rotation * shot.lookAt;
	const glm::dvec3 up = frame.rotation * glm::dvec3{0.0, 1.0, 0.0};
	const float fov = shot.fovFrom > 0.0f
	                      ? shot.fovFrom + (shot.fovTo - shot.fovFrom) * static_cast<float>(share)
	                      : gameFov;
	return {{position, LookRotation(target - position, up)}, target, fov};
}

// The game's camera as a shot is blended from or to: the point it looks at taken along its view, as
// far off as the shot's subject is from the shot's camera.
ShotPose GamePose(const CameraPose &game, float fov, const ShotPose &shot)
{
	const double reach = std::max(glm::length(shot.target - shot.pose.position), 1.0);
	return {game, game.position + game.orientation * glm::dvec3{0.0, 0.0, -reach}, fov};
}

// The camera along the line between the two, looking at the point along the line between their
// targets, with its up turned from one's to the other's.
ShotPose Blend(const ShotPose &from, const ShotPose &to, double share)
{
	const glm::dvec3 position = from.pose.position + (to.pose.position - from.pose.position) * share;
	const glm::dvec3 target = from.target + (to.target - from.target) * share;
	const glm::dvec3 up =
	    glm::slerp(from.pose.orientation, to.pose.orientation, share) * glm::dvec3{0.0, 1.0, 0.0};
	return {
	    {position, LookRotation(target - position, up)},
	    target,
	    from.fov + (to.fov - from.fov) * static_cast<float>(share)};
}

double BlendShare(double elapsed, double length)
{
	return Math::Ease(Math::Easing::InOutCubic, static_cast<float>(elapsed / length));
}

float FadeAt(const Sequence &sequence, double time)
{
	const std::vector<SequenceFadeKey> &keys = sequence.fade;
	if (keys.empty())
		return 0.0f;
	const auto next = std::ranges::upper_bound(keys, time, {}, &SequenceFadeKey::time);
	if (next == keys.begin())
		return keys.front().amount;
	if (next == keys.end())
		return keys.back().amount;
	const SequenceFadeKey &before = *(next - 1);
	const auto share = static_cast<float>((time - before.time) / (next->time - before.time));
	return before.amount + (next->amount - before.amount) * share;
}
} // namespace

Sequence ReadSequence(std::string_view text, std::string_view name)
{
	RegisterTypes();
	const auto record =
	    Serialise::YamlArchive::Read<SequenceRecord>(text, Serialise::FormatVersion{kSequenceVersion});
	Sequence sequence;
	if (record.shots.empty())
		Refuse(name, "it has no shots");
	for (std::size_t index = 0; index < record.shots.size(); ++index)
	{
		sequence.shots.push_back(ToShot(sequence, record.shots[index], index, name));
		sequence.duration += sequence.shots.back().duration;
	}
	if (!std::isfinite(record.blendOut) || record.blendOut < 0.0 || record.blendOut > sequence.duration)
		Refuse(name, "its blend out is longer than it is, or less than no time");
	sequence.blendOut = record.blendOut;
	if (!(record.letterbox >= 0.0f) || !(record.letterbox < 0.5f))
		Refuse(name, "its letterbox's bars are each from none to under half the frame");
	sequence.letterbox = record.letterbox;
	if (!std::isfinite(record.fadeColor.x) || !std::isfinite(record.fadeColor.y) ||
	    !std::isfinite(record.fadeColor.z) ||
	    glm::any(glm::lessThan(record.fadeColor, glm::vec3{0.0f})))
		Refuse(name, "its fade's colour is not a colour");
	sequence.fadeColor = record.fadeColor;
	for (std::size_t key = 0; key < record.fade.size(); ++key)
	{
		const FadeKeyRecord &fade = record.fade[key];
		if (!std::isfinite(fade.time) || fade.time < 0.0 || fade.time > sequence.duration ||
		    (key > 0 && !(fade.time > record.fade[key - 1].time)))
			Refuse(name, "its fade's times are not rising within it");
		if (!(fade.amount >= 0.0f) || !(fade.amount <= 1.0f))
			Refuse(name, "its fade has an amount other than from 0 to 1");
		sequence.fade.push_back({fade.time, fade.amount});
	}
	sequence.timeline.name = std::string{name};
	sequence.timeline.duration = static_cast<float>(sequence.duration);
	for (const EventRecord &event : record.events)
	{
		if (event.name.empty())
			Refuse(name, "it has an event of no name");
		if (!std::isfinite(event.time) || event.time < 0.0 || event.time > sequence.duration)
			Refuse(name, std::format("its event '{}' is not within it", event.name));
		sequence.timeline.events.push_back({static_cast<float>(event.time), event.name});
	}
	// In time order, which playback crosses them in; events at one time keep the file's order.
	std::ranges::stable_sort(sequence.timeline.events, {}, &Animation::ClipEvent::time);
	return sequence;
}

glm::dvec3 RailPoint(const SequenceShot &shot, double share)
{
	if (shot.rail.size() == 1)
		return shot.rail.front();
	const double length = std::clamp(share, 0.0, 1.0) * shot.lengths.back();
	// The samples either side of the length, and the share of the way between them.
	const auto after = std::ranges::upper_bound(shot.lengths, length);
	const auto sample = static_cast<std::size_t>(
	    std::clamp<std::ptrdiff_t>(after - shot.lengths.begin(), 1, std::ssize(shot.lengths) - 1));
	const double from = shot.lengths[sample - 1];
	const double to = shot.lengths[sample];
	const double between = to > from ? (length - from) / (to - from) : 0.0;
	const double parameter =
	    (static_cast<double>(sample - 1) + between) / static_cast<double>(kRailSamples);
	const std::size_t span = std::min(static_cast<std::size_t>(parameter), shot.rail.size() - 2);
	return SplinePoint(shot.rail, span, parameter - static_cast<double>(span));
}

SequenceView SampleSequence(
    const Sequence &sequence, double time, std::span<const glm::dmat4> roles, const CameraPose &game,
    float gameFovDegrees)
{
	SequenceView view;
	view.fade = FadeAt(sequence, time);
	if (!(time >= 0.0) || !(time < sequence.duration))
	{
		view.pose = game;
		view.verticalFovDegrees = gameFovDegrees;
		return view;
	}
	std::size_t index = 0;
	double start = 0.0;
	while (index + 1 < sequence.shots.size() && time >= start + sequence.shots[index].duration)
		start += sequence.shots[index++].duration;
	const SequenceShot &shot = sequence.shots[index];
	const double local = time - start;
	ShotPose pose = PoseAt(shot, local, roles, gameFovDegrees);
	double weight = 1.0;
	if (local < shot.blend)
	{
		// From the game's camera, or from the shot before, held where it ended.
		const double share = BlendShare(local, shot.blend);
		const ShotPose from = index == 0
		                          ? GamePose(game, gameFovDegrees, pose)
		                          : PoseAt(
		                                sequence.shots[index - 1], sequence.shots[index - 1].duration,
		                                roles, gameFovDegrees);
		pose = Blend(from, pose, share);
		if (index == 0)
			weight = share;
	}
	if (const double left = sequence.duration - time; left < sequence.blendOut)
	{
		const double share = BlendShare(left, sequence.blendOut);
		pose = Blend(GamePose(game, gameFovDegrees, pose), pose, share);
		weight *= share;
	}
	view.pose = pose.pose;
	view.verticalFovDegrees = pose.fov;
	view.weight = static_cast<float>(weight);
	view.letterbox = sequence.letterbox * view.weight;
	return view;
}

void RegisterSequenceLoader(Resource::ResourceManager &resources)
{
	// As a clip file's: the decode on the worker keeps the bytes, in the one role of a sequence not
	// yet parsed, and the finish on the main thread parses them over it.
	resources.RegisterLoader(
	    Resource::Loader<Sequence>{
	        [](std::span<const std::byte> bytes, std::string_view)
	        {
		        auto sequence = std::make_unique<Sequence>();
		        sequence->roles.emplace_back(
		            reinterpret_cast<const char *>(bytes.data()), bytes.size());
		        return sequence;
	        },
	        [](Sequence &sequence, std::string_view path)
	        {
		        const std::string text = std::move(sequence.roles.front());
		        sequence = ReadSequence(text, path);
	        },
	        {}});
}
} // namespace Jettison::Scene
