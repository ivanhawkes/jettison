#include <jettison/animation/ClipFile.h>
#include <jettison/animation/Playback.h>
#include <jettison/animation/Rig.h>
#include <jettison/animation/RigLoader.h>

#include <doctest/doctest.h>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <jettison/jobs/IoWorker.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison::Animation;

namespace
{
// A root, and a child one metre up the Y axis carrying the one joint, with a clip that slides the
// child from x = 0 to x = 2 over two seconds.
Rig MakeSlidingRig()
{
	Rig rig;
	rig.nodes.push_back({});
	RigNode child;
	child.parent = 0;
	child.translation = {0.0f, 1.0f, 0.0f};
	rig.nodes.push_back(child);
	rig.joints.push_back({1, glm::mat4{1.0f}});
	rig.meshNodeIndex = 0;

	Track slide;
	slide.nodeIndex = 1;
	slide.path = TrackPath::Translation;
	slide.times = {0.0f, 2.0f};
	slide.values = {{0.0f, 1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f, 0.0f}};
	rig.clips.push_back({"slide", 2.0f, {slide}, {}});
	return rig;
}

// A clip of one second with events at its start, its middle and its end, and no tracks: the events
// alone are what is played.
Clip MakeEventClip()
{
	Clip clip;
	clip.name = "events";
	clip.duration = 1.0f;
	clip.events = {{0.0f, "start"}, {0.5f, "middle"}, {1.0f, "end"}};
	return clip;
}

// Advances the clip from time by each step in turn, and returns the names crossed and where it got.
std::pair<std::vector<std::string>, float> Play(
    const Clip &clip, float time, std::initializer_list<float> steps, bool looping)
{
	std::vector<std::string> names;
	for (const float step : steps)
	{
		std::vector<std::size_t> crossed;
		time = AdvanceClip(clip, time, step, looping, crossed);
		for (const std::size_t index : crossed)
			names.push_back(clip.events[index].name);
	}
	return {names, time};
}

using Names = std::vector<std::string>;
} // namespace

TEST_CASE("Rig: a linear clip is sampled between its keys")
{
	const Rig rig = MakeSlidingRig();
	CHECK(rig.HasSkin());
	CHECK(rig.HasAnimation("slide"));
	CHECK_FALSE(rig.HasAnimation("missing"));
	CHECK(rig.AnimationDuration("slide") == doctest::Approx(2.0f));
	CHECK(rig.AnimationDuration("missing") == 0.0f);

	std::vector<glm::mat4> joints;
	glm::mat4 meshNode{0.0f};
	rig.EvaluateSkin("slide", 0.5f, joints, meshNode);
	REQUIRE(joints.size() == 1);
	CHECK(joints[0][3].x == doctest::Approx(0.5f));
	CHECK(joints[0][3].y == doctest::Approx(1.0f));
	CHECK(meshNode == glm::mat4{1.0f});

	// The rest pose, for an empty clip name.
	rig.EvaluateSkin("", 0.5f, joints, meshNode);
	CHECK(joints[0][3].x == doctest::Approx(0.0f));

	CHECK_THROWS_AS(rig.EvaluateSkin("missing", 0.0f, joints, meshNode), std::runtime_error);
}

TEST_CASE("Playback: a rotation's phase stays inside one turn")
{
	RotationAnimationComponent rotation;
	rotation.rotationSpeed = 1.0f;
	AdvanceRotation(rotation, 7.0f);
	CHECK(rotation.angle == doctest::Approx(7.0f - 6.28318531f));
}

TEST_CASE("Playback: a looping clip wraps, a one-shot clip clamps, a paused clip holds")
{
	AnimationPlayerComponent looping;
	AdvancePlayer(looping, 2.0f, 2.5f);
	CHECK(looping.time == doctest::Approx(0.5f));

	AnimationPlayerComponent once;
	once.looping = false;
	AdvancePlayer(once, 2.0f, 2.5f);
	CHECK(once.time == doctest::Approx(2.0f));

	AnimationPlayerComponent paused;
	paused.playing = false;
	AdvancePlayer(paused, 2.0f, 1.0f);
	CHECK(paused.time == 0.0f);

	AnimationPlayerComponent empty;
	AdvancePlayer(empty, 0.0f, 1.0f);
	CHECK(empty.time == 0.0f);
}

TEST_CASE("Playback: a clip's events fire once each as it is played forwards and held at its end")
{
	const Clip clip = MakeEventClip();
	// The start's on the first advance from it, the middle's as it is reached, the end's as the end
	// is reached, and nothing more while the clip is held there.
	auto [names, time] = Play(clip, 0.0f, {0.25f, 0.25f, 0.25f, 0.25f, 0.25f}, false);
	CHECK(names == Names{"start", "middle", "end"});
	CHECK(time == 1.0f);

	// A frame that passes over two events fires both, in order.
	std::tie(names, time) = Play(clip, 0.25f, {0.9f}, false);
	CHECK(names == Names{"middle", "end"});
}

