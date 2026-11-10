#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/core/Log.h>

namespace Jettison::DevTools
{
// One line of the console's view: a log message, a command echoed, or a command's output.
struct ConsoleLine
{
	enum class Kind : std::uint8_t
	{
		Log,
		Input,
		Output,
		Error,
	};

	Kind kind{Kind::Output};
	Core::Severity severity{Core::Severity::Info};
	std::string category;
	std::string text;
};

// The developer console: the log, commands, console variables bound through reflection, history
// and completion. It draws nothing; Views.h puts it on screen with ImGui, and the tests drive it
// as text.
//
// A line is a command or a variable followed by arguments, separated by spaces; an argument in
// double quotes may hold spaces, and \" and \\ inside quotes are a quote and a backslash. A
// variable alone prints its value; a variable and one argument sets it. A command that throws
// prints the error: the console is a tool, and a mistyped command does not end the run.
//
// Main thread only, apart from the log capture, which takes messages from any thread.
class Console
{
  public:
	using Command = std::function<void(Console &, std::span<const std::string> arguments)>;

	// Keeps the most recent `capacity` lines. Registers help, clear and vars.
	explicit Console(std::size_t capacity = 2000);
	~Console();
	Console(const Console &) = delete;
	Console &operator=(const Console &) = delete;

	// From now on, every log message at or above the log's minimum is a line too.
	void CaptureLog();

	// Names are letters, digits, underscores and dots. Throws std::logic_error for a name that is
	// not valid or is taken by a command or a variable.
	void RegisterCommand(std::string name, std::string help, Command run);

	// Every field of the object registered with Reflect that carries Attribute::Console, as the
	// variable "<prefix>.<field>". The console refers to the object, which outlives it. A field
	// is a bool, an integer, a float, a double, a std::string or an enumeration registered with
	// Reflect; another type throws std::logic_error, as a name already taken does.
	template <typename T> void BindVariables(std::string_view prefix, T &object)
	{
		BindReference(prefix, entt::forward_as_meta(object));
	}

	void Execute(std::string_view line);
	void Print(std::string text);
	void PrintError(std::string text);
	void Clear();
	// A copy, since the log capture may be adding to them.
	std::vector<ConsoleLine> Lines() const;

	// Lines executed, oldest first, without a repeat of the line before.
	const std::vector<std::string> &History() const
	{
		return m_history;
	}

	// The commands and variables whose names start with the prefix, sorted.
	std::vector<std::string> Complete(std::string_view prefix) const;
	// The line with its first word extended as far as every candidate agrees, and a space after
	// it if only one candidate is left. Arguments are not completed.
	std::string CompleteLine(std::string_view line) const;

	// A variable's value as text, or nothing for a name that is not a variable.
	std::optional<std::string> Value(std::string_view variable) const;

  private:
	struct Variable
	{
		entt::meta_any object;
		entt::meta_data field;
	};
	struct Registered
	{
		std::string help;
		Command run;
	};

	void BindReference(std::string_view prefix, entt::meta_any object);
	void CheckName(std::string_view name) const;
	void Add(ConsoleLine line);
	void RunVariable(
	    const std::string &name, Variable &variable, std::span<const std::string> arguments);

	std::size_t m_capacity;
	mutable std::mutex m_linesMutex;
	std::deque<ConsoleLine> m_lines;
	std::optional<Core::LogCallbackId> m_logCallback;

	// Ordered, for help, vars and completion in name order.
	std::map<std::string, Registered, std::less<>> m_commands;
	std::map<std::string, Variable, std::less<>> m_variables;
	std::vector<std::string> m_history;
};

// The words of a line, as Execute splits it; nothing for an unterminated quote.
std::optional<std::vector<std::string>> SplitLine(std::string_view line);
} // namespace Jettison::DevTools
