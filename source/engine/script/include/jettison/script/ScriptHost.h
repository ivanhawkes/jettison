#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Jettison::Script
{
// The Wasm host for mod behaviour: modules loaded from mod mounts, run under fuel and time
// limits, with a capability-scoped host API generated from reflection. The runtime (WAMR or
// wasmtime) is decided when phase 11 starts, and stays inside this module.
struct ModuleHandle
{
	std::uint32_t index{0};
	std::uint32_t generation{0};
};

struct Limits
{
	std::uint64_t fuel{1'000'000}; // Instructions per call.
	std::uint32_t memoryPages{64}; // 64 KiB each.
};

class ScriptHost
{
  public:
	ModuleHandle Load(const std::string &virtualPath, const Limits &limits);
	void Unload(ModuleHandle module);
	// Calls an exported function taking and returning i32s. Nothing when the call trapped or ran
	// out of fuel; the reason is logged.
	std::optional<std::vector<std::int32_t>> Call(
	    ModuleHandle module, const std::string &function, const std::vector<std::int32_t> &arguments);
	std::size_t LoadedModules() const
	{
		return m_loadedModules;
	}

  private:
	std::size_t m_loadedModules{0};
};
} // namespace Jettison::Script
