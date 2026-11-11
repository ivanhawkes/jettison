#include "Settings.h"

#include <format>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <jettison/core/Log.h>
#include <jettison/input/Actions.h>

namespace Jettison::App
{
namespace
{
constexpr Core::Category kSettings{"settings"};

// The context and the action a command names, as the input system has them.
Input::ActionId Named(
    const Input::InputSystem &input, std::span<const std::string> arguments, std::string_view usage)
{
	if (arguments.size() < 2)
		throw std::runtime_error{std::format("usage: {}", usage)};
	try
	{
		const Input::ActionId action = input.Action(arguments[1]);
		// Throws for a context that is not defined.
		(void)input.Bindings(arguments[0], action);
		return action;
	}
	catch (const std::logic_error &error)
	{
		throw std::runtime_error{error.what()};
	}
}
} // namespace

Settings::Settings(std::filesystem::path file) : m_file{std::move(file)}
{
}

void Settings::Load(Input::InputSystem &input)
{
	try
	{
		m_config.LoadLayer(Core::ConfigLayer::User, m_file);
		// Both parsed before either applies: the joysticks into a map of their own first.
		Input::JoystickMap joysticks;
		joysticks.Load(m_config);
		input.LoadBindings(m_config);
		m_joysticks = std::move(joysticks);
	}
	catch (const std::runtime_error &error)
	{
		Core::Log(
		    Core::Severity::Warning, kSettings,
		    "The settings were not applied, and the defaults stand: {}", error.what());
		m_unread = true;
		return;
	}
	Core::Log(Core::Severity::Info, kSettings, "Settings from {}", m_file.string());
}

void Settings::Save(const Input::InputSystem &input)
{
	if (m_unread)
	{
		std::filesystem::path aside = m_file;
		aside += ".unread";
		std::error_code error;
		if (std::filesystem::exists(m_file, error))
		{
			std::filesystem::copy_file(
			    m_file, aside, std::filesystem::copy_options::overwrite_existing, error);
			if (error)
				throw std::runtime_error{std::format(
				    "{} did not read, and could not be kept aside as {}: {}", m_file.string(),
				    aside.string(), error.message())};
			Core::Log(
			    Core::Severity::Warning, kSettings, "The settings that did not read are kept as {}",
			    aside.string());
		}
		m_unread = false;
	}
	input.SaveBindings(m_config);
	m_joysticks.Save(m_config);
	m_config.SaveUserLayer(m_file);
}

BindResult Bind(Input::InputSystem &input, std::span<const std::string> arguments)
{
	constexpr std::string_view kUsage = "bind <context> <action> [bindings...]";
	const Input::ActionId action = Named(input, arguments, kUsage);
	const std::string &context = arguments[0];
	if (arguments.size() == 2)
		return {
		    std::format(
		        "{}.{} is {}", context, arguments[1],
		        Input::FormatBindings(input.Bindings(context, action))),
		    false};

	std::string text;
	for (const std::string &argument : arguments.subspan(2))
		text += (text.empty() ? "" : ",") + argument;
	const std::optional<std::vector<Input::Binding>> bindings = Input::ParseBindings(text);
	if (!bindings)
		throw std::runtime_error{std::format("'{}' is not bindings", text)};
	try
	{
		input.Rebind(context, action, *bindings);
	}
	catch (const std::logic_error &error)
	{
		throw std::runtime_error{error.what()};
	}
	return {std::format("{}.{} is {}", context, arguments[1], Input::FormatBindings(*bindings)), true};
}

BindResult Unbind(Input::InputSystem &input, std::span<const std::string> arguments)
{
	const Input::ActionId action = Named(input, arguments, "unbind <context> <action>");
	if (arguments.size() != 2)
		throw std::runtime_error{"usage: unbind <context> <action>"};
	input.ResetBindings(arguments[0], action);
	return {
	    std::format(
	        "{}.{} is back to {}", arguments[0], arguments[1],
	        Input::FormatBindings(input.Bindings(arguments[0], action))),
	    true};
}
} // namespace Jettison::App
