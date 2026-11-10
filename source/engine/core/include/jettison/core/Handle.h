#pragma once

#include <cstdint>

namespace Jettison::Core
{
// A typed handle: an index into some owner's table and the generation of that slot when the
// handle was made. A handle whose generation no longer matches the slot's is stale, and the owner
// says so instead of handing back whatever now lives at the index. Caches key on these, not on
// addresses (docs/coding-standards.md, "Ownership and lifetime"; docs/history.md, row 5). The tag
// type makes a texture handle and a model handle different types. Null is {0, 0}.
template <typename Tag> struct Handle
{
	std::uint32_t index{0};
	std::uint32_t generation{0};

	constexpr bool IsNull() const
	{
		return generation == 0;
	}
	friend constexpr bool operator==(const Handle &, const Handle &) = default;
};
} // namespace Jettison::Core