TEST_CASE("Playback: a clip played backwards meets its events in reverse, its start as it reaches it")
{
	const Clip clip = MakeEventClip();
	auto [names, time] = Play(clip, 1.0f, {-0.3f, -0.3f, -0.3f, -0.3f, -0.3f}, false);
	CHECK(names == Names{"end", "middle", "start"});
	CHECK(time == 0.0f);
}

TEST_CASE("Playback: a looping clip fires its end and its start as it wraps, and each event once a lap")
{
	const Clip clip = MakeEventClip();
	auto [names, time] = Play(clip, 0.75f, {0.5f}, true);
	CHECK(names == Names{"end", "start"});
	CHECK(time == doctest::Approx(0.25f));

	std::tie(names, time) = Play(clip, 0.25f, {-0.5f}, true);
	CHECK(names == Names{"start", "end"});
	CHECK(time == doctest::Approx(0.75f));

	// More than a lap in one advance: each event once, from where the clip was.
	std::tie(names, time) = Play(clip, 0.75f, {2.5f}, true);
	CHECK(names == Names{"end", "start", "middle"});
	CHECK(time == doctest::Approx(0.25f));
}

TEST_CASE("Playback: a time past a clip's end shows the end without its events")
{
	const Clip clip = MakeEventClip();
	auto [names, time] = Play(clip, std::numeric_limits<float>::max(), {0.1f, 0.1f}, false);
	CHECK(names.empty());
	CHECK(time == 1.0f);
	// Played back from there, it meets the end's event first, as if it had got there.
	std::tie(names, time) = Play(clip, time, {-0.1f}, false);
	CHECK(names == Names{"end"});

	// A paused frame, or a clip of no length, moves nothing.
	std::tie(names, time) = Play(clip, 0.0f, {0.0f}, false);
	CHECK(names.empty());
	Clip empty;
	std::tie(names, time) = Play(empty, 0.5f, {0.1f}, false);
	CHECK(names.empty());
	CHECK(time == 0.0f);
}

TEST_CASE("Clip file: tracks on named nodes, rotations in radians about X then Y then Z, and events")
{
	const ClipFile file = ReadClipFile(
	    R"(version: 1
clips:
  - name: swing
    tracks:
      - node: arm
        path: rotation
        times: [0, 1]
        values: [[0, 0, 0], [0, 0, 1.5707964]]
      - node: jaw
        path: translation
        interpolation: step
        times: [0, 0.5]
        values: [[0, 0, 0], [0, 2, 0]]
      - node: arm
        path: scale
        interpolation: cubic
        times: [0, 2]
        values: [[0, 0, 0], [1, 1, 1], [0, 0, 0], [0, 0, 0], [3, 3, 3], [0, 0, 0]]
    events:
      - {time: 1.5, name: late}
      - {time: 0.5, name: early}
)",
	    "animations/test.clip.yaml");
	CHECK(file.nodes == std::vector<std::string>{"arm", "jaw"});
	REQUIRE(file.clips.size() == 1);
	const Clip &clip = file.clips[0];
	CHECK(clip.duration == 2.0f);
	REQUIRE(clip.events.size() == 2);
	CHECK(clip.events[0].name == "early");
	CHECK(clip.events[1].name == "late");
	REQUIRE(clip.tracks.size() == 3);
	CHECK(clip.tracks[1].nodeIndex == NodeIndex(file, "jaw"));
	CHECK(NodeIndex(file, "missing") == file.nodes.size());

	// Halfway through the swing, an eighth of a turn about Z: +X turned towards +Y.
	const glm::vec4 half = Sample(clip.tracks[0], 0.5f);
	const glm::vec3 turned = glm::quat{half.w, half.x, half.y, half.z} * glm::vec3{1.0f, 0.0f, 0.0f};
	CHECK(turned.x == doctest::Approx(0.70710678f));
	CHECK(turned.y == doctest::Approx(0.70710678f));
	// A step holds its key; a cubic track with flat tangents eases from one value to the next.
	CHECK(Sample(clip.tracks[1], 0.49f).y == 0.0f);
	CHECK(Sample(clip.tracks[1], 0.5f).y == 2.0f);
	CHECK(Sample(clip.tracks[2], 1.0f).x == doctest::Approx(2.0f));
	CHECK(Sample(clip.tracks[2], 0.5f).x < 1.5f);
}

