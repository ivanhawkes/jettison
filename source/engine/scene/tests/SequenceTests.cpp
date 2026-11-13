#include <jettison/scene/Sequence.h>

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <jettison/jobs/IoWorker.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/SceneSystems.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison;
using namespace Jettison::Scene;

namespace
{
// Two shots round a pad, the first blended in from the game's camera, the second cut to and looking
// at the ship, and the camera handed back over the last second.
constexpr const char *kDock = R"(version: 1
letterbox: 0.1
blendOut: 1
fadeColor: [0, 0, 0]
fade:
  - {time: 1.5, amount: 0}
  - {time: 2, amount: 1}
  - {time: 2.5, amount: 0}
shots:
  - duration: 2
    blend: 1
    frame: pad
    rail: [[10, 2, 0], [0, 2, -10]]
    easing: linear
    lookAt: [0, 0, 0]
    fov: [60, 40]
  - duration: 3
    frame: pad
    rail: [[0, 20, 0]]
    look: ship
    lookAt: [0, 1, 0]
events:
  - {time: 2, name: cut}
  - {time: 0.5, name: early}
)";

std::string Refusal(const std::string &text)
{
	try
	{
		(void)ReadSequence(text, "cinematics/test.sequence.yaml");
	}
	catch (const Serialise::FormatError &error)
	{
		return error.what();
	}
	return {};
}

// A sequence of one shot in the system's frame, from the text of the shot's fields.
std::string OneShot(const std::string &shot, const std::string &rest = {})
{
	return "version: 1\n" + rest + "shots:\n  - " + shot + "\n";
}

glm::dvec3 Forward(const CameraPose &pose)
{
	return pose.orientation * glm::dvec3{0.0, 0.0, -1.0};
}

void CheckNear(const glm::dvec3 &a, const glm::dvec3 &b, double within = 1e-9)
{
	CHECK(glm::length(a - b) < within);
}
} // namespace

TEST_CASE("Sequence: a file's shots, roles and timeline are read as written")
{
	const Sequence sequence = ReadSequence(kDock, "cinematics/dock.sequence.yaml");
	CHECK(sequence.roles == std::vector<std::string>{"pad", "ship"});
	REQUIRE(sequence.shots.size() == 2);
	CHECK(sequence.duration == 5.0);
	CHECK(sequence.blendOut == 1.0);
	CHECK(sequence.letterbox == 0.1f);
	CHECK(sequence.shots[0].frame == 0);
	CHECK(sequence.shots[0].look == kNoRole);
	CHECK(sequence.shots[0].easing == Math::Easing::Linear);
	CHECK(sequence.shots[0].fovFrom == 60.0f);
	CHECK(sequence.shots[0].fovTo == 40.0f);
	CHECK(sequence.shots[1].look == 1);
	CHECK(sequence.shots[1].easing == Math::Easing::InOutCubic);
	CHECK(sequence.shots[1].fovFrom == 0.0f);
	// The events in time order, on a timeline as long as the sequence.
	REQUIRE(sequence.timeline.events.size() == 2);
	CHECK(sequence.timeline.events[0].name == "early");
	CHECK(sequence.timeline.events[1].name == "cut");
	CHECK(sequence.timeline.duration == 5.0f);
}

