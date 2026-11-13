#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <jettison/serialise/Archive.h>

namespace YAML
{
class Node;
}

namespace Jettison::Serialise
{
// A binary archive opened for a migration to edit: the layout BinaryWriter::Finish writes
// (Archive.h), as its format's name, its version, its table of names and its payload. Fields and
// enumerators are found by name, so renaming a field, a component, a command or an enumerator is
// an edit to the table alone (Rename); a change that adds to the payload, as the world's version 2
// added its services' state after the components, appends to it. A change inside a value needs the
// types that wrote it, and is the migration of whoever has them.
struct BinaryDocument
{
	std::string format;
	std::uint32_t version{0};
	std::vector<std::string> names;
	std::vector<std::byte> payload;

	// Throws FormatError for data that is not a binary archive of the format, or whose table does
	// not fit in it.
	static BinaryDocument Parse(std::span<const std::byte> bytes, std::string_view format);
	// The archive again, byte for byte what Parse read if nothing was changed.
	std::vector<std::byte> Emit() const;

	// Every use of a name becomes another: a field of every type that has it, every enumerator, every
	// type. Throws std::logic_error if the document does not use the name, or already uses the new
	// one, which would merge the two.
	void Rename(std::string_view from, std::string_view to);
};

// The version a binary archive of the format says it is, read from its header alone. Throws
// FormatError for data that is not one.
std::uint32_t PeekVersion(std::span<const std::byte> bytes, std::string_view format);

// A format's migrations: a chain of steps, each taking data of one version to the next, so data of
// any version the chain reaches is brought up to the current one a step at a time, and a change to
// a format is one new step rather than a change to every reader of every old version. A step is
// written once, when its version is current, and never changed after: the data it reads is the
// data that version wrote. Data is a BinaryDocument for saves and a YAML::Node for authored
// content (definitions, scenes), whose root holds its version as YamlArchive writes it; a step
// edits the data, and the chain sets the version it leaves.
//
// A YAML step edits what the data has and adds nothing it leaves out. A field left out is read as
// its default anyway, and a patch (YamlArchive::ReadLayered) is migrated as a document is while
// leaving out every field it does not change: a step that filled a default in would set it over the
// layer below.
//
// A chain is made at start-up and read from then on; Run is safe from any thread once it is.
template <typename Data> class Migrations
{
  public:
	using Step = std::function<void(Data &)>;

	// A chain with no steps reads the current version alone, as a FormatVersion does.
	Migrations(std::string format, FormatVersion current)
	    : m_format{std::move(format)}, m_current{current}
	{
	}

	// The step from version `from` to from + 1. Throws std::logic_error for one that would not end
	// at or before the current version, or that the chain has.
	Migrations &Add(std::uint32_t from, Step step)
	{
		if (from == 0 || from >= m_current.version)
			throw std::logic_error{std::format(
			    "Migrations: {}: a step from version {} does not lead to version {}", m_format, from,
			    m_current.version)};
		if (!step || !m_steps.try_emplace(from, std::move(step)).second)
			throw std::logic_error{std::format(
			    "Migrations: {}: the step from version {} is already added", m_format, from)};
		return *this;
	}

	const std::string &Format() const
	{
		return m_format;
	}
	FormatVersion Current() const
	{
		return m_current;
	}
	// Whether data of the version is current or can be brought up to it.
	bool Reaches(std::uint32_t found) const
	{
		if (found == 0 || found > m_current.version)
			return false;
		for (std::uint32_t version = found; version < m_current.version; ++version)
			if (!m_steps.contains(version))
				return false;
		return true;
	}

	// Brings data of the version found up to the current one. Throws FormatVersionError, before any
	// step runs, for a version newer than the chain's (a later build wrote it) or one the chain does
	// not reach; a step's own error is the step's.
	void Run(Data &data, std::uint32_t found) const
	{
		Check(found);
		for (std::uint32_t version = found; version < m_current.version; ++version)
			m_steps.at(version)(data);
	}

	// The FormatVersionError Run throws for that version, or nothing to throw.
	void Check(std::uint32_t found) const
	{
		if (Reaches(found))
			return;
		if (found > m_current.version)
			throw FormatVersionError{
			    m_format, found, m_current.version, "it was written by a later build"};
		std::uint32_t missing = found;
		while (missing < m_current.version && m_steps.contains(missing))
			++missing;
		throw FormatVersionError{
		    m_format, found, m_current.version,
		    std::format("there is no migration from version {}", missing)};
	}

  private:
	std::string m_format;
	FormatVersion m_current;
	std::map<std::uint32_t, Step> m_steps;
};

// A binary document carries its version in a field of its own, which each step leaves at the
// version it made; a YAML root's is set by YamlArchive, which owns where it lives.
template <> inline void Migrations<BinaryDocument>::Run(BinaryDocument &data, std::uint32_t found) const
{
	Check(found);
	for (std::uint32_t version = found; version < m_current.version; ++version)
	{
		m_steps.at(version)(data);
		data.version = version + 1;
	}
}

using BinaryMigrations = Migrations<BinaryDocument>;
using YamlMigrations = Migrations<YAML::Node>;

// Binary data of any version the chain reaches, as the current version: the bytes as they are when
// they are current, and migrated when not. Throws as Run does, and FormatError for data that is not
// of the chain's format.
std::vector<std::byte> Migrate(std::span<const std::byte> bytes, const BinaryMigrations &migrations);
} // namespace Jettison::Serialise
