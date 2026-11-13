#include <jettison/ui/DataModel.h>

#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Variant.h>

#include <cmath>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <jettison/reflect/Reflect.h>

#include "UiState.h"

namespace Jettison::Ui
{
std::int64_t AsInteger(const EventArgument &argument)
{
	if (const auto *integer = std::get_if<std::int64_t>(&argument))
		return *integer;
	if (const auto *number = std::get_if<double>(&argument))
		return std::trunc(*number) == *number ? static_cast<std::int64_t>(*number) : 0;
	if (const auto *flag = std::get_if<bool>(&argument))
		return *flag ? 1 : 0;
	return 0;
}

std::string AsText(const EventArgument &argument)
{
	return std::visit(
	    [](const auto &value) -> std::string
	    {
		    using Value = std::decay_t<decltype(value)>;
		    if constexpr (std::is_same_v<Value, std::string>)
			    return value;
		    else if constexpr (std::is_same_v<Value, bool>)
			    return value ? "true" : "false";
		    else
			    return std::format("{}", value);
	    },
	    argument);
}

namespace Detail
{
namespace
{
template <typename T> T Load(const void *pValue)
{
	T value{};
	std::memcpy(&value, pValue, sizeof(T));
	return value;
}

template <typename T> void Store(void *pValue, T value)
{
	std::memcpy(pValue, &value, sizeof(T));
}

std::int64_t LoadSigned(const void *pValue, std::size_t width)
{
	switch (width)
	{
		case 1: return Load<std::int8_t>(pValue);
		case 2: return Load<std::int16_t>(pValue);
		case 4: return Load<std::int32_t>(pValue);
		default: return Load<std::int64_t>(pValue);
	}
}

std::uint64_t LoadUnsigned(const void *pValue, std::size_t width)
{
	switch (width)
	{
		case 1: return Load<std::uint8_t>(pValue);
		case 2: return Load<std::uint16_t>(pValue);
		case 4: return Load<std::uint32_t>(pValue);
		default: return Load<std::uint64_t>(pValue);
	}
}

void StoreInteger(void *pValue, std::size_t width, std::int64_t value)
{
	switch (width)
	{
		case 1: Store(pValue, static_cast<std::int8_t>(value)); break;
		case 2: Store(pValue, static_cast<std::int16_t>(value)); break;
		case 4: Store(pValue, static_cast<std::int32_t>(value)); break;
		default: Store(pValue, value); break;
	}
}

Kind KindOf(const entt::meta_type &type, const std::string &path)
{
	if (type == entt::resolve<bool>())
		return Kind::Bool;
	if (type == entt::resolve<std::string>())
		return Kind::String;
	if (type == entt::resolve<float>())
		return Kind::Float;
	if (type == entt::resolve<double>())
		return Kind::Double;
	if (type.is_enum() && Reflect::Resolve(type.name()))
		return Kind::Enum;
	if (type.is_integral())
		return type.is_signed() ? Kind::Signed : Kind::Unsigned;
	if (type.is_sequence_container())
		return Kind::Sequence;
	if (type.is_class() && Reflect::Resolve(type.name()))
		return Kind::Class;
	throw std::logic_error(
	    std::format(
	        "DataModel: '{}' is of a type the UI cannot show (one Reflect has not registered)", path));
}

EventArgument FromVariant(const Rml::Variant &variant)
{
	switch (variant.GetType())
	{
		case Rml::Variant::BOOL: return variant.Get<bool>();
		case Rml::Variant::BYTE:
		case Rml::Variant::CHAR:
		case Rml::Variant::INT:
		case Rml::Variant::INT64:
		case Rml::Variant::UINT:
		case Rml::Variant::UINT64: return variant.Get<std::int64_t>();
		case Rml::Variant::FLOAT:
		case Rml::Variant::DOUBLE: return variant.Get<double>();
		default: return variant.Get<std::string>();
	}
}
} // namespace

class ReflectedDefinition final : public Rml::VariableDefinition
{
  public:
	ReflectedDefinition(Kind kind, const entt::meta_type &type, Definitions &definitions)
	    : Rml::VariableDefinition{
	          kind == Kind::Class      ? Rml::DataVariableType::Struct
	          : kind == Kind::Sequence ? Rml::DataVariableType::Array
	                                   : Rml::DataVariableType::Scalar},
	      m_kind{kind}, m_type{type}, m_definitions{definitions}
	{
	}

