#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace Jettison::Serialise
{
// Every format carries one. A reader that meets a version it does not know runs the migration
// chain (phase 8) or throws FormatVersionError naming both versions.
struct FormatVersion
{
	std::uint32_t version{1};
};

class FormatVersionError : public std::runtime_error
{
  public:
	FormatVersionError(std::string_view format, std::uint32_t found, std::uint32_t expected);
};

// Reflection-driven archives. A value whose type is registered with Reflect is written field by
// field, Saved fields only, and read back the same way; the loader and the writer are one pair,
// tested as a round trip (docs/history.md, row 8). YAML for authored content, binary for saves.
// Filled in phase 3; the existing scene format moves onto it then.
class YamlArchive
{
  public:
	static std::string Write(entt::meta_any value, FormatVersion version);
	static entt::meta_any Read(std::string_view text, entt::meta_type type, FormatVersion expected);
};

class BinaryArchive
{
  public:
	static std::vector<std::byte> Write(entt::meta_any value, FormatVersion version);
	static entt::meta_any Read(
	    std::span<const std::byte> bytes, entt::meta_type type, FormatVersion expected);
};
} // namespace Jettison::Serialise
