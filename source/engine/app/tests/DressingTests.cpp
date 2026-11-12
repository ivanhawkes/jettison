#include "../src/Dressing.h"
#include "../src/Stations.h"

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <jettison/ai/Steering.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/scene/Components.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/spatial/SystemIndices.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;

Celestial::StarSystem System(const std::string &file)
{
	std::ifstream in{std::string{JETTISON_ASSET_SOURCE_DIR} + "/systems/" + file};
	std::stringstream text;
	text << in.rdbuf();
	return Celestial::ReadStarSystem(text.str());
}

// The sample's stations and hulls, as the shell has them, and a scene with Highport's entity in it.
struct Dressed
{
	Sim::SimWorld world{kStep, 5};
	Physics::PhysicsWorld &physics{Setup(world)};
	Spatial::SystemIndices &indices{Spatial::SystemIndices::Attach(world)};
	Ai::Steering &steering{Ai::Steering::Attach(world, physics, indices)};
	Dock::Docks &docks{Dock::Docks::Attach(world, physics)};
	std::vector<Celestial::StarSystem> systems{System("halcyon.yaml"), System("vesper.yaml")};
	Vfs::FileSystem files;
	entt::registry scene;
	entt::entity station{scene.create()};

	Dressed()
	{
		for (const Celestial::StarSystem &system : systems)
			physics.DefineStarSystem(system);
		files.AddMount({"base", JETTISON_ASSET_SOURCE_DIR, false});
		Sim::DefinitionRegistry definitions{files};
		definitions.Load<Dock::HullDefinition>(Dock::kHullDirectory, {Dock::kDefinitionVersion});
		definitions.Load<Dock::StationLayout>(Dock::kStationDirectory, {Dock::kDefinitionVersion});
		docks.Define(definitions);
		scene.emplace<Scene::CelestialBodyComponent>(station, "Ostra Highport");
		scene.emplace<Scene::TransformComponent>(station);
	}

	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InSystem>();
		return Physics::PhysicsWorld::Attach(world);
	}

	const Dock::StationLayout &Layout() const
	{
		return *docks.FindLayout("ostra-highport");
	}
};

// The scene's transform as a matrix: translated, turned about X, then Y, then Z, and scaled.
glm::dmat4 Local(const Scene::TransformComponent &transform)
{
	glm::dmat4 matrix = glm::translate(glm::dmat4{1.0}, transform.translation);
	matrix = glm::rotate(matrix, double{transform.rotation.x}, {1.0, 0.0, 0.0});
	matrix = glm::rotate(matrix, double{transform.rotation.y}, {0.0, 1.0, 0.0});
	matrix = glm::rotate(matrix, double{transform.rotation.z}, {0.0, 0.0, 1.0});
	return glm::scale(matrix, glm::dvec3{transform.scale});
}

// An entity's place relative to the root it is under.
glm::dmat4 Under(const entt::registry &scene, entt::entity entity, entt::entity root)
{
	glm::dmat4 matrix{1.0};
	while (entity != entt::null && entity != root)
	{
		matrix = Local(scene.get<Scene::TransformComponent>(entity)) * matrix;
		const auto *parent = scene.try_get<Scene::ParentComponent>(entity);
		entity = parent ? parent->entity : entt::null;
	}
	return matrix;
}

Scene::Layer LayerOf(const entt::registry &scene, entt::entity entity)
{
	for (; entity != entt::null;)
	{
		if (const auto *layer = scene.try_get<Scene::LayerComponent>(entity))
			return layer->layer;
		const auto *parent = scene.try_get<Scene::ParentComponent>(entity);
		entity = parent ? parent->entity : entt::null;
	}
	return Scene::Layer::Environment;
}

