#include <jettison/animation/ClipFile.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <format>
#include <memory>
#include <set>
#include <span>
#include <utility>

#include <jettison/reflect/Reflect.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/serialise/Archive.h>

namespace Jettison::Animation
{
namespace
{
// ---- The file's records ---------------------------------------------------------------------

struct TrackRecord
{
	std::string node;
	TrackPath path{TrackPath::Translation};
	Interpolation interpolation{Interpolation::Linear};
	std::vector<float> times;
	std::vector<glm::vec3> values;
};

struct EventRecord
{
	float time{};
	std::string name;
};

struct ClipRecord
{
	std::string name;
	std::vector<TrackRecord> tracks;
	std::vector<EventRecord> events;
};

struct ClipFileRecord
{
	std::vector<ClipRecord> clips;
};

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterEnum<TrackPath>("Animation.TrackPath")
		    .Value<TrackPath::Translation>("translation")
		    .Value<TrackPath::Rotation>("rotation")
		    .Value<TrackPath::Scale>("scale");
		RegisterEnum<Interpolation>("Animation.Interpolation")
		    .Value<Interpolation::Linear>("linear")
		    .Value<Interpolation::Step>("step")
		    .Value<Interpolation::CubicSpline>("cubic");
		RegisterType<TrackRecord>("Animation.Track")
		    .Field<&TrackRecord::node>("node", Attribute::Saved)
		    .Field<&TrackRecord::path>("path", Attribute::Saved)
		    .Field<&TrackRecord::interpolation>("interpolation", Attribute::Saved)
		    .Field<&TrackRecord::times>("times", Attribute::Saved)
		    .Field<&TrackRecord::values>("values", Attribute::Saved);
		RegisterType<EventRecord>("Animation.ClipEvent")
		    .Field<&EventRecord::time>("time", Attribute::Saved)
		    .Field<&EventRecord::name>("name", Attribute::Saved);
		RegisterType<ClipRecord>("Animation.Clip")
		    .Field<&ClipRecord::name>("name", Attribute::Saved)
		    .Field<&ClipRecord::tracks>("tracks", Attribute::Saved)
		    .Field<&ClipRecord::events>("events", Attribute::Saved);
		RegisterType<ClipFileRecord>("Animation.ClipFile", Attribute::Moddable)
		    .Field<&ClipFileRecord::clips>("clips", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

[[noreturn]] void Refuse(std::string_view file, const std::string &what)
{
	throw Serialise::FormatError(std::format("Clip file '{}': {}", file, what));
}

// A turn about X, then Y, then Z, as the scene's transform composes one.
glm::vec4 Turn(const glm::vec3 &angles)
{
	const glm::quat turn = glm::angleAxis(angles.x, glm::vec3{1.0f, 0.0f, 0.0f}) *
	                       glm::angleAxis(angles.y, glm::vec3{0.0f, 1.0f, 0.0f}) *
	                       glm::angleAxis(angles.z, glm::vec3{0.0f, 0.0f, 1.0f});
	return {turn.x, turn.y, turn.z, turn.w};
}

bool Finite(const glm::vec3 &value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Track ToTrack(
    const TrackRecord &record, std::size_t node, std::string_view file, const std::string &clip)
{
	const std::string where = std::format("clip '{}', node '{}'", clip, record.node);
	if (record.times.empty())
		Refuse(file, where + " has a track of no keys");
	for (std::size_t key = 0; key < record.times.size(); ++key)
	{
		const float time = record.times[key];
		if (!std::isfinite(time) || time < 0.0f || (key > 0 && !(time > record.times[key - 1])))
			Refuse(file, where + " has key times that are not finite, not negative and rising");
	}
	const bool cubic = record.interpolation == Interpolation::CubicSpline;
	if (cubic && record.path == TrackPath::Rotation)
		Refuse(file, where + " turns by a cubic track, which is translation or scale alone");
	const std::size_t perKey = cubic ? 3 : 1;
	if (record.values.size() != record.times.size() * perKey)
		Refuse(
		    file, std::format(
		              "{} has {} values for {} keys; it needs {}", where, record.values.size(),
		              record.times.size(), record.times.size() * perKey));
	Track track;
	track.nodeIndex = node;
	track.path = record.path;
	track.interpolation = record.interpolation;
	track.times = record.times;
	track.values.reserve(record.values.size());
	for (const glm::vec3 &value : record.values)
	{
		if (!Finite(value))
			Refuse(file, where + " has a value that is not finite");
		track.values.push_back(
		    record.path == TrackPath::Rotation ? Turn(value) : glm::vec4{value, 0.0f});
	}
	return track;
}
} // namespace

ClipFile ReadClipFile(std::string_view text, std::string_view name)
{
	RegisterTypes();
	const auto record =
	    Serialise::YamlArchive::Read<ClipFileRecord>(text, Serialise::FormatVersion{kClipFileVersion});
	ClipFile file;
	std::set<std::string_view> clipNames;
	for (const ClipRecord &clipRecord : record.clips)
	{
		if (clipRecord.name.empty() || !clipNames.insert(clipRecord.name).second)
			Refuse(name, "clip names must be non-empty and unique: '" + clipRecord.name + "'");
		Clip clip;
		clip.name = clipRecord.name;
		std::set<std::pair<std::size_t, TrackPath>> paths;
		for (const TrackRecord &trackRecord : clipRecord.tracks)
		{
			if (trackRecord.node.empty())
				Refuse(name, "clip '" + clip.name + "' has a track on no node");
			std::size_t node = NodeIndex(file, trackRecord.node);
			if (node == file.nodes.size())
				file.nodes.push_back(trackRecord.node);
			if (!paths.emplace(node, trackRecord.path).second)
				Refuse(
				    name, std::format(
				              "clip '{}' moves node '{}' by two tracks of one path", clip.name,
				              trackRecord.node));
			clip.tracks.push_back(ToTrack(trackRecord, node, name, clip.name));
			clip.duration = std::max(clip.duration, clip.tracks.back().times.back());
		}
		for (const EventRecord &eventRecord : clipRecord.events)
		{
			if (eventRecord.name.empty())
				Refuse(name, "clip '" + clip.name + "' has an event of no name");
			if (!std::isfinite(eventRecord.time) || eventRecord.time < 0.0f)
				Refuse(
				    name, std::format(
				              "clip '{}' has event '{}' at a time that is negative or not finite",
				              clip.name, eventRecord.name));
			clip.events.push_back({eventRecord.time, eventRecord.name});
			clip.duration = std::max(clip.duration, eventRecord.time);
		}
		// In time order, which playback crosses them in; events at one time keep the file's order.
		std::ranges::stable_sort(clip.events, {}, &ClipEvent::time);
		if (!(clip.duration > 0.0f))
			Refuse(name, "clip '" + clip.name + "' has no length: no key or event after its start");
		file.clips.push_back(std::move(clip));
	}
	return file;
}

std::size_t NodeIndex(const ClipFile &file, std::string_view node)
{
	for (std::size_t index = 0; index < file.nodes.size(); ++index)
		if (file.nodes[index] == node)
			return index;
	return file.nodes.size();
}

void RegisterClipFileLoader(Resource::ResourceManager &resources)
{
	// The archive reads the reflection tables, which the main thread may be registering into: a
	// decode on the worker keeps the bytes, in the one node name of a file not yet parsed, and the
	// finish on the main thread parses them over it.
	resources.RegisterLoader(
	    Resource::Loader<ClipFile>{
	        [](std::span<const std::byte> bytes, std::string_view)
	        {
		        auto file = std::make_unique<ClipFile>();
		        file->nodes.emplace_back(reinterpret_cast<const char *>(bytes.data()), bytes.size());
		        return file;
	        },
	        [](ClipFile &file, std::string_view path)
	        {
		        const std::string text = std::move(file.nodes.front());
		        file = ReadClipFile(text, path);
	        },
	        {}});
}
} // namespace Jettison::Animation
