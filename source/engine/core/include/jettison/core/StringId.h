#pragma once

#include <cstdint>
#include <string_view>

namespace Jettison::Core
{
// A hashed string for asset paths, definition names and tags: comparing two is comparing two
// integers, and a registry keyed on them needs no string storage. The hash is computed at compile
// time from a literal and at run time from a string, and both must agree, so phase 2 chooses the
// function (FNV-1a or xxHash) once and pins it with recorded values; it is part of the save
// format from then on. In development builds a table maps hashes back to strings for messages.
class StringId
{
  public:
	constexpr StringId() = default;
	explicit StringId(std::string_view text);

	constexpr std::uint64_t Hash() const
	{
		return m_hash;
	}
	constexpr bool IsNull() const
	{
		return m_hash == 0;
	}

	// The text the hash was made from, in development builds; "<unknown>" otherwise.
	std::string_view Text() const;

	friend constexpr bool operator==(const StringId &, const StringId &) = default;

  private:
	std::uint64_t m_hash{0};
};
} // namespace Jettison::Core