// The pads' entities: the station's children placed where a pad is.
std::vector<entt::entity> PadNodes(const Dressed &dressed)
{
	std::vector<entt::entity> nodes;
	for (const Dock::PadLayout &pad : dressed.Layout().pads)
		for (const auto [entity, parent, transform] :
		     dressed.scene.view<const Scene::ParentComponent, const Scene::TransformComponent>().each())
			if (parent.entity == dressed.station && transform.translation == pad.position &&
			    !dressed.scene.all_of<Scene::MeshComponent>(entity))
				nodes.push_back(entity);
	return nodes;
}
} // namespace

TEST_CASE(
    "Dressing: a layout is drawn under the station as it collides, each pad turned to its heading")
{
	Dressed dressed;
	const Dock::StationLayout &layout = dressed.Layout();
	const App::StationDressing dressing =
	    App::DressStation(dressed.scene, dressed.station, layout, dressed.docks);
	CHECK(dressing.station == dressed.station);
	CHECK(dressing.body == "Ostra Highport");
	const entt::registry &scene = dressed.scene;

	// Every deck and pier a box of its size at its place, the hub in the hull's plating and the rest
	// in the deck's.
	std::size_t structure = 0;
	for (const auto [entity, parent, transform, mesh] :
	     scene
	         .view<
	             const Scene::ParentComponent, const Scene::TransformComponent,
	             const Scene::MeshComponent>()
	         .each())
	{
		CHECK(scene.all_of<Scene::BuiltComponent>(entity));
		if (parent.entity != dressed.station)
			continue;
		for (const std::vector<Dock::Box> *boxes : {&layout.decks, &layout.piers})
			for (const Dock::Box &box : *boxes)
				if (transform.translation == box.centre &&
				    glm::dvec3{transform.scale} == box.size * 0.5)
				{
					CHECK(mesh.shape == Scene::Shape::Box);
					CHECK(
					    mesh.material == (&box == &layout.decks.front()
					                          ? "materials/hull.material.yaml"
					                          : "materials/deck.material.yaml"));
					++structure;
				}
	}
	CHECK(structure == layout.decks.size() + layout.piers.size());

	// Each pad's entity turned as the scene turns it, its length along its heading and its plate of
	// its class's size.
	const std::vector<entt::entity> nodes = PadNodes(dressed);
	REQUIRE(nodes.size() == layout.pads.size());
	for (std::size_t index = 0; index < nodes.size(); ++index)
	{
		const Dock::PadLayout &pad = layout.pads[index];
		const glm::dmat4 turn = Under(scene, nodes[index], dressed.station);
		CHECK(glm::distance(glm::dvec3{turn * glm::dvec4{0.0, 0.0, -1.0, 0.0}}, pad.heading) < 1.0e-5);
		CHECK(glm::distance(glm::dvec3{turn * glm::dvec4{0.0, 1.0, 0.0, 0.0}}, pad.normal) < 1.0e-5);
		std::size_t plates = 0;
		for (const auto [entity, parent, transform] :
		     scene.view<const Scene::ParentComponent, const Scene::TransformComponent>().each())
			if (parent.entity == nodes[index] && transform.translation == glm::dvec3{0.0} &&
			    scene.all_of<Scene::MeshComponent>(entity))
			{
				CHECK(transform.scale.x == doctest::Approx(Dock::PadSize(pad.padClass).x * 0.5f));
				CHECK(transform.scale.z == doctest::Approx(Dock::PadSize(pad.padClass).y * 0.5f));
				++plates;
			}
		CHECK(plates == 1);
	}
}