TEST_CASE("Sequence: what a sequence cannot be is refused, naming the file")
{
	const std::string rail = "duration: 2\n    rail: [[0, 0, 0]]";
	CHECK(Refusal(OneShot(rail)).empty());
	CHECK(Refusal("version: 1\nshots: []\n").find("it has no shots") != std::string::npos);
	CHECK(Refusal(OneShot("rail: [[0, 0, 0]]")).find("shot 1 has no duration") != std::string::npos);
	CHECK(
	    Refusal(OneShot("duration: 2\n    blend: 3\n    rail: [[0, 0, 0]]"))
	        .find("blends in for longer than it lasts") != std::string::npos);
	CHECK(Refusal(OneShot("duration: 2")).find("shot 1 has no rail") != std::string::npos);
	CHECK(
	    Refusal(OneShot("duration: 2\n    rail: [[0, 0, 0], [1, 0, 0], [1, 0, 0]]"))
	        .find("rail point 3 twice in a row") != std::string::npos);
	CHECK(
	    Refusal(OneShot(rail + "\n    fov: [40, 30, 20]")).find("field of view") != std::string::npos);
	CHECK(Refusal(OneShot(rail + "\n    fov: [180]")).find("field of view") != std::string::npos);
	CHECK(Refusal(OneShot(rail, "blendOut: 3\n")).find("blend out is longer") != std::string::npos);
	CHECK(Refusal(OneShot(rail, "letterbox: 0.5\n")).find("letterbox") != std::string::npos);
	CHECK(Refusal(OneShot(rail, "fadeColor: [-1, 0, 0]\n")).find("not a colour") != std::string::npos);
	CHECK(
	    Refusal(OneShot(rail, "fade: [{time: 1, amount: 0}, {time: 1, amount: 1}]\n"))
	        .find("fade's times are not rising") != std::string::npos);
	CHECK(
	    Refusal(OneShot(rail, "fade: [{time: 3, amount: 0}]\n"))
	        .find("fade's times are not rising within it") != std::string::npos);
	CHECK(
	    Refusal(OneShot(rail, "fade: [{time: 1, amount: 2}]\n")).find("amount other than") !=
	    std::string::npos);
	CHECK(
	    Refusal(OneShot(rail, "events: [{time: 1, name: ''}]\n")).find("event of no name") !=
	    std::string::npos);
	CHECK(
	    Refusal(OneShot(rail, "events: [{time: 2.5, name: late}]\n"))
	        .find("event 'late' is not within it") != std::string::npos);
	CHECK(Refusal(OneShot(rail)).empty());
	CHECK(
	    Refusal(OneShot("duration: 2\n    rail: [[0, 0, 0]]", "letterbox: 0.6\n"))
	        .find("Sequence 'cinematics/test.sequence.yaml'") != std::string::npos);
	CHECK_THROWS_AS(
	    ReadSequence(OneShot(rail + "\n    easing: sideways"), "cinematics/test.sequence.yaml"),
	    Serialise::FormatError);
}

TEST_CASE("Sequence: a rail is flown at an even speed through its points")
{
	// Three points on a line, unevenly spaced: the middle is a quarter of the way along. Between its
	// samples the spline's parameter stands in for its length, which on 20 m is a quarter of a
	// millimetre out at most.
	const Sequence straight =
	    ReadSequence(OneShot("duration: 1\n    rail: [[0, 0, 0], [5, 0, 0], [20, 0, 0]]"), "straight");
	const SequenceShot &line = straight.shots.front();
	CHECK(line.lengths.back() == doctest::Approx(20.0));
	for (const double share : {0.0, 0.1, 0.25, 0.5, 0.8, 1.0})
		CheckNear(RailPoint(line, share), {20.0 * share, 0.0, 0.0}, 3e-4);

	// Round a corner: the rail passes through every point, never further from the next than the
	// points are, and covers equal lengths in equal shares.
	const Sequence corner = ReadSequence(
	    OneShot("duration: 1\n    rail: [[0, 0, 0], [10, 0, 0], [10, 0, -10], [0, 3, -12]]"), "corner");
	const SequenceShot &curve = corner.shots.front();
	for (const glm::dvec3 &point : curve.rail)
	{
		double nearest = 1e9;
		for (int sample = 0; sample <= 4000; ++sample)
			nearest = std::min(nearest, glm::length(RailPoint(curve, sample / 4000.0) - point));
		CHECK(nearest < 0.01);
	}
	CheckNear(RailPoint(curve, 0.0), curve.rail.front());
	CheckNear(RailPoint(curve, 1.0), curve.rail.back(), 1e-9);
	constexpr int kSteps = 100;
	const double step = curve.lengths.back() / kSteps;
	for (int index = 0; index < kSteps; ++index)
	{
		const double covered = glm::length(
		    RailPoint(curve, (index + 1) / double{kSteps}) - RailPoint(curve, index / double{kSteps}));
		CHECK(covered == doctest::Approx(step).epsilon(0.01));
	}
	// A rail of one point holds the camera there.
	const Sequence held = ReadSequence(OneShot("duration: 1\n    rail: [[1, 2, 3]]"), "held");
	CheckNear(RailPoint(held.shots.front(), 0.7), {1.0, 2.0, 3.0});
}