	// Called once the definition is in the table, so a type that holds itself (a tree of nodes)
	// finds its own definition rather than recursing.
	void Resolve(const std::string &path)
	{
		if (m_kind == Kind::Class)
			for (const entt::meta_data &field : Reflect::Fields(m_type))
				m_fields.emplace_back(
				    field, m_definitions.For(field.type(), path + "." + std::string{field.name()}));
		else if (m_kind == Kind::Sequence)
		{
			const entt::meta_type element = m_type.template_arg(0);
			if (!element)
				throw std::logic_error(
				    std::format("DataModel: '{}' is a sequence of an unknown type", path));
			m_element = m_definitions.For(element, path + "[]");
		}
	}

	bool Get(void *pValue, Rml::Variant &variant) override
	{
		switch (m_kind)
		{
			case Kind::Bool: variant = *static_cast<const bool *>(pValue); return true;
			case Kind::Signed: variant = LoadSigned(pValue, m_type.size_of()); return true;
			case Kind::Unsigned: variant = LoadUnsigned(pValue, m_type.size_of()); return true;
			case Kind::Float: variant = *static_cast<const float *>(pValue); return true;
			case Kind::Double: variant = *static_cast<const double *>(pValue); return true;
			case Kind::String: variant = *static_cast<const std::string *>(pValue); return true;
			case Kind::Enum:
				variant = Rml::String{Reflect::EnumName(m_type.from_void(pValue))};
				return true;
			default: return false;
		}
	}

	bool Set(void *pValue, const Rml::Variant &variant) override
	{
		switch (m_kind)
		{
			case Kind::Bool: *static_cast<bool *>(pValue) = variant.Get<bool>(); return true;
			case Kind::Signed:
			case Kind::Unsigned:
				StoreInteger(pValue, m_type.size_of(), variant.Get<std::int64_t>());
				return true;
			case Kind::Float: *static_cast<float *>(pValue) = variant.Get<float>(); return true;
			case Kind::Double: *static_cast<double *>(pValue) = variant.Get<double>(); return true;
			case Kind::String:
				*static_cast<std::string *>(pValue) = variant.Get<std::string>();
				return true;
			case Kind::Enum:
			{
				entt::meta_any target = m_type.from_void(pValue);
				const entt::meta_any value = Reflect::EnumValue(m_type, variant.Get<std::string>());
				return value && target.assign(value);
			}
			default: return false;
		}
	}

	int Size(void *pValue) override
	{
		if (m_kind != Kind::Sequence)
			return 0;
		return static_cast<int>(m_type.from_void(pValue).as_sequence_container().size());
	}

	Rml::DataVariable Child(void *pValue, const Rml::DataAddressEntry &address) override
	{
		if (m_kind == Kind::Class)
		{
			for (const auto &[field, pDefinition] : m_fields)
				if (address.name == field.name())
					return Rml::DataVariable{pDefinition, Reflect::AddressOf(field, pValue)};
			Rml::Log::Message(
			    Rml::Log::LT_WARNING, "'%s' has no field '%s'", std::string{m_type.name()}.c_str(),
			    address.name.c_str());
			return {};
		}
		if (m_kind == Kind::Sequence)
		{
			entt::meta_sequence_container container = m_type.from_void(pValue).as_sequence_container();
			// A document asks an array its length by name, as RmlUi's own arrays answer it.
			if (address.index < 0 && address.name == "size")
				return Rml::MakeLiteralIntVariable(static_cast<int>(container.size()));
			if (address.index < 0 || static_cast<std::size_t>(address.index) >= container.size())
			{
				Rml::Log::Message(
				    Rml::Log::LT_WARNING, "Index %d is past the %d elements", address.index,
				    static_cast<int>(container.size()));
				return {};
			}
			const entt::meta_any element = container[static_cast<std::size_t>(address.index)];
			// The element is a reference into the bound value, which the model may write through.
			return Rml::DataVariable{m_element, const_cast<void *>(element.base().data())};
		}
		return {};
	}

