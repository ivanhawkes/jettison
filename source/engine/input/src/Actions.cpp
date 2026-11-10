#include <jettison/input/Actions.h>

#include <jettison/core/Config.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace Jettison::Input
{
namespace
{
const std::vector<Binding> kNoBindings;

void CheckName(std::string_view name, std::string_view what)
{
	const bool valid = !name.empty() && std::ranges::all_of(
	                                        name,
	                                        [](char character)
	                                        {
		                                        return (character >= 'a' && character <= 'z') ||
		                                               (character >= 'A' && character <= 'Z') ||
		                                               (character >= '0' && character <= '9') ||
		                                               character == '_';
	                                        });
	if (!valid)
		throw std::logic_error{std::format("Input: '{}' is not a valid {} name", name, what)};
}

std::string Key(std::string_view context, std::string_view action)
{
	return std::format("input.{}.{}", context, action);
}
} // namespace

ActionId InputSystem::DefineAction(std::string name, ActionType type)
{
	CheckName(name, "action");
	if (std::ranges::find(m_actions, name, &ActionState::name) != m_actions.end())
		throw std::logic_error{std::format("Input: action '{}' is already defined", name)};
	m_actions.push_back({std::move(name), type});
	return {static_cast<std::uint32_t>(m_actions.size() - 1)};
}

ActionId InputSystem::Action(std::string_view name) const
{
	const auto found = std::ranges::find(m_actions, name, &ActionState::name);
	if (found == m_actions.end())
		throw std::logic_error{std::format("Input: there is no action '{}'", name)};
	return {static_cast<std::uint32_t>(found - m_actions.begin())};
}

void InputSystem::DefineContext(std::string name, bool opaque)
{
	CheckName(name, "context");
	if (std::ranges::find(m_contexts, name, &Context::name) != m_contexts.end())
		throw std::logic_error{std::format("Input: context '{}' is already defined", name)};
	m_contexts.push_back({std::move(name), opaque, {}, {}});
}

InputSystem::Context &InputSystem::ContextNamed(std::string_view name)
{
	return const_cast<Context &>(std::as_const(*this).ContextNamed(name));
}

const InputSystem::Context &InputSystem::ContextNamed(std::string_view name) const
{
	const auto found = std::ranges::find(m_contexts, name, &Context::name);
	if (found == m_contexts.end())
		throw std::logic_error{std::format("Input: there is no context '{}'", name)};
	return *found;
}

const InputSystem::ActionState &InputSystem::State(ActionId action) const
{
	if (action.index >= m_actions.size())
		throw std::logic_error{std::format("Input: action {} is not defined", action.index)};
	return m_actions[action.index];
}

void InputSystem::Check(ActionId action, const Binding &binding) const
{
	const ActionState &state = State(action);
	const std::uint8_t limit = state.type == ActionType::Vector ? 1 : 0;
	if (binding.component > limit)
		throw std::logic_error{
		    std::format("Input: action '{}' has no component {}", state.name, binding.component)};
}

void InputSystem::Bind(std::string_view context, ActionId action, Binding binding)
{
	Check(action, binding);
	ContextNamed(context).defaults[action.index].push_back(binding);
}

void InputSystem::Rebind(std::string_view context, ActionId action, std::vector<Binding> bindings)
{
	for (const Binding &binding : bindings)
		Check(action, binding);
	ContextNamed(context).overrides[action.index] = std::move(bindings);
}

void InputSystem::ResetBindings(std::string_view context, ActionId action)
{
	(void)State(action);
	ContextNamed(context).overrides.erase(action.index);
}

const std::vector<Binding> &InputSystem::Bindings(std::string_view context, ActionId action) const
{
	(void)State(action);
	const Context &found = ContextNamed(context);
	if (const auto overridden = found.overrides.find(action.index); overridden != found.overrides.end())
		return overridden->second;
	const auto bound = found.defaults.find(action.index);
	return bound == found.defaults.end() ? kNoBindings : bound->second;
}

void InputSystem::PushContext(std::string_view name)
{
	const Context &context = ContextNamed(name);
	m_stack.push_back(static_cast<std::size_t>(&context - m_contexts.data()));
}

void InputSystem::PopContext()
{
	if (m_stack.empty())
		throw std::logic_error{"Input: there is no context to pop"};
	m_stack.pop_back();
}

void InputSystem::SetButton(InputCode input, bool down)
{
	Raw &raw = m_raw[input];
	raw.value = down ? 1.0f : 0.0f;
	raw.latched = raw.latched || down;
}

void InputSystem::SetAxis(InputCode input, float value)
{
	m_raw[input].value = value;
}

void InputSystem::AddAxisDelta(InputCode input, float delta)
{
	m_raw[input].delta += delta;
}

std::vector<InputCode> InputSystem::BoundInputs() const
{
	std::vector<InputCode> inputs;
	const auto add = [&inputs](const std::map<std::uint32_t, std::vector<Binding>> &bindings)
	{
		for (const auto &[action, list] : bindings)
		{
			for (const Binding &binding : list)
			{
				inputs.push_back(binding.input);
				if (binding.negative)
					inputs.push_back(*binding.negative);
			}
		}
	};
	for (const Context &context : m_contexts)
	{
		add(context.defaults);
		add(context.overrides);
	}
	std::ranges::sort(inputs);
	const auto duplicates = std::ranges::unique(inputs);
	inputs.erase(duplicates.begin(), duplicates.end());
	return inputs;
}

float InputSystem::Read(InputCode input, float deadZone) const
{
	const auto found = m_raw.find(input);
	if (found == m_raw.end())
		return 0.0f;
	const Raw &raw = found->second;
	// Latched only by a button pressed since the last poll, which reads as held for this one.
	const float value = (raw.latched ? 1.0f : raw.value) + raw.delta;
	return std::abs(value) < deadZone ? 0.0f : value;
}

void InputSystem::Poll()
{
	for (ActionState &action : m_actions)
	{
		action.wasPressed = action.pressed;
		action.value = glm::vec2{0.0f};
	}

	// Top of the stack first. A context's inputs are claimed once the whole context is read, so
	// one context can bind an input to two actions.
	std::vector<InputCode> claimed;
	const auto isClaimed = [&claimed](InputCode input)
	{ return std::ranges::binary_search(claimed, input); };
	for (const std::size_t position : std::ranges::reverse_view(m_stack))
	{
		const Context &context = m_contexts[position];
		std::vector<InputCode> claims;
		// The actions this context binds, overridden or not, each once and in index order.
		std::vector<std::uint32_t> actions;
		actions.reserve(context.defaults.size() + context.overrides.size());
		for (const auto &[index, bindings] : context.defaults)
			actions.push_back(index);
		for (const auto &[index, bindings] : context.overrides)
			actions.push_back(index);
		std::ranges::sort(actions);
		const auto repeated = std::ranges::unique(actions);
		actions.erase(repeated.begin(), repeated.end());

		for (const std::uint32_t index : actions)
		{
			ActionState &action = m_actions[index];
			for (const Binding &binding : Bindings(context.name, ActionId{index}))
			{
				claims.push_back(binding.input);
				if (binding.negative)
					claims.push_back(*binding.negative);
				if (isClaimed(binding.input) || (binding.negative && isClaimed(*binding.negative)))
					continue;
				float reading = Read(binding.input, binding.deadZone);
				if (binding.negative)
					reading -= Read(*binding.negative, binding.deadZone);
				action.value[binding.component] += reading * binding.scale;
			}
		}
		claimed.insert(claimed.end(), claims.begin(), claims.end());
		std::ranges::sort(claimed);
		if (context.opaque)
			break;
	}

	for (ActionState &action : m_actions)
	{
		if (action.type == ActionType::Axis)
			action.value.x = std::clamp(action.value.x, -1.0f, 1.0f);
		action.pressed = action.type == ActionType::Button ? std::abs(action.value.x) >= 0.5f
		                                                   : glm::length(action.value) >= 0.5f;
	}
	for (auto &[input, raw] : m_raw)
	{
		raw.delta = 0.0f;
		raw.latched = false;
	}
}

bool InputSystem::IsPressed(ActionId action) const
{
	return State(action).pressed;
}

bool InputSystem::WasPressedThisFrame(ActionId action) const
{
	const ActionState &state = State(action);
	return state.pressed && !state.wasPressed;
}

bool InputSystem::WasReleasedThisFrame(ActionId action) const
{
	const ActionState &state = State(action);
	return !state.pressed && state.wasPressed;
}

float InputSystem::Axis(ActionId action) const
{
	return State(action).value.x;
}

glm::vec2 InputSystem::Vector(ActionId action) const
{
	return State(action).value;
}

void InputSystem::SaveBindings(Core::Config &config) const
{
	for (const Context &context : m_contexts)
	{
		for (const auto &[index, bindings] : context.overrides)
			config.Set(
			    Core::ConfigLayer::User, Key(context.name, m_actions[index].name),
			    FormatBindings(bindings));
	}
}

void InputSystem::LoadBindings(const Core::Config &config)
{
	// Parsed in full before any is applied.
	std::vector<std::pair<std::size_t, std::pair<std::uint32_t, std::vector<Binding>>>> loaded;
	for (std::size_t context = 0; context < m_contexts.size(); ++context)
	{
		for (std::uint32_t action = 0; action < m_actions.size(); ++action)
		{
			const std::string key = Key(m_contexts[context].name, m_actions[action].name);
			const std::optional<std::string> text = config.GetString(key);
			if (!text)
				continue;
			std::optional<std::vector<Binding>> bindings = ParseBindings(*text);
			if (!bindings)
				throw std::runtime_error{
				    std::format("Input: {} is '{}', which is not bindings", key, *text)};
			// Data, not code: a binding that does not suit the action is the file's error.
			try
			{
				for (const Binding &binding : *bindings)
					Check({action}, binding);
			}
			catch (const std::logic_error &error)
			{
				throw std::runtime_error{std::format("{}: {}", key, error.what())};
			}
			loaded.push_back({context, {action, std::move(*bindings)}});
		}
	}
	for (auto &[context, entry] : loaded)
		m_contexts[context].overrides[entry.first] = std::move(entry.second);
}
} // namespace Jettison::Input