TEST_CASE("Sequence: the game's camera blends in and out, shots cut and blend, and the bars follow")
{
	const Sequence sequence = ReadSequence(kDock, "cinematics/dock.sequence.yaml");
	// The pad at rest at the origin, the ship 2 m along its -Z.
	const std::vector<glm::dmat4> roles{
	    glm::dmat4{1.0}, glm::translate(glm::dmat4{1.0}, glm::dvec3{0.0, 0.0, -2.0})};
	const CameraPose game{{50.0, 50.0, 50.0}, LookRotation(glm::dvec3{-1.0, -1.0, -1.0})};

	// At the start it is the game's camera, all of it.
	SequenceView view = SampleSequence(sequence, 0.0, roles, game, 45.0f);
	CheckNear(view.pose.position, game.position);
	CHECK(view.weight == 0.0f);
	CHECK(view.letterbox == 0.0f);
	CHECK(view.verticalFovDegrees == 45.0f);
	// Half-way through the blend, half of each: the bars at half their height.
	view = SampleSequence(sequence, 0.5, roles, game, 45.0f);
	CHECK(view.weight == doctest::Approx(0.5f));
	CHECK(view.letterbox == doctest::Approx(0.05f));
	// Past it, the shot's own: on the rail, linear, looking at the pad, its field of view narrowing.
	view = SampleSequence(sequence, 1.5, roles, game, 45.0f);
	CHECK(view.weight == 1.0f);
	CHECK(view.letterbox == 0.1f);
	const glm::dvec3 onRail = RailPoint(sequence.shots[0], 0.75);
	CheckNear(view.pose.position, onRail);
	CheckNear(Forward(view.pose), glm::normalize(-onRail));
	CHECK(view.verticalFovDegrees == doctest::Approx(45.0f));
	// The second shot is cut to: overhead, looking down at the ship's point a metre above it, with
	// the game's field of view, since it sets none.
	view = SampleSequence(sequence, 2.0, roles, game, 45.0f);
	CheckNear(view.pose.position, {0.0, 20.0, 0.0});
	CheckNear(Forward(view.pose), glm::normalize(glm::dvec3{0.0, -19.0, -2.0}));
	CHECK(view.verticalFovDegrees == 45.0f);
	// The fade's keys, linear between them.
	CHECK(SampleSequence(sequence, 1.75, roles, game, 45.0f).fade == doctest::Approx(0.5f));
	CHECK(SampleSequence(sequence, 2.0, roles, game, 45.0f).fade == 1.0f);
	CHECK(SampleSequence(sequence, 3.0, roles, game, 45.0f).fade == 0.0f);
	// Handed back over the last second: half-way at 4.5 s, and the game's camera exactly at the end.
	view = SampleSequence(sequence, 4.5, roles, game, 45.0f);
	CHECK(view.weight == doctest::Approx(0.5f));
	CheckNear(view.pose.position, (game.position + glm::dvec3{0.0, 20.0, 0.0}) * 0.5, 1e-9);
	view = SampleSequence(sequence, 5.0, roles, game, 45.0f);
	CHECK(view.weight == 0.0f);
	CHECK(view.pose.position == game.position);
	CHECK(view.pose.orientation == game.orientation);

	// A shot blended to rather than cut to starts from where the one before ended.
	const Sequence blended = ReadSequence(
	    OneShot(
	        "duration: 1\n    rail: [[0, 0, 10]]\n  - duration: 2\n    blend: 1\n    rail: [[10, 0, "
	        "0]]"),
	    "blended");
	const CameraPose held = SampleSequence(blended, 0.99, {}, game, 45.0f).pose;
	CheckNear(SampleSequence(blended, 1.0, {}, game, 45.0f).pose.position, held.position);
	CheckNear(SampleSequence(blended, 1.5, {}, game, 45.0f).pose.position, glm::dvec3{5.0, 0.0, 5.0});
	CheckNear(SampleSequence(blended, 2.0, {}, game, 45.0f).pose.position, {10.0, 0.0, 0.0});
}

