#pragma once

#include <cstdint>

namespace Jettison::Resource
{
// A typed handle to a loaded resource: an index into the manager's table and the generation of
// the slot when the handle was made. A handle whose generation no longer matches the slot's is
// stale, which is how the renderer's cache learns a model was reloaded without keying on an
// address the next model reuses (docs/architecture.md, "Lifetimes"). Null is {0, 0}.
template <typename T> struct ResourceHandle
{
	std::uint32_t index{0};
	std::uint32_t generation{0};

	bool IsNull() const
	{
		return generation == 0;
	}
	friend bool operator==(const ResourceHandle &, const ResourceHandle &) = default;
};
} // namespace Jettison::Resource
