#pragma once

#include <jettison/celestial/Galaxy.h>
#include <jettison/celestial/StarSystem.h>

#include <string>
#include <string_view>
#include <utility>

// What the AI suite's routes and lanes share: the sample galaxy's three systems in a line, Halcyon
// to Vesper to Marrow, built in code from their files (assets/systems) without the bodies' spin,
// so a route from one end to the other is two jumps.
namespace Jettison::AiTests
{
inline Celestial::Body MakeBody(
    std::string name, Celestial::BodyKind kind, std::string parent, double radius,
    double gravitationalParameter, Celestial::OrbitalElements orbit = {})
{
	Celestial::Body body;
	body.name = std::move(name);
	body.kind = kind;
	body.parent = std::move(parent);
	body.radius = radius;
	body.gravitationalParameter = gravitationalParameter;
	body.orbit = orbit;
	return body;
}

// A Sun-like star, an Earth-like planet with a moon, a station in low orbit over the planet, and
// the gate to Vesper keeping station 5 km above the station, as assets/systems/halcyon.yaml has them.
inline Celestial::StarSystem Halcyon()
{
	using enum Celestial::BodyKind;
	Celestial::StarSystem system;
	system.name = "Halcyon";
	system.bodies = {
	    MakeBody("Halcyon", Star, "", 6.957e8, 1.32712440018e20),
	    MakeBody(
	        "Ostra", Planet, "Halcyon", 6.371e6, 3.986004418e14,
	        {1.496e11, 0.0167, 0.0, 0.0, 1.7967, 6.24, 0.0}),
	    MakeBody(
	        "Ostra Highport", Station, "Ostra", 60.0, 0.0,
	        {6.791e6, 0.0005, 0.9006, 1.2, 0.4, 3.1416, 0.0}),
	    MakeBody(
	        "Pell", Moon, "Ostra", 1.7374e6, 4.9048695e12,
	        {3.844e8, 0.0549, 0.0898, 2.1831, 5.5528, 2.3554, 0.0}),
	    MakeBody(
	        "Halcyon Gate", JumpPoint, "Ostra Highport", 500.0, 0.0,
	        {5000.0, 0.0, 0.9006, 1.2, 0.4, 3.1416, 5569.440597283014}),
	};
	Celestial::Resolve(system);
	return system;
}

// A star, a gas giant and its refinery, and two gates by the refinery, back to Halcyon and on to
// Marrow.
inline Celestial::StarSystem Vesper()
{
	using enum Celestial::BodyKind;
	Celestial::StarSystem system;
	system.name = "Vesper";
	system.bodies = {
	    MakeBody("Vesper", Star, "", 2.9e8, 5.97e19),
	    MakeBody("Corva", Planet, "Vesper", 7.0e7, 1.27e17, {3.0e10, 0.012, 0.02, 0.6, 2.1, 0.4, 0.0}),
	    MakeBody(
	        "Corva Refinery", Station, "Corva", 80.0, 0.0, {1.4e8, 0.001, 0.3, 2.4, 0.9, 1.7, 0.0}),
	    MakeBody(
	        "Vesper Gate", JumpPoint, "Corva Refinery", 500.0, 0.0,
	        {5000.0, 0.0, 0.3, 2.4, 0.9, 1.7, 29205.864191967677}),
	    MakeBody(
	        "Vesper Deep Gate", JumpPoint, "Corva Refinery", 500.0, 0.0,
	        {6000.0, 0.0, 0.3, 2.4, 0.9, 2.2236, 29205.864191967677}),
	};
	Celestial::Resolve(system);
	return system;
}

// A star, an airless world and the gate back to Vesper, in orbit of the world.
inline Celestial::StarSystem Marrow()
{
	using enum Celestial::BodyKind;
	Celestial::StarSystem system;
	system.name = "Marrow";
	system.bodies = {
	    MakeBody("Marrow", Star, "", 1.2e9, 2.6e20),
	    MakeBody("Ashfall", Planet, "Marrow", 4.1e6, 2.5e14, {2.6e11, 0.08, 0.04, 4.0, 1.2, 2.9, 0.0}),
	    MakeBody(
	        "Marrow Gate", JumpPoint, "Ashfall", 500.0, 0.0, {1.2e7, 0.0, 0.1, 0.0, 0.0, 0.7, 0.0}),
	};
	Celestial::Resolve(system);
	return system;
}

inline Celestial::JumpLane Lane(
    std::string_view a, std::string_view aGate, std::string_view b, std::string_view bGate,
    double transitSeconds)
{
	return {{std::string{a}, std::string{aGate}}, {std::string{b}, std::string{bGate}}, transitSeconds};
}

// The sample galaxy (assets/systems/galaxy.yaml): Halcyon to Vesper in 40 s, Vesper to Marrow in 65.
inline Celestial::Galaxy Sample()
{
	Celestial::Galaxy galaxy;
	galaxy.name = "Sample";
	galaxy.systems = {
	    {"Halcyon", "systems/halcyon.yaml", std::nullopt},
	    {"Vesper", "systems/vesper.yaml", std::nullopt},
	    {"Marrow", "systems/marrow.yaml", std::nullopt},
	};
	galaxy.lanes = {
	    Lane("Halcyon", "Halcyon Gate", "Vesper", "Vesper Gate", 40.0),
	    Lane("Vesper", "Vesper Deep Gate", "Marrow", "Marrow Gate", 65.0),
	};
	Celestial::Resolve(galaxy);
	return galaxy;
}
} // namespace Jettison::AiTests
