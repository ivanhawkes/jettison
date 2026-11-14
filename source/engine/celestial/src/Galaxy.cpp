#include <jettison/celestial/Galaxy.h>

#include <cmath>
#include <map>
#include <string>
#include <utility>

#include <jettison/celestial/StarSystem.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

namespace Jettison::Celestial
{
namespace
{
[[noreturn]] void Refuse(const Galaxy &galaxy, const std::string &what)
{
	throw Serialise::FormatError("Galaxy '" + galaxy.name + "': " + what);
}

std::string Describe(const JumpLane &lane, std::size_t index)
{
	return "lane " + std::to_string(index) + " ('" + lane.a.system + "' to '" + lane.b.system + "')";
}

void CheckPath(
    const Galaxy &galaxy, const GalaxySystem &system, const std::string &path, const char *what)
{
	if (!Vfs::FileSystem::IsValidPath(path))
		Refuse(
		    galaxy, "system '" + system.name + "' has " + what + " '" + path +
		                "', which is not a virtual path (systems/name.yaml, relative and "
		                "forward-slashed)");
}

Exit ExitOf(const Galaxy &galaxy, std::size_t lane, bool fromA)
{
	const JumpLane &edge = galaxy.lanes[lane];
	const LaneEnd &here = fromA ? edge.a : edge.b;
	const LaneEnd &there = fromA ? edge.b : edge.a;
	return {lane, here.jumpPoint, there.systemIndex, there.jumpPoint, edge.transitSeconds, edge.toll};
}
} // namespace

void RegisterGalaxyTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<GalaxySystem>("Celestial.GalaxySystem")
		    .Field<&GalaxySystem::name>("name", Attribute::Saved)
		    .Field<&GalaxySystem::system>("system", Attribute::Saved)
		    .Field<&GalaxySystem::scene>("scene", Attribute::Saved);
		RegisterType<LaneEnd>("Celestial.LaneEnd")
		    .Field<&LaneEnd::system>("system", Attribute::Saved)
		    .Field<&LaneEnd::jumpPoint>("jumpPoint", Attribute::Saved);
		RegisterType<JumpLane>("Celestial.JumpLane")
		    .Field<&JumpLane::a>("a", Attribute::Saved)
		    .Field<&JumpLane::b>("b", Attribute::Saved)
		    .Field<&JumpLane::transitSeconds>("transitSeconds", Attribute::Saved)
		    .Field<&JumpLane::toll>("toll", Attribute::Saved);
		RegisterType<Galaxy>("Celestial.Galaxy", Attribute::Moddable)
		    .Field<&Galaxy::name>("name", Attribute::Saved)
		    .Field<&Galaxy::systems>("systems", Attribute::Saved)
		    .Field<&Galaxy::lanes>("lanes", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

int Galaxy::Find(std::string_view systemName) const
{
	for (std::size_t index = 0; index < systems.size(); ++index)
		if (systems[index].name == systemName)
			return static_cast<int>(index);
	return -1;
}

int Galaxy::FindBySystemFile(std::string_view virtualPath) const
{
	for (std::size_t index = 0; index < systems.size(); ++index)
		if (systems[index].system == virtualPath)
			return static_cast<int>(index);
	return -1;
}

void Resolve(Galaxy &galaxy)
{
	if (galaxy.systems.empty())
		Refuse(galaxy, "lists no systems");

	// Ordered, so the first duplicate reported is the same one on every run.
	std::map<std::string, std::size_t, std::less<>> indices;
	for (std::size_t index = 0; index < galaxy.systems.size(); ++index)
	{
		const GalaxySystem &system = galaxy.systems[index];
		if (system.name.empty())
			Refuse(galaxy, "system " + std::to_string(index) + " has no name");
		if (!indices.emplace(system.name, index).second)
			Refuse(galaxy, "system '" + system.name + "' is listed twice");
		CheckPath(galaxy, system, system.system, "a system file");
		if (system.scene)
			CheckPath(galaxy, system, *system.scene, "a scene file");
	}

	std::map<std::pair<std::string, std::string>, std::size_t> ends;
	for (std::size_t index = 0; index < galaxy.lanes.size(); ++index)
	{
		JumpLane &lane = galaxy.lanes[index];
		for (LaneEnd *end : {&lane.a, &lane.b})
		{
			const auto system = indices.find(end->system);
			if (system == indices.end())
				Refuse(
				    galaxy, Describe(lane, index) + " ends in '" + end->system +
				                "', which the galaxy does not list");
			end->systemIndex = system->second;
			if (end->jumpPoint.empty())
				Refuse(galaxy, Describe(lane, index) + " names no jump point in '" + end->system + "'");
			if (const auto [at, added] = ends.emplace(std::pair{end->system, end->jumpPoint}, index);
			    !added)
				Refuse(
				    galaxy, "jump point '" + end->jumpPoint + "' in '" + end->system +
				                "' is an end of lane " + std::to_string(at->second) + " and of " +
				                Describe(lane, index) + "; a jump point leads one way");
		}
		if (lane.a.systemIndex == lane.b.systemIndex)
			Refuse(galaxy, Describe(lane, index) + " joins a system to itself");
		if (!std::isfinite(lane.transitSeconds) || lane.transitSeconds < 0.0)
			Refuse(galaxy, Describe(lane, index) + " has a negative transit time");
		if (lane.toll < 0)
			Refuse(galaxy, Describe(lane, index) + " has a negative toll");
	}
}

Galaxy ReadGalaxy(std::string_view text)
{
	RegisterGalaxyTypes();
	auto galaxy =
	    Serialise::YamlArchive::Read<Galaxy>(text, Serialise::FormatVersion{kGalaxyFormatVersion});
	Resolve(galaxy);
	return galaxy;
}

std::string WriteGalaxy(const Galaxy &galaxy)
{
	RegisterGalaxyTypes();
	return Serialise::YamlArchive::Write(galaxy, Serialise::FormatVersion{kGalaxyFormatVersion});
}

void CheckJumpPoints(const Galaxy &galaxy, std::size_t system, const StarSystem &starSystem)
{
	const GalaxySystem &entry = galaxy.systems.at(system);
	if (starSystem.name != entry.name)
		Refuse(
		    galaxy, "system '" + entry.name + "' is at '" + entry.system +
		                "', and the system there is named '" + starSystem.name + "'");
	for (const Exit &exit : Exits(galaxy, system))
	{
		const int body = starSystem.Find(exit.jumpPoint);
		if (body < 0)
			Refuse(
			    galaxy, Describe(galaxy.lanes[exit.lane], exit.lane) + " leaves '" + entry.name +
			                "' through '" + std::string{exit.jumpPoint} +
			                "', which the system does not have");
		if (starSystem.bodies[static_cast<std::size_t>(body)].kind != BodyKind::JumpPoint)
			Refuse(
			    galaxy, Describe(galaxy.lanes[exit.lane], exit.lane) + " leaves '" + entry.name +
			                "' through '" + std::string{exit.jumpPoint} +
			                "', which is not a jump point");
	}
}

std::vector<Exit> Exits(const Galaxy &galaxy, std::size_t system)
{
	std::vector<Exit> exits;
	for (std::size_t lane = 0; lane < galaxy.lanes.size(); ++lane)
	{
		// A lane joins two different systems (Resolve), so at most one end is here.
		if (galaxy.lanes[lane].a.systemIndex == system)
			exits.push_back(ExitOf(galaxy, lane, true));
		else if (galaxy.lanes[lane].b.systemIndex == system)
			exits.push_back(ExitOf(galaxy, lane, false));
	}
	return exits;
}

std::optional<Exit> ExitThrough(const Galaxy &galaxy, std::size_t system, std::string_view jumpPoint)
{
	for (const Exit &exit : Exits(galaxy, system))
		if (exit.jumpPoint == jumpPoint)
			return exit;
	return std::nullopt;
}
} // namespace Jettison::Celestial
