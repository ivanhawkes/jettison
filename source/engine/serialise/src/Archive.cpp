#include <jettison/serialise/Archive.h>

#include <format>

namespace Jettison::Serialise
{
FormatVersionError::FormatVersionError(
    std::string_view format, std::uint32_t found, std::uint32_t expected, std::string_view reason)
    : std::runtime_error{std::format(
          "{}: version {} found, version {} expected{}{}", format, found, expected,
          reason.empty() ? "" : ": ", reason)},
      m_found{found}, m_expected{expected}
{
}
} // namespace Jettison::Serialise
