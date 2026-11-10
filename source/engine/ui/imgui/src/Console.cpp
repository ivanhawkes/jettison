#include <jettison/devtools/Console.h>

#include <jettison/reflect/Reflect.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace Jettison::DevTools
{
namespace
{
// How a variable's type is shown and set. Integers are parsed at their own width, so a value out
// of the type's range is refused rather than wrapped.
struct Codec
{
	std::string (*format)(const entt::meta_any &);
	std::optional<entt::meta_any> (*parse)(std::string_view);
	std::string_view what;
};

template <typename T> std::string FormatNumber(const entt::meta_any &value)
{
	std::array<char, 32> buffer{};
	const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value.cast<T>());
	return {buffer.data(), result.ptr};
}

template <typename T> std::optional<entt::meta_any> ParseNumber(std::string_view text)
{
	if (text.starts_with('+'))
		text.remove_prefix(1);
	T value{};
	const char *const first = std::to_address(text.begin());
	const char *const last = std::to_address(text.end());
	const auto [stop, error] = std::from_chars(first, last, value);
	if (text.empty() || error != std::errc{} || stop != last)
		return std::nullopt;
	return entt::meta_any{value};
}

std::string FormatBool(const entt::meta_any &value)
{
	return value.cast<bool>() ? "true" : "false";
}

std::optional<entt::meta_any> ParseBool(std::string_view text)
{
	if (text == "true" || text == "1" || text == "on")
		return entt::meta_any{true};
	if (text == "false" || text == "0" || text == "off")
		return entt::meta_any{false};
	return std::nullopt;
}

std::string FormatString(const entt::meta_any &value)
{
	return value.cast<const std::string &>();
}

std::optional<entt::meta_any> ParseString(std::string_view text)
{
	return entt::meta_any{std::string{text}};
}

template <typename T> std::pair<entt::id_type, Codec> Number(std::string_view what)
{
	return {entt::type_id<T>().hash(), Codec{&FormatNumber<T>, &ParseNumber<T>, what}};
}

const std::unordered_map<entt::id_type, Codec> &Codecs()
{
	// Looked up, never iterated.
	static const std::unordered_map<entt::id_type, Codec> codecs{
	    {entt::type_id<bool>().hash(), Codec{&FormatBool, &ParseBool, "true or false"}},
	    {entt::type_id<std::string>().hash(), Codec{&FormatString, &ParseString, "a string"}},
	    Number<signed char>("a whole number"),
	    Number<short>("a whole number"),
	    Number<int>("a whole number"),
	    Number<long>("a whole number"),
	    Number<long long>("a whole number"),
	    Number<unsigned char>("a whole number from 0"),
	    Number<unsigned short>("a whole number from 0"),
	    Number<unsigned int>("a whole number from 0"),
	    Number<unsigned long>("a whole number from 0"),
	    Number<unsigned long long>("a whole number from 0"),
	    Number<float>("a number"),
	    Number<double>("a number"),
	};
	return codecs;
}

const Codec *CodecFor(const entt::meta_type &type)
{
	const auto found = Codecs().find(type.info().hash());
	return found == Codecs().end() ? nullptr : &found->second;
}

bool IsRegisteredEnum(const entt::meta_type &type)
{
	return type.is_enum() && entt::resolve(type.info());
}

std::string FormatVariable(const entt::meta_any &value)
{
	if (const Codec *codec = CodecFor(value.type()))
		return codec->format(value);
	const std::string_view name = Reflect::EnumName(value);
	return name.empty() ? std::string{"(unnamed)"} : std::string{name};
}

bool IsNameCharacter(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
	       (character >= '0' && character <= '9') || character == '_' || character == '.';
}
} // namespace

