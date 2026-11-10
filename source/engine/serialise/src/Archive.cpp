#include <jettison/serialise/Archive.h>

#include <format>

namespace Jettison::Serialise
{
FormatVersionError::FormatVersionError(
    std::string_view format, std::uint32_t found, std::uint32_t expected)
    : std::runtime_error{
          std::format("{}: version {} found, version {} expected", format, found, expected)}
{
}
} // namespace Jettison::Serialise