TEST_CASE(
    "Dressing: every pad painted and floodlit at Highport, a hundred and four lights within the "
    "cluster range, the paint and the lamps in the effects layer")
{
	Dressed dressed;
	const Dock::StationLayout &layout = dressed.Layout();
	const App::StationDressing dressing =
	    App::DressStation(dressed.scene, dressed.station, layout, dressed.docks);
	const entt::registry &scene = dressed.scene;
	const std::vector<entt::entity> nodes = PadNodes(dressed);
	REQUIRE(nodes.size() == 18);

	// Six poles an L pad, eight an XL and four an S, each a lamp and a floodlight: 12 x 6 + 2 x 8 +
	// 4 x 4.
	REQUIRE(dressing.lamps.size() == layout.pads.size());
	for (std::size_t index = 0; index < layout.pads.size(); ++index)
	{
		const std::size_t poles = dressing.lamps[index].size();
		switch (layout.pads[index].padClass)
		{
			case Dock::PadClass::S: CHECK(poles == 4); break;
			case Dock::PadClass::M: CHECK(poles == 4); break;
			case Dock::PadClass::L: CHECK(poles == 6); break;
			case Dock::PadClass::XL: CHECK(poles == 8); break;
		}
		for (const entt::entity lamp : dressing.lamps[index])
		{
			CHECK(LayerOf(scene, lamp) == Scene::Layer::Effects);
			CHECK(scene.get<Scene::MeshComponent>(lamp).material == "materials/lamp.material.yaml");
			CHECK(
			    scene.get<Scene::MeshComponent>(lamp).color ==
			    App::LampColour(Dock::PadState::Free, layout.pads[index].display));
		}
	}
	CHECK(dressing.floodlights == 104);

	// Every light a spot from the station, all of them reaching no further than 900 m from its centre,
	// so a camera at the station sees them within the 2 km cluster range with room to spare; each
	// lights the point it is aimed at, under its range.
	std::size_t lights = 0;
	double furthest = 0.0;
	for (const auto [entity, light] : scene.view<const Scene::LightComponent>().each())
	{
		CHECK(light.type == Scene::LightType::Spot);
		CHECK(light.range > 0.0f);
		CHECK(light.intensity > 0.0f);
		CHECK(LayerOf(scene, entity) == Scene::Layer::Environment);
		const glm::dvec3 at{Under(scene, entity, dressed.station)[3]};
		furthest = std::max(furthest, glm::length(at) + double{light.range});
		++lights;
	}
	CHECK(lights == 104);
	CHECK(furthest < 900.0);

	// The paint: thin boxes resting on each pad's plate, in the effects layer, and nothing of the
	// effects layer that is not paint or a lamp.
	std::size_t paint = 0;
	for (const auto [entity, mesh] : scene.view<const Scene::MeshComponent>().each())
	{
		if (LayerOf(scene, entity) != Scene::Layer::Effects)
			continue;
		const bool lamp = mesh.material == "materials/lamp.material.yaml";
		CHECK((lamp || mesh.shape == Scene::Shape::Box));
		if (!lamp)
		{
			CHECK(mesh.material.empty());
			const auto &transform = scene.get<Scene::TransformComponent>(entity);
			CHECK(transform.translation.y > 0.0);
			CHECK(transform.scale.y < 0.05f);
			++paint;
		}
	}
	// Each pad's outline (4), chevrons (6), keep-clear box with its diagonals (6), centre line (1) and
	// its number's bar segments, by digit from 0 to 9; and a line down each road off the pads, the
	// 44 edges of Highport's roads less the twelve from an apron to its hatch.
	const std::array<std::size_t, 10> segments{6, 2, 5, 5, 4, 5, 6, 3, 7, 6};
	CHECK(dressing.roads == 32);
	CHECK(dressing.parked == 0);
	std::size_t expected = dressing.roads;
	for (unsigned number = 1; number <= layout.pads.size(); ++number)
	{
		expected += 4 + 6 + 6 + 1;
		for (const char digit : std::to_string(number))
			expected += segments[static_cast<std::size_t>(digit - '0')];
	}
	CHECK(paint == expected);
}