	Rml::StringList ReflectMemberNames() override
	{
		Rml::StringList names;
		for (const auto &[field, pDefinition] : m_fields)
			names.emplace_back(field.name());
		return names;
	}

  private:
	Kind m_kind;
	entt::meta_type m_type;
	Definitions &m_definitions;
	std::vector<std::pair<entt::meta_data, Rml::VariableDefinition *>> m_fields;
	Rml::VariableDefinition *m_element{nullptr};
};

Rml::VariableDefinition *Definitions::For(const entt::meta_type &type, const std::string &path)
{
	if (const auto found = m_definitions.find(type.info().hash()); found != m_definitions.end())
		return found->second.get();
	auto pDefinition = std::make_unique<ReflectedDefinition>(KindOf(type, path), type, *this);
	ReflectedDefinition *pResolving = pDefinition.get();
	m_definitions.emplace(type.info().hash(), std::move(pDefinition));
	try
	{
		pResolving->Resolve(path);
	}
	catch (...)
	{
		m_definitions.erase(type.info().hash());
		throw;
	}
	return pResolving;
}
} // namespace Detail


DataModel::DataModel(std::unique_ptr<Detail::DataModelState> pState) : m_pState{std::move(pState)}
{
}

void Detail::RegisterTransforms(Rml::DataModelConstructor &constructor, const StringTable &strings)
{
	// {{trader.credits | thousands}}: 1,250,000, or as the language groups a number's thousands.
	constructor.RegisterTransformFunc(
	    "thousands",
	    [&strings](const Rml::VariantList &arguments)
	    {
		    return arguments.empty() ? Rml::Variant{}
		                             : Rml::Variant{strings.Number(arguments[0].Get<std::int64_t>())};
	    });
}

DataModel::~DataModel() = default;

const std::string &DataModel::Name() const
{
	return m_pState->name;
}

void DataModel::BindValue(std::string_view name, const entt::meta_type &type, void *pValue)
{
	const std::string key{name};
	if (m_pState->bound.contains(key))
		throw std::logic_error(
		    std::format("DataModel '{}': '{}' is bound already", m_pState->name, name));
	Rml::VariableDefinition *pDefinition = m_pState->definitions.For(type, key);
	if (!m_pState->constructor.BindCustomDataVariable(key, Rml::DataVariable{pDefinition, pValue}))
		throw std::logic_error(
		    std::format("DataModel '{}': RmlUi refused the variable '{}'", m_pState->name, name));
	m_pState->bound.insert(key);
}

void DataModel::BindEvent(
    std::string_view name, std::function<void(std::span<const EventArgument>)> handler)
{
	const std::string key{name};
	if (m_pState->bound.contains(key))
		throw std::logic_error(
		    std::format("DataModel '{}': '{}' is bound already", m_pState->name, name));
	m_pState->constructor.BindEventCallback(
	    key,
	    [handler =
	         std::move(handler)](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &variants)
	    {
		    std::vector<EventArgument> arguments;
		    arguments.reserve(variants.size());
		    for (const Rml::Variant &variant : variants)
			    arguments.push_back(Detail::FromVariant(variant));
		    handler(arguments);
	    });
	m_pState->bound.insert(key);
}

void DataModel::Changed(std::string_view name)
{
	m_pState->handle.DirtyVariable(std::string{name});
}

void DataModel::ChangedAll()
{
	m_pState->handle.DirtyAllVariables();
}
} // namespace Jettison::Ui
