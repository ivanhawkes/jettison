#include <jettison/script/ScriptHost.h>

#include <jettison/core/Log.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/CommandQueue.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/vfs/FileSystem.h>

#include <jettison_wamr_print.h>
#include <wasm_export.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace Jettison::Script
{
namespace
{
constexpr Core::Category kScript{"script"};
constexpr const char *kHostModule = "jettison";
// The interpreter's operand stack and call frames, a call's.
constexpr std::uint32_t kStackBytes = 64 * 1024;
// The host's heap in a handler's memory, where an event is copied for it.
constexpr std::uint32_t kHeapBytes = 64 * 1024;
// The traps a script logs one by one before the rest are counted.
constexpr std::uint64_t kTrapsLogged = 10;
// How often the watchdog looks at the call running.
constexpr std::chrono::milliseconds kWatchPeriod{5};

// WAMR's messages, a line at a time: os_printf is called with pieces of a line.
int PrintToLog(const char *format, va_list arguments)
{
	thread_local std::string line;
	std::array<char, 512> buffer{};
	va_list copy;
	va_copy(copy, arguments);
	const int written = std::vsnprintf(buffer.data(), buffer.size(), format, copy);
	va_end(copy);
	if (written <= 0)
		return written;
	line.append(
	    buffer.data(), std::min<std::size_t>(static_cast<std::size_t>(written), buffer.size() - 1));
	for (std::size_t end = line.find('\n'); end != std::string::npos; end = line.find('\n'))
	{
		if (end > 0)
			Core::Log(
			    Core::Severity::Warning, kScript, "WAMR: {}", std::string_view{line}.substr(0, end));
		line.erase(0, end + 1);
	}
	return written;
}

void LogNative(
    wasm_exec_env_t environment, std::int32_t severity, const char *pText, std::uint32_t size);
double InputNative(wasm_exec_env_t environment, const char *pName, std::uint32_t size);
void SendNative(wasm_exec_env_t environment, std::uint32_t offset);

// The process's runtime, started by the first host and destroyed with the last. WAMR's natives are
// the process's too, registered by name under the module "jettison": the fixed ones as the runtime
// starts, and a send for each command as the first script granted it loads, its attachment the
// command's type. Which a module may import is each host's to check, as the module loads.
class Runtime
{
  public:
	static Runtime &Get()
	{
		static Runtime runtime;
		return runtime;
	}

	void Acquire()
	{
		if (m_users++ > 0)
			return;
		jettison_wamr_set_print(&PrintToLog);
		RuntimeInitArgs init{};
		init.mem_alloc_type = Alloc_With_System_Allocator;
		if (!wasm_runtime_full_init(&init))
		{
			m_users = 0;
			throw std::runtime_error{"Script: the WebAssembly runtime did not start"};
		}
		wasm_runtime_set_log_level(WASM_LOG_LEVEL_WARNING);
		if (!wasm_runtime_register_natives(
		        kHostModule, m_fixed.data(), static_cast<std::uint32_t>(m_fixed.size())))
			throw std::runtime_error{"Script: the host's functions could not be registered"};
	}

	void Release()
	{
		if (--m_users > 0)
			return;
		wasm_runtime_destroy();
		// WAMR's lists of natives went with it; the next start registers its own.
		m_sends.clear();
	}

	// The send native for a command type, registered the first time it is asked for.
	void EnsureSend(const entt::meta_type &type)
	{
		const entt::id_type id = type.info().hash();
		if (m_sends.contains(id))
			return;
		auto pSend = std::make_unique<Send>();
		pSend->type = type;
		pSend->name = SendImport(type);
		pSend->symbol = NativeSymbol{
		    pSend->name.c_str(), reinterpret_cast<void *>(&SendNative), "(i)", &pSend->type};
		if (!wasm_runtime_register_natives(kHostModule, &pSend->symbol, 1))
			throw std::runtime_error{std::format("Script: '{}' could not be registered", pSend->name)};
		m_sends.emplace(id, std::move(pSend));
	}

  private:
	struct Send
	{
		entt::meta_type type;
		std::string name;
		NativeSymbol symbol{};
	};

	Runtime()
	    : m_fixed{{
	          {"log", reinterpret_cast<void *>(&LogNative), "(i*~)", nullptr},
	          {"input", reinterpret_cast<void *>(&InputNative), "(*~)F", nullptr},
	      }}
	{
	}

	int m_users{0};
	// WAMR keeps these pointers: the arrays live as long as the process.
	std::array<NativeSymbol, 2> m_fixed;
	std::map<entt::id_type, std::unique_ptr<Send>> m_sends;
};

// Stops a call past its time limit. It looks every few milliseconds rather than being woken for
// each call, so a call costs a lock and a clock read, not a context switch; a call it stops has
// overrun by a period at most. The lock is held while it terminates, so the call it stops is the
// one it looked at.
class Watchdog
{
  public:
	Watchdog() : m_thread{[this] { Watch(); }}
	{
	}
	~Watchdog()
	{
		{
			const std::scoped_lock lock{m_mutex};
			m_stop = true;
		}
		m_wake.notify_one();
		m_thread.join();
	}
	Watchdog(const Watchdog &) = delete;
	Watchdog &operator=(const Watchdog &) = delete;

	void Arm(wasm_module_inst_t instance, std::chrono::milliseconds limit)
	{
		const std::scoped_lock lock{m_mutex};
		m_running = instance;
		m_deadline = std::chrono::steady_clock::now() + limit;
		m_fired = false;
	}
	// Whether it stopped the call.
	bool Disarm()
	{
		const std::scoped_lock lock{m_mutex};
		m_running = nullptr;
		return m_fired;
	}

  private:
	void Watch()
	{
		std::unique_lock lock{m_mutex};
		while (!m_stop)
		{
			m_wake.wait_for(lock, kWatchPeriod);
			if (m_running != nullptr && !m_fired && std::chrono::steady_clock::now() >= m_deadline)
			{
				wasm_runtime_terminate(m_running);
				m_fired = true;
			}
		}
	}

	std::mutex m_mutex;
	std::condition_variable m_wake;
	bool m_stop{false};
	wasm_module_inst_t m_running{nullptr};
	std::chrono::steady_clock::time_point m_deadline;
	bool m_fired{false};
	std::thread m_thread;
};

std::string_view Text(const char *pText, std::uint32_t size)
{
	return pText != nullptr ? std::string_view{pText, size} : std::string_view{};
}

bool HasSignature(
    wasm_function_inst_t function, wasm_module_inst_t instance,
    std::span<const wasm_valkind_t> parameters, std::span<const wasm_valkind_t> results)
{
	if (wasm_func_get_param_count(function, instance) != parameters.size() ||
	    wasm_func_get_result_count(function, instance) != results.size())
		return false;
	std::array<wasm_valkind_t, 8> kinds{};
	if (parameters.size() > kinds.size() || results.size() > kinds.size())
		return false;
	wasm_func_get_param_types(function, instance, kinds.data());
	if (!std::equal(parameters.begin(), parameters.end(), kinds.begin()))
		return false;
	wasm_func_get_result_types(function, instance, kinds.data());
	return std::equal(results.begin(), results.end(), kinds.begin());
}

std::string Join(std::span<const std::string> items)
{
	std::string list;
	for (const std::string &item : items)
		list += (list.empty() ? "" : ", ") + item;
	return list;
}

constexpr std::array<wasm_valkind_t, 1> kOneI32{WASM_I32};
constexpr std::array<wasm_valkind_t, 1> kOneF64{WASM_F64};
} // namespace

// One instance of a module and the environment its calls run in.
struct Instance
{
	wasm_module_inst_t instance{nullptr};
	wasm_exec_env_t environment{nullptr};

	Instance() = default;
	Instance(const Instance &) = delete;
	Instance &operator=(const Instance &) = delete;
	~Instance()
	{
		if (environment != nullptr)
			wasm_runtime_destroy_exec_env(environment);
		if (instance != nullptr)
			wasm_runtime_deinstantiate(instance);
	}

	std::span<std::byte> Memory() const
	{
		wasm_memory_inst_t memory = wasm_runtime_get_default_memory(instance);
		if (memory == nullptr)
			return {};
		const std::uint64_t bytes =
		    wasm_memory_get_cur_page_count(memory) * wasm_memory_get_bytes_per_page(memory);
		return {
		    static_cast<std::byte *>(wasm_memory_get_base_address(memory)),
		    static_cast<std::size_t>(bytes)};
	}
};

struct ScriptHost::Impl
{
	ScriptHost &host;
	std::map<std::string, std::unique_ptr<Script>, std::less<>> scripts;
	// Which script answers each input.
	std::map<std::string, Script *, std::less<>> answers;
	std::unique_ptr<Watchdog> pWatchdog;

	explicit Impl(ScriptHost &owner) : host{owner}
	{
		Runtime::Get().Acquire();
	}

	// A command a script sends: read from its memory as the command's layout says, and queued.
	void Send(const entt::meta_type &type, std::uint32_t offset, wasm_exec_env_t environment);
	~Impl()
	{
		answers.clear();
		scripts.clear();
		pWatchdog.reset();
		Runtime::Get().Release();
	}
	Impl(const Impl &) = delete;
	Impl &operator=(const Impl &) = delete;
};

struct ScriptHost::Script
{
	ScriptHost::Impl *pHost{nullptr};
	std::string name;
	ScriptDefinition definition;
	// The module's bytes, which WAMR may refer to for as long as the module is loaded.
	std::vector<std::uint8_t> bytes;
	wasm_module_t module{nullptr};
	// The pages its memory may grow to: its definition's, or fewer where the module says so (WAMR
	// holds a module that never grows its memory to the pages it starts with).
	std::uint32_t memoryPages{0};
	// The handlers' instance, kept from call to call, so a handler may count what it has seen.
	std::unique_ptr<Instance> pHandlers;

	struct Handler
	{
		entt::id_type event{0};
		std::string function;
		const TypeLayout *pLayout{nullptr};
	};
	std::vector<Handler> handlers;

	// While it answers an input: the game's inputs for the option, which its input import reads.
	const Inputs *pGame{nullptr};
	// What a host function refused, in full; WAMR keeps 128 bytes of a trap's message.
	std::string refusal;
	ScriptStatus status;

	Script() = default;
	Script(const Script &) = delete;
	Script &operator=(const Script &) = delete;
	~Script()
	{
		pHandlers.reset();
		if (module != nullptr)
			wasm_runtime_unload(module);
	}
};

namespace
{
using HostScript = ScriptHost::Script;

HostScript *CallingScript(wasm_exec_env_t environment)
{
	return static_cast<HostScript *>(
	    wasm_runtime_get_custom_data(wasm_runtime_get_module_inst(environment)));
}

// Traps the call with a host function's refusal, keeping the whole message for the log.
void Refuse(wasm_exec_env_t environment, HostScript &script, std::string message)
{
	script.refusal = std::move(message);
	wasm_runtime_set_exception(wasm_runtime_get_module_inst(environment), script.refusal.c_str());
}

void LogNative(
    wasm_exec_env_t environment, std::int32_t severity, const char *pText, std::uint32_t size)
{
	const HostScript *pScript = CallingScript(environment);
	const Core::Severity level = severity <= 0   ? Core::Severity::Debug
	                             : severity == 1 ? Core::Severity::Info
	                             : severity == 2 ? Core::Severity::Warning
	                                             : Core::Severity::Error;
	Core::Log(level, kScript, "{}: {}", pScript ? pScript->name : "?", Text(pText, size));
}

double InputNative(wasm_exec_env_t environment, const char *pName, std::uint32_t size)
{
	HostScript *pScript = CallingScript(environment);
	if (pScript == nullptr)
		return 0.0;
	const std::string_view name = Text(pName, size);
	if (pScript->pGame == nullptr)
	{
		Refuse(
		    environment, *pScript,
		    std::format("asked for input '{}' outside an input of its own", name));
		return 0.0;
	}
	// The game's inputs throw for a name they do not know; nothing may unwind through the
	// interpreter's frames, so it is a trap instead.
	try
	{
		return (*pScript->pGame)(name);
	}
	catch (const std::exception &error)
	{
		Refuse(environment, *pScript, std::format("input '{}': {}", name, error.what()));
		return 0.0;
	}
}

void SendNative(wasm_exec_env_t environment, std::uint32_t offset)
{
	HostScript *pScript = CallingScript(environment);
	if (pScript == nullptr)
		return;
	const auto *pType =
	    static_cast<const entt::meta_type *>(wasm_runtime_get_function_attachment(environment));
	if (pScript->pGame != nullptr)
	{
		Refuse(
		    environment, *pScript, std::format("sent a '{}' while answering an input", pType->name()));
		return;
	}
	try
	{
		pScript->pHost->Send(*pType, offset, environment);
	}
	catch (const std::exception &error)
	{
		Refuse(environment, *pScript, error.what());
	}
}
} // namespace

ScriptHost::ScriptHost(const Vfs::FileSystem &fileSystem, HostLimits limits)
    : m_fileSystem{fileSystem}, m_limits{limits}, m_pImpl{std::make_unique<Impl>(*this)}
{
}

ScriptHost::~ScriptHost() = default;

void ScriptHost::SendCommandsTo(Sim::SimWorld &world)
{
	m_pWorld = &world;
}

namespace
{
std::unique_ptr<Instance> Instantiate(
    HostScript &script, wasm_module_t module, std::uint32_t heapBytes = kHeapBytes)
{
	std::array<char, 256> error{};
	InstantiationArgs arguments{};
	arguments.default_stack_size = kStackBytes;
	arguments.host_managed_heap_size = heapBytes;
	arguments.max_memory_pages = script.memoryPages;
	auto pInstance = std::make_unique<Instance>();
	pInstance->instance = wasm_runtime_instantiate_ex(
	    module, &arguments, error.data(), static_cast<std::uint32_t>(error.size()));
	if (pInstance->instance == nullptr)
		throw ScriptError{
		    std::format("Script '{}': it could not be instantiated: {}", script.name, error.data())};
	wasm_runtime_set_custom_data(pInstance->instance, &script);
	pInstance->environment = wasm_runtime_create_exec_env(pInstance->instance, kStackBytes);
	if (pInstance->environment == nullptr)
		throw ScriptError{
		    std::format("Script '{}': no environment could be made to call it in", script.name)};
	return pInstance;
}

// Runs a call under the script's fuel and the host's time limit. The arguments are WAMR's cells, and
// hold the results after.
bool Run(
    HostScript &script, Instance &instance, Watchdog &watchdog, std::chrono::milliseconds limit,
    std::string_view function, std::span<std::uint32_t> cells, std::uint32_t argumentCells)
{
	wasm_function_inst_t pFunction =
	    wasm_runtime_lookup_function(instance.instance, std::string{function}.c_str());
	if (pFunction == nullptr)
		throw std::logic_error{std::format("Script '{}' has no export '{}'", script.name, function)};
	script.refusal.clear();
	wasm_runtime_set_instruction_count_limit(
	    instance.environment,
	    static_cast<int>(std::min<std::uint32_t>(script.definition.fuel, INT32_MAX)));
	watchdog.Arm(instance.instance, limit);
	const bool returned =
	    wasm_runtime_call_wasm(instance.environment, pFunction, argumentCells, cells.data());
	const bool stopped = watchdog.Disarm();
	++script.status.calls;
	if (returned)
	{
		// The watchdog may have looked as the call returned: what it set would end the next call.
		if (stopped)
			wasm_runtime_clear_exception(instance.instance);
		return true;
	}

	const char *pException = wasm_runtime_get_exception(instance.instance);
	const std::string why =
	    !script.refusal.empty() ? script.refusal : (pException ? pException : "it trapped");
	wasm_runtime_clear_exception(instance.instance);
	script.status.lastTrap = std::format("{}: {}", function, why);
	if (stopped)
	{
		script.status.stopped = true;
		Core::Log(
		    Core::Severity::Error, kScript,
		    "Script '{}': {} ran past the time limit of {} ms, and the script is not called again this "
		    "run",
		    script.name, function, limit.count());
		return false;
	}
	if (++script.status.traps <= kTrapsLogged)
		Core::Log(
		    Core::Severity::Error, kScript, "Script '{}': {} trapped: {}", script.name, function, why);
	if (script.status.traps == kTrapsLogged)
		Core::Log(
		    Core::Severity::Error, kScript, "Script '{}': its further traps are counted and not logged",
		    script.name);
	return false;
}
} // namespace

void ScriptHost::Load(std::string_view name, const ScriptDefinition &definition)
{
	const auto fail = [&](std::string_view what) -> ScriptError
	{ return ScriptError{std::format("Script '{}': {}", name, what)}; };

	if (definition.module.empty())
		throw fail("it names no module");
	if (definition.fuel == 0 || definition.fuel > m_limits.fuel)
		throw fail(
		    std::format(
		        "its fuel of {} is not between 1 and the host's {}", definition.fuel, m_limits.fuel));
	if (definition.memoryPages == 0 || definition.memoryPages > m_limits.memoryPages)
		throw fail(
		    std::format(
		        "its {} pages of memory are not between 1 and the host's {}", definition.memoryPages,
		        m_limits.memoryPages));

	// The imports it may make: "log", "input" for a script that answers inputs, and a send for each
	// command it is granted.
	std::set<std::string, std::less<>> allowed;
	for (const std::string &capability : definition.capabilities)
	{
		if (capability == kLogCapability)
		{
			allowed.insert(capability);
			continue;
		}
		constexpr std::string_view kSend = "send:";
		if (!capability.starts_with(kSend))
			throw fail(std::format("'{}' is not a capability: log, or send:<command>", capability));
		const entt::meta_type type =
		    Reflect::Resolve(std::string_view{capability}.substr(kSend.size()));
		if (!type || !Reflect::HasAll(Reflect::AttributesOf(type), Reflect::Attribute::Script))
			throw fail(std::format("'{}' grants a type scripts do not see", capability));
		if (m_pWorld == nullptr || !m_pWorld->Accepts(type))
			throw fail(std::format("'{}' grants a command nothing applies", capability));
		m_bindings.Layout(type);
		Runtime::Get().EnsureSend(type);
		allowed.insert(capability);
	}
	if (!definition.inputs.empty())
		allowed.emplace(kInputImport);

	auto pScript = std::make_unique<Script>();
	pScript->pHost = m_pImpl.get();
	pScript->name = std::string{name};
	pScript->definition = definition;
	pScript->status.name = pScript->name;

	for (const EventHandler &handler : definition.events)
	{
		const entt::meta_type type = Reflect::Resolve(handler.event);
		if (!type || !Reflect::HasAll(Reflect::AttributesOf(type), Reflect::Attribute::Script))
			throw fail(
			    std::format("it handles '{}', which is not an event scripts see", handler.event));
		if (handler.function.empty())
			throw fail(std::format("its handler of '{}' names no function", handler.event));
		pScript->handlers.push_back({type.info().hash(), handler.function, &m_bindings.Layout(type)});
	}
	std::set<std::string_view> inputs;
	for (const InputFunction &input : definition.inputs)
	{
		if (input.input.empty() || input.function.empty())
			throw fail("an input needs a name and a function");
		if (!inputs.insert(input.input).second)
			throw fail(std::format("it answers input '{}' twice", input.input));
		if (const auto other = m_pImpl->answers.find(input.input);
		    other != m_pImpl->answers.end() && other->second->name != name)
			throw fail(
			    std::format(
			        "input '{}' is answered by script '{}' already", input.input, other->second->name));
	}

	const std::optional<std::vector<std::byte>> bytes = m_fileSystem.Read(definition.module);
	if (!bytes)
		throw fail(std::format("there is no module at '{}'", definition.module));
	pScript->bytes.resize(bytes->size());
	std::memcpy(pScript->bytes.data(), bytes->data(), bytes->size());
	std::array<char, 256> error{};
	pScript->module = wasm_runtime_load(
	    pScript->bytes.data(), static_cast<std::uint32_t>(pScript->bytes.size()), error.data(),
	    static_cast<std::uint32_t>(error.size()));
	if (pScript->module == nullptr)
		throw fail(std::format("'{}' is not a module it can run: {}", definition.module, error.data()));

	pScript->memoryPages = definition.memoryPages;
	const std::int32_t exports = wasm_runtime_get_export_count(pScript->module);
	for (std::int32_t index = 0; index < exports; ++index)
	{
		wasm_export_t exported{};
		wasm_runtime_get_export_type(pScript->module, index, &exported);
		if (exported.kind != WASM_IMPORT_EXPORT_KIND_MEMORY)
			continue;
		const std::uint32_t initial = wasm_memory_type_get_init_page_count(exported.u.memory_type);
		if (initial > definition.memoryPages)
			throw fail(
			    std::format(
			        "its module starts with {} pages of memory, more than the {} it is allowed",
			        initial, definition.memoryPages));
		pScript->memoryPages = std::min(
		    definition.memoryPages, wasm_memory_type_get_max_page_count(exported.u.memory_type));
	}

	const std::int32_t imports = wasm_runtime_get_import_count(pScript->module);
	for (std::int32_t index = 0; index < imports; ++index)
	{
		wasm_import_t import{};
		wasm_runtime_get_import_type(pScript->module, index, &import);
		const std::string_view module =
		    import.module_name ? std::string_view{import.module_name} : std::string_view{};
		const std::string_view imported =
		    import.name ? std::string_view{import.name} : std::string_view{};
		if (module != kHostModule || import.kind != WASM_IMPORT_EXPORT_KIND_FUNC)
			throw fail(
			    std::format(
			        "it imports '{}' from '{}', which the host does not offer", imported, module));
		if (!allowed.contains(imported))
			throw fail(std::format("it imports '{}', which it is not granted", imported));
		if (!import.linked)
			throw fail(std::format("it imports '{}' with another signature than the host's", imported));
	}

	pScript->pHandlers = Instantiate(*pScript, pScript->module);
	for (const Script::Handler &handler : pScript->handlers)
	{
		wasm_function_inst_t pFunction =
		    wasm_runtime_lookup_function(pScript->pHandlers->instance, handler.function.c_str());
		if (pFunction == nullptr || !HasSignature(pFunction, pScript->pHandlers->instance, kOneI32, {}))
			throw fail(
			    std::format(
			        "its handler '{}' is not exported as void {}(const {} *)", handler.function,
			        handler.function, handler.pLayout->cName));
	}
	for (const InputFunction &input : definition.inputs)
	{
		wasm_function_inst_t pFunction =
		    wasm_runtime_lookup_function(pScript->pHandlers->instance, input.function.c_str());
		if (pFunction == nullptr || !HasSignature(pFunction, pScript->pHandlers->instance, {}, kOneF64))
			throw fail(
			    std::format(
			        "its input '{}' is not exported as double {}(void)", input.function,
			        input.function));
	}

	if (!m_pImpl->pWatchdog)
		m_pImpl->pWatchdog = std::make_unique<Watchdog>();
	Unload(name);
	Script &loaded = *m_pImpl->scripts.emplace(std::string{name}, std::move(pScript)).first->second;
	for (const InputFunction &input : loaded.definition.inputs)
		m_pImpl->answers.emplace(input.input, &loaded);
	Core::Log(
	    Core::Severity::Info, kScript, "Script '{}': {} loaded, {} handler(s), {} input(s), granted {}",
	    loaded.name, loaded.definition.module, loaded.handlers.size(), loaded.definition.inputs.size(),
	    loaded.definition.capabilities.empty() ? std::string{"nothing"}
	                                           : Join(loaded.definition.capabilities));
}

std::size_t ScriptHost::LoadAll(const Sim::DefinitionRegistry &definitions)
{
	std::size_t loaded = 0;
	for (const std::string &name : definitions.Names<ScriptDefinition>())
	{
		try
		{
			Load(name, *definitions.Find<ScriptDefinition>(name));
			++loaded;
		}
		catch (const std::exception &error)
		{
			Core::Log(Core::Severity::Error, kScript, "{}; it is left out", error.what());
		}
	}
	return loaded;
}

void ScriptHost::Unload(std::string_view name)
{
	const auto found = m_pImpl->scripts.find(name);
	if (found == m_pImpl->scripts.end())
		return;
	std::erase_if(
	    m_pImpl->answers, [&](const auto &answer) { return answer.second == found->second.get(); });
	m_pImpl->scripts.erase(found);
}

void ScriptHost::UnloadAll()
{
	m_pImpl->answers.clear();
	m_pImpl->scripts.clear();
}

void ScriptHost::Dispatch(const Sim::SimEvent &event)
{
	const entt::meta_type type = event.event.type();
	if (!type)
		return;
	const entt::id_type id = type.info().hash();
	for (const auto &[name, pScript] : m_pImpl->scripts)
	{
		Script &script = *pScript;
		for (const Script::Handler &handler : script.handlers)
		{
			if (handler.event != id || script.status.stopped)
				continue;
			// A trap made the instance again; the handlers' instance is whatever it is now.
			Instance &instance = *script.pHandlers;
			const std::uint32_t size = m_bindings.SizeOf(*handler.pLayout, ObjectOf(event.event));
			void *pNative = nullptr;
			const std::uint64_t offset = wasm_runtime_module_malloc(instance.instance, size, &pNative);
			if (offset == 0)
			{
				++script.status.traps;
				script.status.lastTrap =
				    std::format("no room in its heap for a {} of {} bytes", type.name(), size);
				Core::Log(
				    Core::Severity::Error, kScript, "Script '{}': {}", script.name,
				    script.status.lastTrap);
				continue;
			}
			m_bindings.Write(
			    *handler.pLayout, ObjectOf(event.event), instance.Memory(),
			    static_cast<std::uint32_t>(offset));
			std::array<std::uint32_t, 1> cells{static_cast<std::uint32_t>(offset)};
			if (Run(script, instance, *m_pImpl->pWatchdog, m_limits.time, handler.function, cells, 1))
			{
				wasm_runtime_module_free(instance.instance, offset);
				continue;
			}
			if (script.status.stopped)
				break;
			// A trap leaves the shadow stack where it was and the heap as it was: the handlers start
			// again from the module, and what they had kept is lost.
			try
			{
				script.pHandlers = Instantiate(script, script.module);
			}
			catch (const ScriptError &error)
			{
				script.status.stopped = true;
				Core::Log(
				    Core::Severity::Error, kScript, "{}; it is not called again this run",
				    error.what());
				break;
			}
		}
	}
}

bool ScriptHost::Answers(std::string_view input) const
{
	return m_pImpl->answers.contains(input);
}

double ScriptHost::Input(std::string_view input, const Inputs &game)
{
	const auto found = m_pImpl->answers.find(input);
	if (found == m_pImpl->answers.end())
		throw std::invalid_argument{std::format("Script: no script answers input '{}'", input)};
	Script &script = *found->second;
	if (script.status.stopped)
		return 0.0;
	const auto answer = std::ranges::find(script.definition.inputs, input, &InputFunction::input);

	// Each input call starts from the module as it was instantiated, so its answer depends on the
	// option's inputs and on nothing a call before it did.
	std::unique_ptr<Instance> pInstance;
	try
	{
		pInstance = Instantiate(script, script.module, 0);
	}
	catch (const ScriptError &error)
	{
		script.status.stopped = true;
		Core::Log(Core::Severity::Error, kScript, "{}; it is not called again this run", error.what());
		return 0.0;
	}
	script.pGame = &game;
	std::array<std::uint32_t, 2> cells{};
	const bool returned =
	    Run(script, *pInstance, *m_pImpl->pWatchdog, m_limits.time, answer->function, cells, 0);
	script.pGame = nullptr;
	if (!returned)
		return 0.0;
	double value = 0.0;
	std::memcpy(&value, cells.data(), sizeof(value));
	if (!std::isfinite(value))
	{
		++script.status.traps;
		script.status.lastTrap = std::format("{}: answered {}", answer->function, value);
		Core::Log(
		    Core::Severity::Error, kScript, "Script '{}': input '{}' answered {}, read as 0",
		    script.name, input, value);
		return 0.0;
	}
	return value;
}

Inputs ScriptHost::WithScripts(Inputs game)
{
	return [this, game = std::move(game)](std::string_view input)
	{ return Answers(input) ? Input(input, game) : game(input); };
}

std::optional<std::vector<std::int32_t>> ScriptHost::Call(
    std::string_view name, std::string_view function, std::span<const std::int32_t> arguments)
{
	const auto found = m_pImpl->scripts.find(name);
	if (found == m_pImpl->scripts.end())
		throw std::invalid_argument{std::format("Script: no script '{}' is loaded", name)};
	Script &script = *found->second;
	if (script.status.stopped)
		return std::nullopt;
	Instance &instance = *script.pHandlers;
	wasm_function_inst_t pFunction =
	    wasm_runtime_lookup_function(instance.instance, std::string{function}.c_str());
	if (pFunction == nullptr)
		throw std::invalid_argument{std::format("Script '{}' does not export '{}'", name, function)};
	const std::vector<wasm_valkind_t> parameters(arguments.size(), WASM_I32);
	const std::uint32_t results = wasm_func_get_result_count(pFunction, instance.instance);
	const std::vector<wasm_valkind_t> resultKinds(results, WASM_I32);
	if (!HasSignature(pFunction, instance.instance, parameters, resultKinds))
		throw std::invalid_argument{std::format(
		    "Script '{}': '{}' does not take {} i32s and return i32s", name, function,
		    arguments.size())};

	std::vector<std::uint32_t> cells(std::max<std::size_t>(arguments.size(), results));
	std::ranges::transform(
	    arguments, cells.begin(), [](std::int32_t value) { return static_cast<std::uint32_t>(value); });
	if (!Run(
	        script, instance, *m_pImpl->pWatchdog, m_limits.time, function, cells,
	        static_cast<std::uint32_t>(arguments.size())))
	{
		if (!script.status.stopped)
			script.pHandlers = Instantiate(script, script.module);
		return std::nullopt;
	}
	std::vector<std::int32_t> returned(results);
	std::ranges::transform(
	    cells.begin(), cells.begin() + results, returned.begin(),
	    [](std::uint32_t cell) { return static_cast<std::int32_t>(cell); });
	return returned;
}

void ScriptHost::Impl::Send(
    const entt::meta_type &type, std::uint32_t offset, wasm_exec_env_t environment)
{
	if (host.m_pWorld == nullptr)
		throw std::logic_error{std::format("sent a '{}' with no world to send it to", type.name())};
	const TypeLayout &layout = host.m_bindings.Layout(type);
	wasm_memory_inst_t memory =
	    wasm_runtime_get_default_memory(wasm_runtime_get_module_inst(environment));
	if (memory == nullptr)
		throw BindingError{"Script: the module has no memory to send from"};
	const std::span<const std::byte> bytes{
	    static_cast<const std::byte *>(wasm_memory_get_base_address(memory)),
	    static_cast<std::size_t>(
	        wasm_memory_get_cur_page_count(memory) * wasm_memory_get_bytes_per_page(memory))};
	entt::meta_any command = type.construct();
	host.m_bindings.Read(layout, bytes, offset, ObjectOf(command));
	host.m_pWorld->Commands().Push(std::move(command));
}

std::vector<ScriptStatus> ScriptHost::Scripts() const
{
	std::vector<ScriptStatus> statuses;
	for (const auto &[name, pScript] : m_pImpl->scripts)
		statuses.push_back(pScript->status);
	return statuses;
}

std::size_t ScriptHost::LoadedModules() const
{
	return m_pImpl->scripts.size();
}

std::vector<entt::meta_type> ScriptHost::ScriptTypes()
{
	std::vector<entt::meta_type> types;
	for (const auto &[id, type] : entt::resolve())
		if (type.is_class() && Reflect::HasAll(Reflect::AttributesOf(type), Reflect::Attribute::Script))
			types.push_back(type);
	std::ranges::sort(types, {}, [](const entt::meta_type &type) { return type.name(); });
	return types;
}

std::string ScriptHost::WriteHeader()
{
	const std::vector<entt::meta_type> types = ScriptTypes();
	return Header(m_bindings, types, Commands(types));
}

std::string ScriptHost::WriteRustModule()
{
	const std::vector<entt::meta_type> types = ScriptTypes();
	return RustModule(m_bindings, types, Commands(types));
}

std::vector<entt::meta_type> ScriptHost::Commands(std::span<const entt::meta_type> types) const
{
	std::vector<entt::meta_type> commands;
	for (const entt::meta_type &type : types)
		if (m_pWorld != nullptr && m_pWorld->Accepts(type))
			commands.push_back(type);
	return commands;
}
} // namespace Jettison::Script