TEST_CASE("Dressing: the display wing shows a liner, a frigate and four fighters, as dressing")
{
	Dressed dressed;
	const Dock::StationLayout &layout = dressed.Layout();
	App::DressStation(dressed.scene, dressed.station, layout, dressed.docks);
	const entt::registry &scene = dressed.scene;
	const std::vector<entt::entity> nodes = PadNodes(dressed);

	// A proxy's root is what is under a pad and neither draws nor lights: one on each display pad,
	// resting on it, and none on a pad a ship lands on.
	std::multiset<std::string> shown;
	for (std::size_t index = 0; index < nodes.size(); ++index)
	{
		std::size_t roots = 0;
		for (const auto [entity, parent, transform] :
		     scene.view<const Scene::ParentComponent, const Scene::TransformComponent>().each())
			if (parent.entity == nodes[index] &&
			    !scene.any_of<Scene::MeshComponent, Scene::LightComponent>(entity))
			{
				const Dock::HullDefinition *hull = dressed.docks.FindHull(layout.pads[index].ship);
				REQUIRE(hull);
				CHECK(transform.translation.y == doctest::Approx(hull->height * 0.5 + 0.025));
				++roots;
			}
		CHECK(roots == (layout.pads[index].display ? 1u : 0u));
		if (layout.pads[index].display)
		{
			CHECK(layout.pads[index].ship != "");
			shown.insert(layout.pads[index].ship);
		}
	}
	CHECK(
	    shown ==
	    std::multiset<std::string>{"fighter", "fighter", "fighter", "fighter", "frigate", "liner"});
}

TEST_CASE("Dressing: a pad's lamps show its state from the snapshots, and a display pad's stay blue")
{
	Dressed dressed;
	const std::vector<entt::entity> stations =
	    App::OpenStations(dressed.world, dressed.docks, dressed.systems);
	REQUIRE(!stations.empty());
	entt::registry &world = dressed.world.Registry();
	const entt::entity highport = stations.front();
	const App::StationDressing dressing =
	    App::DressStation(dressed.scene, dressed.station, dressed.Layout(), dressed.docks);
	const auto colour = [&](std::uint32_t number)
	{ return dressed.scene.get<Scene::MeshComponent>(dressing.lamps[number - 1].front()).color; };

	// Pads 2, 7 and 11 hold the parked transports; the rest are free, and 13 to 18 are display pads.
	Sim::Snapshot snapshot = Sim::Capture(dressed.world);
	CHECK(
	    App::ShowPadStates(dressed.scene, dressing, snapshot.Components<Dock::Pad>(), highport) == 18);
	CHECK(colour(1) == App::LampColour(Dock::PadState::Free, false));
	CHECK(colour(2) == App::LampColour(Dock::PadState::Holding, false));
	CHECK(colour(13) == App::LampColour(Dock::PadState::Free, true));
	for (const entt::entity lamp : dressing.lamps[1])
		CHECK(dressed.scene.get<Scene::MeshComponent>(lamp).color == colour(2));

	// Reserved, as the docks leave a pad given to a ship on its way; the world is not read, only what
	// the next snapshot carries.
	const entt::entity pad = world.get<Dock::Station>(highport).pads.front();
	world.get<Dock::Pad>(pad).state = Dock::PadState::Reserved;
	CHECK(colour(1) == App::LampColour(Dock::PadState::Free, false));
	snapshot = Sim::Capture(dressed.world);
	App::ShowPadStates(dressed.scene, dressing, snapshot.Components<Dock::Pad>(), highport);
	CHECK(colour(1) == App::LampColour(Dock::PadState::Reserved, false));

	// Without its station, none of the pads are the dressing's.
	CHECK(
	    App::ShowPadStates(dressed.scene, dressing, snapshot.Components<Dock::Pad>(), entt::null) == 0);
}

