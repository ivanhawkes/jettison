#include <jettison/animation/Playback.h>
#include <jettison/animation/Rig.h>
#include <jettison/animation/RigLoader.h>

#include <doctest/doctest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

#include <jettison/jobs/IoWorker.h>
#include <jettison/resource/ResourceManager.h>
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
	rig.clips.push_back({"slide", 2.0f, {slide}});
	return rig;
}
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