std::optional<std::vector<std::string>> SplitLine(std::string_view line)
{
	std::vector<std::string> words;
	std::size_t at = 0;
	while (at < line.size())
	{
		if (line[at] == ' ' || line[at] == '\t')
		{
			++at;
			continue;
		}
		std::string word;
		if (line[at] == '"')
		{
			++at;
			bool closed = false;
			while (at < line.size())
			{
				const char character = line[at++];
				if (character == '"')
				{
					closed = true;
					break;
				}
				if (character == '\\' && at < line.size() && (line[at] == '"' || line[at] == '\\'))
					word += line[at++];
				else
					word += character;
			}
			if (!closed)
				return std::nullopt;
		}
		else
		{
			while (at < line.size() && line[at] != ' ' && line[at] != '\t')
				word += line[at++];
		}
		words.push_back(std::move(word));
	}
	return words;
}

Console::Console(std::size_t capacity) : m_capacity{std::max<std::size_t>(capacity, 1)}
{
	RegisterCommand(
	    "help", "Lists the commands, or describes one: help [command]",
	    [](Console &console, std::span<const std::string> arguments)
	    {
		    if (!arguments.empty())
		    {
			    const auto found = console.m_commands.find(arguments[0]);
			    if (found == console.m_commands.end())
				    throw std::runtime_error{std::format("there is no command '{}'", arguments[0])};
			    console.Print(std::format("{}: {}", found->first, found->second.help));
			    return;
		    }
		    for (const auto &[name, command] : console.m_commands)
			    console.Print(std::format("{}: {}", name, command.help));
	    });
	RegisterCommand(
	    "clear", "Clears the console",
	    [](Console &console, std::span<const std::string>) { console.Clear(); });
	RegisterCommand(
	    "vars", "Lists the console variables and their values",
	    [](Console &console, std::span<const std::string>)
	    {
		    for (const auto &[name, variable] : console.m_variables)
			    console.Print(std::format("{} = {}", name, *console.Value(name)));
	    });
}

Console::~Console()
{
	if (m_logCallback)
		Core::RemoveLogCallback(*m_logCallback);
}

void Console::CaptureLog()
{
	if (m_logCallback)
		return;
	// Called with the logging lock held, from whichever thread logged: it only copies the message
	// in, under the console's own lock.
	m_logCallback = Core::AddLogCallback(
	    [this](const Core::LogRecord &record)
	    {
		    Add(
		        {ConsoleLine::Kind::Log, record.severity, std::string{record.category},
		         std::string{record.message}});
	    });
}

void Console::CheckName(std::string_view name) const
{
	if (name.empty() || !std::ranges::all_of(name, IsNameCharacter))
		throw std::logic_error{std::format("Console: '{}' is not a valid name", name)};
	if (m_commands.contains(name) || m_variables.contains(name))
		throw std::logic_error{std::format("Console: '{}' is already taken", name)};
}

void Console::RegisterCommand(std::string name, std::string help, Command run)
{
	CheckName(name);
	if (!run)
		throw std::logic_error{std::format("Console: command '{}' has nothing to run", name)};
	m_commands.emplace(std::move(name), Registered{std::move(help), std::move(run)});
}

void Console::BindReference(std::string_view prefix, entt::meta_any object)
{
	const entt::meta_type type = object.type();
	if (!entt::resolve(type.info()))
		throw std::logic_error{
		    std::format("Console: '{}' is not registered with Reflect", type.info().name())};

	// Checked in full before any is added, so a bad field leaves no half-bound object.
	std::vector<std::pair<std::string, entt::meta_data>> fields;
	for (const entt::meta_data &field : Reflect::Fields(type, Reflect::Attribute::Console))
	{
		std::string name = std::format("{}.{}", prefix, field.name());
		CheckName(name);
		if (!CodecFor(field.type()) && !IsRegisteredEnum(field.type()))
			throw std::logic_error{std::format(
			    "Console: variable '{}' is a '{}', which the console cannot set", name,
			    field.type().info().name())};
		fields.emplace_back(std::move(name), field);
	}
	for (auto &[name, field] : fields)
		m_variables.emplace(std::move(name), Variable{object.as_ref(), field});
}

void Console::Add(ConsoleLine line)
{
	const std::scoped_lock lock{m_linesMutex};
	m_lines.push_back(std::move(line));
	while (m_lines.size() > m_capacity)
		m_lines.pop_front();
}

void Console::Print(std::string text)
{
	Add({ConsoleLine::Kind::Output, Core::Severity::Info, {}, std::move(text)});
}

