#pragma once

#include <entt/entt.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/script/Bindings.h>
#include <jettison/script/ScriptDefinition.h>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
struct SimEvent;
} // namespace Jettison::Sim

namespace Jettison::Script
{
// The Wasm host for mod behaviour (docs/architecture.md, "Scripts"): scripts as definitions, each a
// module read through the VFS from whichever mount has it, run by WAMR's interpreter under a fuel
// limit and a time limit, and given only what its definition grants. It calls a script from two
// places:
//   - events: each of the simulation's events, as presentation takes them once a frame, to each
//     script with a handler for its type. A handler acts through commands it is granted, into the
//     world's queue, applied at the next step and recorded as the player's are, so a replay needs
//     no script to run;
//   - inputs: a decision's consideration names an input a script answers, and WithScripts wraps the
//     game's inputs for an option so that the consideration is worked out by the script, from the
//     option's other inputs. That call is inside the simulation's step, so it is held to the
//     simulation's rules: every input call starts from the module as it was instantiated, and its
//     answer is a function of the option's inputs alone, whatever was called before it.
//
// A call that traps (out of fuel, out of bounds, an unreachable, a host call refused) loses its
// result: a handler's effects stop where it trapped, and an input reads as nothing. The trap is
// logged and the script stays loaded, its handlers' instance made again from the module, since a
// trap leaves the stack it was using where it was. A call past the time limit stops the script for
// the run: it is not called again, which is the one outcome that depends on the machine.
//
// A host is used from one thread, the one that made it; its watchdog is the only other.
using Inputs = std::function<double(std::string_view input)>;

// The ceilings a definition's asking stays within, and the time limit, which is the host's alone.
struct HostLimits
{
	std::uint32_t fuel{10'000'000};
	std::uint32_t memoryPages{256};
	// What a script may keep by key (ScriptStore): its keys and their bytes together, and how many
	// keys, so no mod grows every save. A key is 1 to kMaxKeyBytes bytes.
	std::uint32_t storeBytes{64 * 1024};
	std::uint32_t storeKeys{256};
	// A call's wall time. Fuel bounds what a script does; this bounds what fuel cannot see (a
	// machine stalled, a definition with fuel enough for seconds), well past what the most fuel
	// takes in a sanitised Debug build.
	std::chrono::milliseconds time{250};
};

// What scripts keep by key through the store and load imports, by script, then key: each script's
// own, in an encoding it chooses and migrates itself, by a version under a key of its own should it
// want one, so the host never needs its types. Not the instance's memory saved whole, which is up to
// megabytes a script and which a module rebuilt would no longer read. Handlers run outside the step,
// so this is a section of the save game (kStoreSection) and not the world's state, its hash or a
// replay. Ordered by name, so the same keys write the same bytes.
using ScriptKeys = std::map<std::string, std::vector<std::byte>, std::less<>>;
using ScriptStore = std::map<std::string, ScriptKeys, std::less<>>;

inline constexpr std::string_view kStoreSection = "Script.Store";
inline constexpr std::uint32_t kStoreVersion = 1;
inline constexpr std::size_t kMaxKeyBytes = 64;

// The store as a binary archive of the format kStoreSection, at kStoreVersion.
std::vector<std::byte> WriteStore(const ScriptStore &store);
// Throws FormatError for data that does not fit, a key that is empty or too long among it, and
// FormatVersionError for another version.
ScriptStore ReadStore(std::span<const std::byte> bytes);

// A script that does not load: a module that does not parse, an import not granted, an export
// missing or of the wrong signature, a grant of a command nothing applies. Names the script.
class ScriptError : public std::runtime_error
{
  public:
	explicit ScriptError(const std::string &message) : std::runtime_error{message}
	{
	}
};

// What became of a loaded script, for the console and the tests.
struct ScriptStatus
{
	std::string name;
	std::uint64_t calls{0};
	std::uint64_t traps{0};
	// Past its time limit, and not called again.
	bool stopped{false};
	// What it keeps by key: the keys, and their bytes and the values' together.
	std::size_t storedKeys{0};
	std::size_t storedBytes{0};
	// The last trap's message, or the reason it stopped.
	std::string lastTrap;
};

class ScriptHost
{
  public:
	explicit ScriptHost(const Vfs::FileSystem &fileSystem, HostLimits limits = {});
	~ScriptHost();
	ScriptHost(const ScriptHost &) = delete;
	ScriptHost &operator=(const ScriptHost &) = delete;

	// Where scripts' commands go: the world's queue, for a command type the world applies and
	// Reflect marks Script. Without a world, a definition granting a command does not load.
	void SendCommandsTo(Sim::SimWorld &world);

	// Loads a script under its name: its definition checked against the host's limits, its module
	// read and parsed, every import checked against what it is granted, every handler's and input's
	// export against its signature, and an instance made. Throws ScriptError naming the script and
	// what does not fit, and then nothing of it is kept. A name already loaded is replaced.
	void Load(std::string_view name, const ScriptDefinition &definition);
	// Every script the registry defines, in name order. One that does not load is logged and left
	// out, as a mod that cannot be mounted is, and the rest load. Returns how many did.
	std::size_t LoadAll(const Sim::DefinitionRegistry &definitions);
	void Unload(std::string_view name);
	void UnloadAll();

	// Gives the event to each script with a handler for its type, in the scripts' name order.
	void Dispatch(const Sim::SimEvent &event);

	// Whether a loaded script answers the input.
	bool Answers(std::string_view input) const;
	// The input from the script that answers it, which may ask game for the option's other inputs;
	// nothing for a call that trapped or a script stopped. Throws std::invalid_argument for an input
	// no script answers.
	double Input(std::string_view input, const Inputs &game);
	// The scripts' inputs over the game's: an input a script answers is the script's, and any other
	// the game's. The host outlives what it returns.
	Inputs WithScripts(Inputs game);

	// Calls an export taking and returning i32s, under the script's limits: the console's and the
	// tests' way in. Nothing when the call trapped, the reason logged; throws std::invalid_argument
	// for a script not loaded or a function it does not export with that many i32s.
	std::optional<std::vector<std::int32_t>> Call(
	    std::string_view script, std::string_view function, std::span<const std::int32_t> arguments);

	// What every script keeps by key, loaded or not: a script unloaded, or left out of a run, keeps
	// its keys for when it loads again.
	const ScriptStore &Store() const;
	// Replaces every script's keys, as a save's are loaded (ReadStore), and makes each loaded
	// script's handlers' instance again from its module, so what a script holds in memory is the
	// loaded game's and nothing of the game before it: it reads its keys again. A script whose
	// instance cannot be made is stopped, as after a trap.
	void SetStore(ScriptStore store);

	// The scripts loaded, in name order.
	std::vector<ScriptStatus> Scripts() const;
	std::size_t LoadedModules() const;

	// The bindings for script authors over every class Reflect marks Script, with a send import for
	// each that the world applies: the C header (Script::Header) and the Rust module
	// (Script::RustModule).
	std::string WriteHeader();
	std::string WriteRustModule();

	// The types Reflect marks Script, in name order.
	static std::vector<entt::meta_type> ScriptTypes();

	struct Script;

  private:
	// The types among these the world applies as commands.
	std::vector<entt::meta_type> Commands(std::span<const entt::meta_type> types) const;

	const Vfs::FileSystem &m_fileSystem;
	HostLimits m_limits;
	Sim::SimWorld *m_pWorld{nullptr};
	Bindings m_bindings;
	struct Impl;
	std::unique_ptr<Impl> m_pImpl;
};
} // namespace Jettison::Script