TEST_CASE("Sequence: a rail in a moving frame rides it, far from the origin, and looks at another")
{
	const Sequence sequence = ReadSequence(kDock, "cinematics/dock.sequence.yaml");
	// The pad on a station 1e8 m out, turned a quarter about +Y and scaled, as a station drawn by a
	// model may be: the scale is not the rail's.
	const glm::dvec3 station{1.0e8, -3.0e6, 4.0e7};
	const glm::dmat4 pad = glm::scale(
	    glm::rotate(
	        glm::translate(glm::dmat4{1.0}, station), glm::half_pi<double>(),
	        glm::dvec3{0.0, 1.0, 0.0}),
	    glm::dvec3{12.0});
	const glm::dmat4 ship = glm::translate(glm::dmat4{1.0}, station + glm::dvec3{0.0, 0.0, 30.0});
	const std::vector<glm::dmat4> roles{pad, ship};
	const CameraPose game{station, {1.0, 0.0, 0.0, 0.0}};
	const SequenceView view = SampleSequence(sequence, 1.5, roles, game, 45.0f);
	// The rail's point turned a quarter about +Y: x to -z, z to x.
	const glm::dvec3 point = RailPoint(sequence.shots[0], 0.75);
	CheckNear(view.pose.position, station + glm::dvec3{point.z, point.y, -point.x}, 1e-6);
	// Overhead, 20 m above the pad, looking at the ship 30 m along +Z of it.
	const SequenceView overhead = SampleSequence(sequence, 3.0, roles, game, 45.0f);
	CheckNear(overhead.pose.position, station + glm::dvec3{0.0, 20.0, 0.0}, 1e-6);
	CheckNear(Forward(overhead.pose), glm::normalize(glm::dvec3{0.0, -19.0, 30.0}), 1e-9);
}

