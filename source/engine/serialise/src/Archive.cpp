#include <jettison/serialise/Archive.h>

#include <jettison/core/Assert.h>

namespace Jettison::Serialise
{
FormatVersionError::FormatVersionError(
    std::string_view format, std::uint32_t found, std::uint32_t expected)
    : std::runtime_error{
          std::string{format} + ": version " + std::to_string(found) + " found, version " +
          std::to_string(expected) + " expected"}
{
}

std::string YamlArchive::Write(entt::meta_any /*unused*/, FormatVersion /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
entt::meta_any YamlArchive::Read(
    std::string_view /*unused*/, entt::meta_type /*unused*/, FormatVersion /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
std::vector<std::byte> BinaryArchive::Write(entt::meta_any /*unused*/, FormatVersion /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
entt::meta_any BinaryArchive::Read(
    std::span<const std::byte> /*unused*/, entt::meta_type /*unused*/, FormatVersion /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Serialise
