#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Sim
{
class DefinitionRegistry;
}

namespace Jettison::Script
{
// A script: defs/scripts/<name>.yaml, loaded through Sim::DefinitionRegistry, so a mod adds one
// with a file and changes another's grants or fuel with a patch, as it would a good's price. It
// names its module (a .wasm through the VFS, usually beside it in the same mod), what each call
// may spend, what the module is granted, and which of its exports the host calls, and for what
// (docs/architecture.md, "Scripts").

// A handler: the exported function called with each event of the type, by the type's registered
// name ("Shell.Traded"), as void handler(const Shell_Traded *event). Registered as
// "Script.EventHandler".
struct EventHandler
{
	std::string event;
	std::string function;

	bool operator==(const EventHandler &) const = default;
};

// An input the script answers for a decision's considerations: the name a consideration asks for,
// and the exported function that works it out, as double input(void), reading the option's other
// inputs through jettison_input. Registered as "Script.InputFunction".
struct InputFunction
{
	std::string input;
	std::string function;

	bool operator==(const InputFunction &) const = default;
};

// Registered as "Script.ScriptDefinition", Moddable, every field Saved.
struct ScriptDefinition
{
	// The module's virtual path.
	std::string module;
	// The interpreter's instructions a call may run before it traps: the same count on every run,
	// so a script out of fuel stops at the same place in every build.
	std::uint32_t fuel{100'000};
	// The most the module's linear memory may grow to, in 64 KiB pages, its stack and the host's
	// heap for what it is given among them.
	std::uint32_t memoryPages{16};
	// What the module may import beyond answering inputs: "log"; "store", for the imports store and
	// load, which keep bytes by key in the game's saves; and "send:<Command>" for each command it may
	// send, by the command's registered name.
	std::vector<std::string> capabilities;
	std::vector<EventHandler> events;
	std::vector<InputFunction> inputs;

	bool operator==(const ScriptDefinition &) const = default;
};

inline constexpr std::uint32_t kScriptVersion = 1;
inline constexpr std::string_view kScriptDirectory = "defs/scripts";
inline constexpr std::string_view kLogCapability = "log";
inline constexpr std::string_view kStoreCapability = "store";
inline constexpr std::string_view kStoreImport = "store";
inline constexpr std::string_view kLoadImport = "load";
inline constexpr std::string_view kInputImport = "input";

// Registers the types with Reflect, once however often it is called.
void RegisterTypes();
// Every script definition under defs/scripts into the registry. Throws as the registry's Load does.
void LoadDefinitions(Sim::DefinitionRegistry &definitions);
} // namespace Jettison::Script