TEST_CASE("Sequence: a player plays its file over the camera's rig, hears its events once, and ends")
{
	const std::filesystem::path root = std::filesystem::temp_directory_path() /
	                                   ("jettison-sequence-tests-" + std::to_string(getpid()));
	std::filesystem::create_directories(root / "cinematics");
	std::ofstream{root / "cinematics/dock.sequence.yaml"} << kDock;
	{
		Vfs::FileSystem files;
		files.AddMount({"base", root});
		Jobs::IoWorker worker;
		Resource::ResourceManager resources{files, worker, {.hotReload = false, .pollInterval = {}}};
		RegisterSequenceLoader(resources);

		entt::registry registry;
		const entt::entity pad = registry.create();
		registry.emplace<WorldTransformComponent>(pad);
		const entt::entity ship = registry.create();
		registry.emplace<WorldTransformComponent>(
		    ship, glm::translate(glm::dmat4{1.0}, glm::dvec3{0.0, 0.0, -2.0}));
		const entt::entity camera = registry.create();
		registry.emplace<CameraComponent>(camera).verticalFovDegrees = 50.0f;
		const CameraPose game{{50.0, 50.0, 50.0}, LookRotation(glm::dvec3{-1.0})};
		const auto rig = [&]
		{ registry.emplace_or_replace<CameraPoseComponent>(camera, game.position, game.orientation); };
		auto &player = registry.emplace<SequencePlayerComponent>(camera);
		player.file = "cinematics/dock.sequence.yaml";
		player.roles = {{"pad", pad}, {"ship", ship}};

		SequenceSystem system{resources};
		std::vector<SequenceEventFired> events;
		// Until it is in, the player waits, and the camera is the rig's.
		rig();
		system.Update(registry, 0.1, &events);
		CHECK(registry.get<SequencePlayerComponent>(camera).time == 0.0);
		CHECK(registry.get<CameraPoseComponent>(camera).position == game.position);
		worker.WaitIdle();
		resources.Update();

		// Its first frame is its start; each after it a tenth of a second on, the rig's pose under it.
		for (int frame = 0; frame <= 15; ++frame)
		{
			rig();
			system.Update(registry, 0.1, &events);
		}
		CHECK(registry.get<SequencePlayerComponent>(camera).time == doctest::Approx(1.5));
		const auto &pose = registry.get<CameraPoseComponent>(camera);
		CheckNear(pose.position, RailPoint(ReadSequence(kDock, "dock").shots[0], 0.75), 1e-5);
		CHECK(pose.verticalFovDegrees == doctest::Approx(45.0f));
		CHECK(registry.get<SequencePlayerComponent>(camera).letterbox == 0.1f);
		REQUIRE(events.size() == 1);
		CHECK(events[0].camera == camera);
		CHECK(events[0].name == "early");

		// To its end: the cut heard once, the camera the rig's again with its own field of view, and
		// the player gone.
		for (int frame = 0; frame < 60 && registry.all_of<SequencePlayerComponent>(camera); ++frame)
		{
			rig();
			system.Update(registry, 0.1, &events);
		}
		CHECK_FALSE(registry.all_of<SequencePlayerComponent>(camera));
		REQUIRE(events.size() == 2);
		CHECK(events[1].name == "cut");
		CHECK(registry.get<CameraPoseComponent>(camera).position == game.position);
		CHECK(registry.get<CameraPoseComponent>(camera).verticalFovDegrees == 50.0f);

		// A role bound to no entity yet is waited for before the start, and stops it once it is gone.
		auto &waiting = registry.emplace<SequencePlayerComponent>(camera);
		waiting.file = "cinematics/dock.sequence.yaml";
		waiting.roles = {{"pad", pad}, {"ship", entt::null}};
		for (int frame = 0; frame < 3; ++frame)
			system.Update(registry, 0.1, &events);
		CHECK_FALSE(registry.get<SequencePlayerComponent>(camera).started);
		registry.get<SequencePlayerComponent>(camera).roles["ship"] = ship;
		system.Update(registry, 0.1, &events);
		system.Update(registry, 0.1, &events);
		CHECK(registry.get<SequencePlayerComponent>(camera).time == doctest::Approx(0.1));
		registry.destroy(ship);
		system.Update(registry, 0.1, &events);
		CHECK_FALSE(registry.all_of<SequencePlayerComponent>(camera));

		// A role not bound at all, or a file that is not there, stops a player at once, leaving the
		// camera to its rig.
		auto &unbound = registry.emplace<SequencePlayerComponent>(camera);
		unbound.file = "cinematics/dock.sequence.yaml";
		unbound.roles = {{"pad", pad}};
		rig();
		system.Update(registry, 0.1, &events);
		CHECK_FALSE(registry.all_of<SequencePlayerComponent>(camera));
		registry.emplace<SequencePlayerComponent>(camera).file = "cinematics/none.sequence.yaml";
		system.Update(registry, 0.1, &events);
		worker.WaitIdle();
		resources.Update();
		system.Update(registry, 0.1, &events);
		CHECK_FALSE(registry.all_of<SequencePlayerComponent>(camera));
		CHECK(registry.get<CameraPoseComponent>(camera).position == game.position);
	}
	std::filesystem::remove_all(root);
}