TEST_CASE(
    "Dressing: each hull class's proxy is inside its hull's box, and a hull's model is drawn instead")
{
	Dressed dressed;
	for (const std::string_view name :
	     {"shuttle", "fighter", "transport", "heavy-transport", "liner", "frigate"})
	{
		CAPTURE(name);
		const Dock::HullDefinition *hull = dressed.docks.FindHull(name);
		REQUIRE(hull);
		entt::registry scene;
		const entt::entity root = App::MakeHullProxy(scene, *hull);
		const glm::dvec3 half = glm::dvec3{hull->beam, hull->height, hull->length} * 0.5;
		std::size_t parts = 0;
		glm::dvec3 reach{0.0};
		for (const auto [entity, mesh] : scene.view<const Scene::MeshComponent>().each())
		{
			CHECK(mesh.shape.has_value());
			CHECK(mesh.model.empty());
			// A unit shape's corners, wherever they are turned to.
			const glm::dmat4 placed = Under(scene, entity, root);
			for (int corner = 0; corner < 8; ++corner)
			{
				const glm::dvec4 at = placed * glm::dvec4{
				                                   (corner & 1) ? 1.0 : -1.0, (corner & 2) ? 1.0 : -1.0,
				                                   (corner & 4) ? 1.0 : -1.0, 1.0};
				reach = glm::max(reach, glm::abs(glm::dvec3{at}));
			}
			++parts;
		}
		CHECK(parts >= 5);
		// A sphere's or a cylinder's box corners are outside the shape itself, so this is the
		// strictest bound the corners give: within a hair of the hull's box on every axis.
		CHECK(reach.x <= half.x * 1.001);
		CHECK(reach.y <= half.y * 1.001);
		CHECK(reach.z <= half.z * 1.001);
		// Wide and long enough that the box it collides as is not mostly empty.
		CHECK(reach.x >= half.x * 0.9);
		CHECK(reach.z >= half.z * 0.9);
	}

	Dock::HullDefinition modelled = *dressed.docks.FindHull("transport");
	modelled.model = "models/transport.glb";
	entt::registry scene;
	const entt::entity root = App::MakeHullProxy(scene, modelled);
	std::size_t models = 0;
	for (const auto [entity, mesh] : scene.view<const Scene::MeshComponent>().each())
	{
		CHECK(mesh.model == "models/transport.glb");
		CHECK(scene.get<Scene::ParentComponent>(entity).entity == root);
		++models;
	}
	CHECK(models == 1);
}

TEST_CASE("Dressing: a ship's proxy has a plume at each nozzle, lit by its throttle in the snapshots")
{
	Dressed dressed;
	const Dock::HullDefinition *shuttle = dressed.docks.FindHull("shuttle");
	REQUIRE(shuttle);
	entt::registry &world = dressed.world.Registry();
	const entt::entity ship = world.create();
	world.emplace<Physics::FlightControls>(ship);
	const entt::entity proxy = App::MakeHullProxy(dressed.scene, *shuttle, ship);
	REQUIRE(dressed.scene.all_of<App::ShipEngines>(proxy));
	const auto &engines = dressed.scene.get<App::ShipEngines>(proxy);
	// The shuttle's two engines, each a plume aft of its nozzle, in the effects layer.
	REQUIRE(engines.plumes.size() == 2);
	for (const entt::entity plume : engines.plumes)
	{
		CHECK(dressed.scene.get<Scene::TransformComponent>(plume).translation.z > 0.0);
		CHECK(dressed.scene.get<Scene::LayerComponent>(plume).layer == Scene::Layer::Effects);
	}
	const auto &emitter = dressed.scene.get<Scene::ParticleEmitterComponent>(engines.plumes.front());

	// Let go, nothing burns.
	App::ShowEngines(dressed.scene, Sim::Capture(dressed.world), dressed.station);
	CHECK(!emitter.active);
	CHECK(emitter.frame == dressed.station);
	// Half ahead, a plume half as dense as full ahead's; astern, none from the main engines.
	world.get<Physics::FlightControls>(ship).linear.z = -0.5f;
	App::ShowEngines(dressed.scene, Sim::Capture(dressed.world), dressed.station);
	CHECK(emitter.active);
	const float half = emitter.rate;
	world.get<Physics::FlightControls>(ship).linear.z = -1.0f;
	App::ShowEngines(dressed.scene, Sim::Capture(dressed.world), dressed.station);
	CHECK(emitter.rate == doctest::Approx(2.0 * half));
	world.get<Physics::FlightControls>(ship).linear.z = 1.0f;
	App::ShowEngines(dressed.scene, Sim::Capture(dressed.world), dressed.station);
	CHECK(!emitter.active);

	// The display wing's ships are of no simulation entity, and have no plumes.
	entt::registry display;
	App::MakeHullProxy(display, *shuttle);
	CHECK(display.view<const Scene::ParticleEmitterComponent>().empty());
}