void Console::PrintError(std::string text)
{
	Add({ConsoleLine::Kind::Error, Core::Severity::Error, {}, std::move(text)});
}

void Console::Clear()
{
	const std::scoped_lock lock{m_linesMutex};
	m_lines.clear();
}

std::vector<ConsoleLine> Console::Lines() const
{
	const std::scoped_lock lock{m_linesMutex};
	return {m_lines.begin(), m_lines.end()};
}

void Console::Execute(std::string_view line)
{
	Add({ConsoleLine::Kind::Input, Core::Severity::Info, {}, std::format("> {}", line)});
	if (line.find_first_not_of(" \t") == std::string_view::npos)
		return;
	if (m_history.empty() || m_history.back() != line)
		m_history.emplace_back(line);

	const std::optional<std::vector<std::string>> words = SplitLine(line);
	if (!words)
	{
		PrintError("a quote is not closed");
		return;
	}
	const std::string &name = words->front();
	const std::span<const std::string> arguments = std::span{*words}.subspan(1);
	if (const auto command = m_commands.find(name); command != m_commands.end())
	{
		try
		{
			// A copy: the command may register another, or clear, and the map may move under it.
			const Command run = command->second.run;
			run(*this, arguments);
		}
		catch (const std::exception &error)
		{
			PrintError(std::format("{}: {}", name, error.what()));
		}
		return;
	}
	if (const auto variable = m_variables.find(name); variable != m_variables.end())
	{
		RunVariable(name, variable->second, arguments);
		return;
	}
	PrintError(std::format("there is no command or variable '{}'", name));
}

void Console::RunVariable(
    const std::string &name, Variable &variable, std::span<const std::string> arguments)
{
	if (arguments.size() > 1)
	{
		PrintError(std::format("{} takes one value; quote it if it has spaces", name));
		return;
	}
	if (arguments.size() == 1)
	{
		const entt::meta_type type = variable.field.type();
		std::optional<entt::meta_any> parsed;
		std::string what;
		if (const Codec *codec = CodecFor(type))
		{
			parsed = codec->parse(arguments[0]);
			what = codec->what;
		}
		else
		{
			if (entt::meta_any enumerator = Reflect::EnumValue(type, arguments[0]))
				parsed = std::move(enumerator);
			for (const auto &[id, enumerator] : type.data())
				what += std::format("{}{}", what.empty() ? "one of " : ", ", enumerator.name());
		}
		if (!parsed)
		{
			PrintError(std::format("{} is {}, and '{}' is not", name, what, arguments[0]));
			return;
		}
		if (!variable.field.set(variable.object, std::move(*parsed)))
		{
			PrintError(std::format("{} could not be set", name));
			return;
		}
	}
	Print(std::format("{} = {}", name, *Value(name)));
}

std::optional<std::string> Console::Value(std::string_view variable) const
{
	const auto found = m_variables.find(variable);
	if (found == m_variables.end())
		return std::nullopt;
	return FormatVariable(found->second.field.get(found->second.object));
}

std::vector<std::string> Console::Complete(std::string_view prefix) const
{
	std::vector<std::string> names;
	for (const auto &[name, command] : m_commands)
	{
		if (name.starts_with(prefix))
			names.push_back(name);
	}
	for (const auto &[name, variable] : m_variables)
	{
		if (name.starts_with(prefix))
			names.push_back(name);
	}
	std::ranges::sort(names);
	return names;
}

std::string Console::CompleteLine(std::string_view line) const
{
	const std::size_t start = line.find_first_not_of(' ');
	if (start == std::string_view::npos || line.find(' ', start) != std::string_view::npos)
		return std::string{line};
	const std::vector<std::string> candidates = Complete(line.substr(start));
	if (candidates.empty())
		return std::string{line};
	std::string common = candidates.front();
	for (const std::string &candidate : candidates)
	{
		const auto [mine, theirs] = std::ranges::mismatch(common, candidate);
		common.erase(mine, common.end());
	}
	return std::format("{}{}{}", line.substr(0, start), common, candidates.size() == 1 ? " " : "");
}
} // namespace Jettison::DevTools
