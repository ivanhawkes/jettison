#pragma once

#include <jettison/core/Handle.h>

namespace Jettison::Resource
{
// A typed handle to a resource: an index into the manager's table for T and the generation of the
// slot when the handle was made. A handle whose generation no longer matches the slot's is stale,
// which is how a cache learns a resource was released and its slot reused, without keying on an
// address the next resource reuses (docs/architecture.md, "Lifetimes"). A hot reload keeps the
// handle and bumps the resource's revision instead (ResourceManager::RevisionOf). Null is {0, 0}.
template <typename T> using ResourceHandle = Core::Handle<T>;
} // namespace Jettison::Resource
