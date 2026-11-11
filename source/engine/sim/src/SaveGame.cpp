#include <jettison/sim/SaveGame.h>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

#include <jettison/core/Version.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Binary.h>
#include <jettison/serialise/Migration.h>
#include <jettison/sim/SimWorld.h>

namespace Jettison::Sim
{
void RegisterSaveTypes()
{
	static const bool registered = []
	{
		using Reflect::Attribute;
		Reflect::RegisterType<SaveInfo>("Sim.SaveInfo")
		    .Field<&SaveInfo::name>("name", Attribute::Saved)
		    .Field<&SaveInfo::engineVersion>("engineVersion", Attribute::Saved)
		    .Field<&SaveInfo::writtenAt>("writtenAt", Attribute::Saved)
		    .Field<&SaveInfo::step>("step", Attribute::Saved)
		    .Field<&SaveInfo::time>("time", Attribute::Saved)
		    .Field<&SaveInfo::galaxy>("galaxy", Attribute::Saved)
		    .Field<&SaveInfo::system>("system", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

SaveGame SaveGame::Of(const SimWorld &world, std::string name)
{
	SaveGame save;
	save.info.name = std::move(name);
	save.info.engineVersion = std::string{Core::EngineVersion()};
	save.info.step = world.Clock().Step();
	save.info.time = world.Clock().Now();
	save.world = world.Save();
	return save;
}

void SaveGame::AddSection(std::string name, std::vector<std::byte> data)
{
	if (name.empty())
		throw std::logic_error{"SaveGame: a section needs a name"};
	if (FindSection(name))
		throw std::logic_error{std::format("SaveGame: section '{}' is already in the save", name)};
	sections.push_back({std::move(name), std::move(data)});
}

const SaveSection *SaveGame::FindSection(std::string_view name) const
{
	const auto found = std::ranges::find(sections, name, &SaveSection::name);
	return found == sections.end() ? nullptr : &*found;
}

// The info first, so a list reads no further; then the world, then the sections in the order they
// were added.
std::vector<std::byte> SaveGame::Write() const
{
	RegisterSaveTypes();
	Serialise::BinaryWriter writer{kFormat};
	writer.Write(info);
	writer.WriteBytes(world);
	writer.WriteUnsigned(sections.size(), 4);
	for (const SaveSection &section : sections)
	{
		writer.WriteName(section.name);
		writer.WriteBytes(section.data);
	}
	return writer.Finish(kVersion);
}

SaveGame SaveGame::Read(std::span<const std::byte> bytes)
{
	RegisterSaveTypes();
	Serialise::BinaryReader reader{bytes, Migrations()};
	SaveGame save;
	reader.Read(save.info);
	save.world = reader.ReadBytes();
	const auto count = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
	// A section is at least its name's index and its count.
	if (count > reader.Remaining() / 8)
		reader.Fail("the count of sections is more than the data holds");
	for (std::uint32_t index = 0; index < count; ++index)
	{
		std::string name{reader.ReadName()};
		if (name.empty() || save.FindSection(name))
			reader.Fail(std::format("the save holds section '{}' twice, or with no name", name));
		save.sections.push_back({std::move(name), reader.ReadBytes()});
	}
	reader.End();
	return save;
}

SaveInfo SaveGame::ReadInfo(std::span<const std::byte> bytes)
{
	RegisterSaveTypes();
	Serialise::BinaryReader reader{bytes, Migrations()};
	SaveInfo info;
	reader.Read(info);
	return info;
}

const Serialise::BinaryMigrations &SaveGame::Migrations()
{
	static const Serialise::BinaryMigrations migrations{std::string{kFormat}, kVersion};
	return migrations;
}
} // namespace Jettison::Sim
