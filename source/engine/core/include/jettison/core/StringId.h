#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include <jettison/core/Hash.h>

namespace Jettison::Core
{
// A hashed string for asset paths, definition names and tags: comparing two is comparing two
// integers, and a registry keyed on them needs no string storage. The hash is Fnv1a64 (Hash.h),
// the same function at compile time and at run time, and it is part of the save format.
//
// In development builds (no NDEBUG) every ID made at run time records its text in a table, so a
// message can name it, and two different strings that hash alike stop the program at the second
// one instead of becoming one ID. An ID made only in a constant expression is not in the table
// until the same text is hashed at run time. The table is thread-safe.
class StringId
{
  public:
	constexpr StringId() = default;
	constexpr explicit StringId(std::string_view text) : m_hash{Fnv1a64(text)}
	{
		if (!std::is_constant_evaluated())
			Record(text);
	}

	constexpr std::uint64_t Hash() const
	{
		return m_hash;
	}
	constexpr bool IsNull() const
	{
		return m_hash == 0;
	}

	// The text the hash was made from, in development builds, while the program runs; "<unknown>"
	// for an ID the table has not seen, and in release builds.
	std::string_view Text() const;

	friend constexpr bool operator==(const StringId &, const StringId &) = default;

  private:
	// Adds the text to the development table, and verifies it does not collide with a different
	// text already there. Does nothing under NDEBUG.
	void Record(std::string_view text) const;

	std::uint64_t m_hash{0};
};
} // namespace Jettison::Core
