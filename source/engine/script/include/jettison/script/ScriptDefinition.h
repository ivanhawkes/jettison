#pragma once

#include <entt/entt.hpp>

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
// (docs/scripts.md).

// A handler: the exported function called with each event of the type, by the type's registered
// name ("Dock.Landed"), as void handler(const Dock_Landed *event). Registered as
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

// A task the script ticks for a behaviour tree's task of that name (Ai::BehaviourTree::DefineTask,
// through ScriptHost::Task): the exported function, as void task(Script_TaskContext *context), and
// the one called when the task is left running, if it has one (Ai::TaskHalt), as
// void halt(const Script_TaskContext *context). Registered as "Script.TaskFunction".
struct TaskFunction
{
	std::string task;
	std::string function;
	std::string halt;

	bool operator==(const TaskFunction &) const = default;
};

// What a task is when the script's tick returns, as Ai::Status says it. Registered as
// "Script.TaskStatus".
enum class TaskStatus : std::uint8_t
{
	Running,
	Success,
	Failure
};

// What a task the script answers is given at each tick, in its memory as Script_TaskContext: the
// entity whose tree ticks it, the node's arguments, whether this is its first tick since it last
// finished, and the 64-bit memory the tree saves for it, which the script may change. The script
// writes the status, which is Running as it is given. Registered as "Script.TaskContext", for
// scripts to see (Reflect::Attribute::Script).
struct TaskContext
{
	entt::entity entity{entt::null};
	std::vector<std::string> arguments;
	bool starting{false};
	std::uint64_t memory{0};
	TaskStatus status{TaskStatus::Running};
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
	std::vector<TaskFunction> tasks;

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