TEST_CASE("Clip file: a track or an event the player could not play is refused, naming the file")
{
	const auto refused = [](const std::string &clips)
	{
		CHECK_THROWS_WITH_AS(
		    ReadClipFile("version: 1\nclips:\n" + clips, "animations/bad.clip.yaml"),
		    doctest::Contains("animations/bad.clip.yaml"), Jettison::Serialise::FormatError);
	};
	const std::string track =
	    "  - name: a\n    tracks:\n      - {node: arm, path: translation, times: [0, 1], values: [[0, "
	    "0, 0], [1, 0, 0]]}\n";
	// Two clips of one name, a track on no node, two of one path on a node.
	refused(track + track);
	refused(
	    "  - name: a\n    tracks:\n      - {path: scale, times: [0, 1], values: [[1, 1, 1], [2, 2, "
	    "2]]}\n");
	refused(
	    track + "      - {node: arm, path: translation, times: [0, 2], values: [[0, 0, 0], [1, 0, "
	            "0]]}\n");
	// Times that fall, a value short, a cubic rotation.
	refused(
	    "  - name: a\n    tracks:\n      - {node: arm, path: scale, times: [1, 0], values: [[1, 1, "
	    "1], [2, 2, 2]]}\n");
	refused(
	    "  - name: a\n    tracks:\n      - {node: arm, path: scale, times: [0, 1], values: [[1, 1, "
	    "1]]}\n");
	refused(
	    "  - name: a\n    tracks:\n      - {node: arm, path: rotation, interpolation: cubic, "
	    "times: [0], values: [[0, 0, 0], [0, 0, 0], [0, 0, 0]]}\n");
	// An event of no name, one before the start, and a clip with nothing after its start.
	refused(track + "    events:\n      - {time: 0.5}\n");
	refused(track + "    events:\n      - {time: -1, name: early}\n");
	refused("  - name: a\n    events:\n      - {time: 0, name: now}\n");
}

TEST_CASE("Clip file: the pad clamp's clips, through the resource manager")
{
	using namespace Jettison;
	Vfs::FileSystem assets;
	assets.AddMount({"base", JETTISON_ASSET_SOURCE_DIR});
	Jobs::IoWorker worker;
	Resource::ResourceManager resources{assets, worker, {.hotReload = false, .pollInterval = {}}};
	RegisterClipFileLoader(resources);

	const auto clamp = resources.Load<ClipFile>("animations/pad-clamp.clip.yaml");
	worker.WaitIdle();
	resources.Update();
	const ClipFile *pClamp = resources.TryGet(clamp);
	REQUIRE(pClamp != nullptr);
	CHECK(pClamp->nodes == std::vector<std::string>{"clamp", "post", "arm", "jaw"});
	const Clip *close = FindClip(pClamp->clips, "close");
	const Clip *open = FindClip(pClamp->clips, "open");
	REQUIRE(close != nullptr);
	REQUIRE(open != nullptr);
	REQUIRE(close->events.size() == 1);
	CHECK(close->events[0].name == "clamp.closed");
	CHECK(close->events[0].time == close->duration);
	REQUIRE(open->events.size() == 1);
	CHECK(open->events[0].name == "clamp.released");
	CHECK(open->events[0].time == 0.0f);
	resources.Release(clamp);
}

TEST_CASE("Rig loader: a glTF file's hierarchy, skin and clips, through the resource manager")
{
	using namespace Jettison;
	Vfs::FileSystem assets;
	assets.AddMount({"base", JETTISON_ASSET_SOURCE_DIR});
	Jobs::IoWorker worker;
	Resource::ResourceManager resources{assets, worker, {.hotReload = false, .pollInterval = {}}};
	RegisterRigLoader(resources);

	const auto fox = resources.Load<Rig>("models/Fox.glb");
	const auto avocado = resources.Load<Rig>("models/Avocado.glb");
	const auto missing = resources.Load<Rig>("models/Nothing.glb");
	worker.WaitIdle();
	resources.Update();

	const Rig *pFox = resources.TryGet(fox);
	REQUIRE(pFox != nullptr);
	CHECK(pFox->HasSkin());
	CHECK(pFox->HasAnimation("Walk"));
	CHECK(pFox->AnimationDuration("Walk") > 0.0f);
	std::vector<glm::mat4> joints;
	glm::mat4 meshNode{1.0f};
	pFox->EvaluateSkin("Walk", 0.25f, joints, meshNode);
	CHECK(joints.size() == pFox->joints.size());

	const Rig *pAvocado = resources.TryGet(avocado);
	REQUIRE(pAvocado != nullptr);
	CHECK_FALSE(pAvocado->HasSkin());
	CHECK(pAvocado->HasSceneGraph());
	CHECK(resources.StatusOf(missing) == Resource::Status::Failed);

	// Bytes that are not glTF fail with the file named, as a resource's error.
	const std::vector<std::byte> junk(16, std::byte{0x2a});
	CHECK_THROWS_WITH_AS(
	    ReadGltfRig(junk, "models/junk.glb"), doctest::Contains("models/junk.glb"), std::runtime_error);
	resources.Release(fox);
	resources.Release(avocado);
	resources.Release(missing);
}
