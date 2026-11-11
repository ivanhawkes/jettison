#pragma once

#include <jettison/celestial/StarSystem.h>
#include <jettison/physics/Shape.h>
#include <jettison/sim/Snapshot.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <string_view>

// What the physics suite's station runs share: Halcyon, and a station's hull as a mesh.
namespace Jettison::PhysicsTests
{
inline const std::uint64_t kHalcyon = Sim::SystemId("Halcyon");
inline constexpr std::string_view kStation = "Ostra Highport";

// Halcyon as its system file has it (assets/systems/halcyon.yaml): a Sun-like star, an Earth-like
// planet a hundred and fifty million kilometres out, and a station in low orbit over it, so the
// station moves at 7.7 km/s about the planet and 30 km/s about the star.
inline Celestial::StarSystem Halcyon()
{
	Celestial::StarSystem system;
	system.name = "Halcyon";
	Celestial::Body star;
	star.name = "Halcyon";
	star.kind = Celestial::BodyKind::Star;
	star.radius = 6.957e8;
	star.gravitationalParameter = 1.32712440018e20;
	Celestial::Body planet;
	planet.name = "Ostra";
	planet.parent = "Halcyon";
	planet.radius = 6.371e6;
	planet.gravitationalParameter = 3.986004418e14;
	planet.orbit = {1.496e11, 0.0167, 0.0, 0.0, 1.7967, 6.24, 0.0};
	Celestial::Body station;
	station.name = std::string{kStation};
	station.kind = Celestial::BodyKind::Station;
	station.parent = "Ostra";
	station.radius = 60.0;
	station.orbit = {6.791e6, 0.0005, 0.9006, 1.2, 0.4, 3.1416, 0.0};
	system.bodies = {star, planet, station};
	Celestial::Resolve(system);
	return system;
}

// A closed box of twelve triangles facing out, a station's hull as a mesh: what a kinematic body
// on rails is made of.
inline Physics::ShapeDefinition MeshBox(const glm::vec3 &half)
{
	Physics::ShapeDefinition shape;
	shape.kind = Physics::ShapeKind::Mesh;
	for (const float x : {-half.x, half.x})
		for (const float y : {-half.y, half.y})
			for (const float z : {-half.z, half.z})
				shape.vertices.emplace_back(x, y, z);
	// Vertex index bits: 4 is x, 2 is y, 1 is z. Counter-clockwise seen from outside.
	shape.indices = {0, 3, 2, 0, 1, 3, 4, 7, 5, 4, 6, 7, 0, 5, 1, 0, 4, 5,
	                 2, 7, 6, 2, 3, 7, 0, 6, 4, 0, 2, 6, 1, 7, 3, 1, 5, 7};
	return shape;
}
} // namespace Jettison::PhysicsTests