TEST_CASE("Dressing: a buggy's proxy carries on its bed the crates the snapshots have it carry")
{
	Dressed dressed;
	const std::vector<entt::entity> stations =
	    App::OpenStations(dressed.world, dressed.docks, dressed.systems);
	REQUIRE(!stations.empty());
	entt::registry &world = dressed.world.Registry();
	// The heavy transport parked on Highport's pad 2, loading six crates: the first buggy out of the
	// bay carries four, and the next waits for it to clear the door.
	const entt::entity pad = world.get<Dock::Station>(stations.front()).pads.at(1);
	const entt::entity ship = world.get<Dock::Pad>(pad).ship;
	REQUIRE(Dock::Carry(world, ship, 6));
	dressed.world.Step();
	const auto buggies = world.view<const Dock::Buggy>();
	REQUIRE(buggies.size() == 1);
	const entt::entity loaded = buggies.front();
	REQUIRE(world.get<Dock::Buggy>(loaded).crates == 4);
	const entt::entity proxy = App::MakeBuggyProxy(dressed.scene, loaded);

	const auto shown = [&]
	{
		std::size_t crates = 0;
		for (const entt::entity crate : dressed.scene.get<App::BuggyLoad>(proxy).crates)
			crates += dressed.scene.get<Scene::TransformComponent>(crate).scale.x > 0.0f ? 1 : 0;
		return crates;
	};
	CHECK(shown() == 0);
	App::ShowBuggyLoads(dressed.scene, Sim::Capture(dressed.world));
	CHECK(shown() == 4);
	// Each part on the buggy's 4 x 2.4 m footprint, on the deck or above it.
	for (const auto [entity, mesh] : dressed.scene.view<const Scene::MeshComponent>().each())
	{
		const glm::dmat4 at = Under(dressed.scene, entity, proxy);
		for (const double x : {-1.0, 1.0})
			for (const double y : {-1.0, 1.0})
				for (const double z : {-1.0, 1.0})
				{
					const glm::dvec4 corner = at * glm::dvec4{x, y, z, 1.0};
					CHECK(std::abs(corner.x) <= 1.31);
					CHECK(std::abs(corner.z) <= 2.01);
					CHECK(corner.y >= -1.0e-6);
				}
	}
	// Without it in the snapshot, its bed is bare.
	world.get<Dock::Buggy>(loaded).crates = 0;
	App::ShowBuggyLoads(dressed.scene, Sim::Capture(dressed.world));
	CHECK(shown() == 0);
}

TEST_CASE("Dressing: a station whose buggies are parked shows them across each pad's stern")
{
	Dressed dressed;
	Dock::StationLayout layout = dressed.Layout();
	layout.traffic.working = false;
	const App::StationDressing dressing =
	    App::DressStation(dressed.scene, dressed.station, layout, dressed.docks);
	// Three a pad on the twelve pads ships land on, none on the display pads.
	CHECK(dressing.parked == 36);
	std::size_t onPads = 0;
	for (const auto [entity, load] : dressed.scene.view<const App::BuggyLoad>().each())
	{
		CHECK(load.buggy == entt::null);
		const glm::dvec3 at = dressed.scene.get<Scene::TransformComponent>(entity).translation;
		for (const Dock::PadLayout &pad : layout.pads)
		{
			const glm::dvec3 local = glm::conjugate(Dock::PadOrientation(pad)) * (at - pad.position);
			const glm::vec2 size = Dock::PadSize(pad.padClass);
			// On the plate, behind where the largest hull's stern is.
			if (std::abs(local.x) < size.x * 0.5 && std::abs(local.z) < size.y * 0.5)
			{
				CHECK(local.z > Dock::LargestHull(pad.padClass).x * 0.5 + 1.2);
				++onPads;
			}
		}
	}
	CHECK(onPads == 36);
}
