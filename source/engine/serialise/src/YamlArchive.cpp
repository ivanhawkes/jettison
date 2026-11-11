#include "Shape.h"

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace Jettison::Serialise
{
namespace
{
using Detail::Kind;
using Detail::Path;
using Detail::Shape;
using Reflect::Attribute;

constexpr std::string_view kVersionKey = "version";

// ---- Scalars as text ----------------------------------------------------------------------
//
// Numbers are formatted and parsed here rather than by yaml-cpp, whose stream conversions write
// a fixed precision and accept what a stream accepts. std::to_chars writes the shortest text
// that reads back to the same value, and std::from_chars must consume the whole scalar.

std::string FormatInteger(auto value)
{
	std::array<char, 24> buffer{};
	const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
	return {buffer.data(), result.ptr};
}

template <typename T> std::string FormatFloat(T value)
{
	// YAML's spellings, which from_chars would not produce or accept.
	if (std::isnan(value))
		return ".nan";
	if (std::isinf(value))
		return value < 0 ? "-.inf" : ".inf";
	std::array<char, 32> buffer{};
	const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
	return {buffer.data(), result.ptr};
}

template <typename T> bool ParseNumber(std::string_view text, T &value)
{
	if (text.starts_with('+'))
		text.remove_prefix(1);
	const char *const first = std::to_address(text.begin());
	const char *const last = std::to_address(text.end());
	const auto [stop, error] = std::from_chars(first, last, value);
	return error == std::errc{} && stop == last;
}

template <typename T> bool ParseFloat(std::string_view text, T &value)
{
	if (text == ".nan" || text == ".NaN" || text == ".NAN")
	{
		value = std::numeric_limits<T>::quiet_NaN();
		return true;
	}
	const bool negative = text.starts_with('-');
	if (negative || text.starts_with('+'))
		text.remove_prefix(1);
	if (text == ".inf" || text == ".Inf" || text == ".INF")
	{
		value = negative ? -std::numeric_limits<T>::infinity() : std::numeric_limits<T>::infinity();
		return true;
	}
	// from_chars also reads "inf" and "nan", which YAML calls strings.
	const bool startsANumber =
	    !text.empty() &&
	    (std::isdigit(static_cast<unsigned char>(text.front())) != 0 || text.front() == '.');
	if (!startsANumber)
		return false;
	const char *const first = std::to_address(text.begin());
	const char *const last = std::to_address(text.end());
	const auto [stop, error] = std::from_chars(first, last, value);
	if (error != std::errc{} || stop != last)
		return false;
	if (negative)
		value = -value;
	return true;
}

// Whether a string written bare would read as something else to a YAML reader that is not
// type-directed, as this one is: a number, a bool (1.2's spellings and 1.1's, which yaml-cpp's
// own conversion still accepts) or one of YAML's non-finite floats. yaml-cpp quotes the null
// spellings itself.
bool LooksLikeAnotherType(std::string_view text)
{
	static constexpr std::array<std::string_view, 24> kWords{
	    "true", "True", "TRUE", "false", "False", "FALSE", "yes", "Yes", "YES", "no", "No", "NO",
	    "on",   "On",   "ON",   "off",   "Off",   "OFF",   "y",   "Y",   "n",   "N",  "~",  "null"};
	if (std::ranges::find(kWords, text) != kWords.end())
		return true;
	double number = 0.0;
	std::int64_t whole = 0;
	std::uint64_t positive = 0;
	return ParseFloat(text, number) || ParseNumber(text, whole) || ParseNumber(text, positive);
}

// ---- Writing ------------------------------------------------------------------------------

class Writer
{
  public:
	explicit Writer(std::string_view root) : m_path{root}
	{
	}

	std::string Finish()
	{
		if (!m_emitter.good())
			throw UnsupportedTypeError{
			    std::format("YamlArchive: {}: {}", m_path.Text(), m_emitter.GetLastError())};
		return std::string{m_emitter.c_str()} + '\n';
	}

	void Root(const entt::meta_any &value, FormatVersion version)
	{
		m_emitter << YAML::BeginMap;
		m_emitter << YAML::Key << std::string{kVersionKey} << YAML::Value << version.version;
		Fields(value, true);
		m_emitter << YAML::EndMap;
	}

  private:
	void Fields(const entt::meta_any &value, bool root)
	{
		for (const entt::meta_data &field : Reflect::Fields(value.type(), Attribute::Saved))
		{
			const auto scope = m_path.Field(field.name());
			if (root && field.name() == kVersionKey)
				throw UnsupportedTypeError{std::format(
				    "YamlArchive: {}: a root type cannot save a field named 'version', which the "
				    "archive's own version uses",
				    m_path.Text())};
			const entt::meta_any member = field.get(value);
			if (Detail::IsEmptyOptional(member))
				continue;
			m_emitter << YAML::Key << std::string{field.name()} << YAML::Value;
			Value(member);
		}
	}

	void Value(const entt::meta_any &value)
	{
		const Shape shape = Detail::ShapeOf(value.type(), m_path.Text());
		switch (shape.kind)
		{
			case Kind::Bool:
			case Kind::Signed:
			case Kind::Unsigned:
			case Kind::Float: Leaf(shape, shape.view(value)); return;
			case Kind::String:
			{
				const auto &text = *static_cast<const std::string *>(shape.view(value));
				if (LooksLikeAnotherType(text))
					m_emitter << YAML::DoubleQuoted;
				m_emitter << text;
				return;
			}
			case Kind::Enum:
			{
				const std::string_view name = Reflect::EnumName(value);
				if (name.empty())
					throw UnsupportedTypeError{std::format(
					    "YamlArchive: {}: the value is not an enumerator registered for '{}'",
					    m_path.Text(), value.type().name())};
				m_emitter << std::string{name};
				return;
			}
			case Kind::Class:
				m_emitter << YAML::BeginMap;
				Fields(value, false);
				m_emitter << YAML::EndMap;
				return;
			case Kind::Sequence: Sequence(value); return;
			case Kind::Optional:
				// Fields leave an empty one out; anywhere else there is no key to leave out.
				if (!shape.optional->hasValue(value))
					throw UnsupportedTypeError{std::format(
					    "YamlArchive: {}: an empty optional can only be a field, which is left out",
					    m_path.Text())};
				Value(shape.optional->value(value));
				return;
		}
	}

	void Leaf(const Shape &shape, const void *components)
	{
		if (shape.count > 1)
			m_emitter << YAML::Flow << YAML::BeginSeq;
		const auto *bytes = static_cast<const std::byte *>(components);
		for (std::uint8_t index = 0; index < shape.count; ++index)
			m_emitter << Component(shape, bytes + std::size_t{index} * shape.width);
		if (shape.count > 1)
			m_emitter << YAML::EndSeq;
	}

	static std::string Component(const Shape &shape, const std::byte *bytes)
	{
		const auto load = [bytes]<typename T>(T value)
		{
			std::memcpy(&value, bytes, sizeof(T));
			return value;
		};
		switch (shape.kind)
		{
			case Kind::Bool: return load(bool{}) ? "true" : "false";
			case Kind::Float:
				return shape.width == 4 ? FormatFloat(load(float{})) : FormatFloat(load(double{}));
			case Kind::Signed:
				switch (shape.width)
				{
					case 1: return FormatInteger(load(std::int8_t{}));
					case 2: return FormatInteger(load(std::int16_t{}));
					case 4: return FormatInteger(load(std::int32_t{}));
					default: return FormatInteger(load(std::int64_t{}));
				}
			default:
				switch (shape.width)
				{
					case 1: return FormatInteger(load(std::uint8_t{}));
					case 2: return FormatInteger(load(std::uint16_t{}));
					case 4: return FormatInteger(load(std::uint32_t{}));
					default: return FormatInteger(load(std::uint64_t{}));
				}
		}
	}

	void Sequence(const entt::meta_any &value)
	{
		// begin() is not const on a container, even one over a const value.
		entt::meta_sequence_container elements = value.as_sequence_container();
		// Elements that are maps or sequences of their own go one to a line; a list of numbers,
		// names or vectors goes on one, as the scene format writes a vector.
		const Kind kind = Detail::ShapeOf(elements.value_type(), m_path.Text()).kind;
		const bool nested = kind == Kind::Class || kind == Kind::Sequence;
		m_emitter << (nested ? YAML::Block : YAML::Flow) << YAML::BeginSeq;
		std::size_t index = 0;
		for (const entt::meta_any element : elements)
		{
			const auto scope = m_path.Element(index++);
			Value(element);
		}
		m_emitter << YAML::EndSeq;
	}

	YAML::Emitter m_emitter;
	Path m_path;
};

// ---- Reading ------------------------------------------------------------------------------

class Reader
{
  public:
	explicit Reader(std::string_view root) : m_path{root}
	{
	}

	void Root(entt::meta_any &value, const YAML::Node &document, FormatVersion expected)
	{
		if (!document.IsMap())
			Fail(document, "the document is not a mapping");
		const YAML::Node version = document[std::string{kVersionKey}];
		if (!version)
			Fail(document, "the document has no version");
		std::uint32_t found = 0;
		if (!version.IsScalar() || !ParseNumber(version.Scalar(), found))
			Fail(version, std::format("the version '{}' is not a whole number", Text(version)));
		if (found != expected.version)
			throw FormatVersionError{m_path.Text(), found, expected.version};
		Fields(value, document, true);
	}

  private:
	[[noreturn]] void Fail(const YAML::Node &node, std::string_view problem) const
	{
		const YAML::Mark mark = node.Mark();
		if (mark.is_null())
			throw FormatError{std::format("YamlArchive: {}: {}", m_path.Text(), problem)};
		throw FormatError{
		    std::format("YamlArchive: {}: {} (line {})", m_path.Text(), problem, mark.line + 1)};
	}

	static std::string Text(const YAML::Node &node)
	{
		if (node.IsScalar())
			return node.Scalar();
		if (node.IsMap())
			return "a mapping";
		if (node.IsSequence())
			return "a sequence";
		return "nothing";
	}

	void Fields(entt::meta_any &value, const YAML::Node &node, bool root)
	{
		if (!node.IsMap())
			Fail(
			    node, std::format(
			              "expected a mapping of '{}' fields, found {}", value.type().name(),
			              node.IsScalar() ? "'" + node.Scalar() + "'" : Text(node)));
		std::vector<std::string_view> seen;
		for (const auto &entry : node)
		{
			if (!entry.first.IsScalar())
				Fail(entry.first, "a field name is not a scalar");
			const std::string &name = entry.first.Scalar();
			if (root && name == kVersionKey)
				continue;
			const entt::meta_data field = Reflect::FindField(value.type(), name);
			const auto scope = m_path.Field(name);
			if (!field || !Reflect::HasAll(Reflect::AttributesOf(field), Attribute::Saved))
				Fail(
				    entry.first,
				    std::format("'{}' has no saved field '{}'", value.type().name(), name));
			if (std::ranges::find(seen, field.name()) != seen.end())
				Fail(entry.first, "the field is given twice");
			seen.push_back(field.name());

			// Read into the field's current value, so a nested class keeps the defaults its
			// owner gave it for anything the data leaves out.
			entt::meta_any member = field.get(value);
			Value(member, entry.second);
			if (!field.set(value, std::move(member)))
				throw UnsupportedTypeError{
				    std::format("YamlArchive: {}: the field could not be set", m_path.Text())};
		}
	}

	void Value(entt::meta_any &value, const YAML::Node &node)
	{
		const Shape shape = Detail::ShapeOf(value.type(), m_path.Text());
		switch (shape.kind)
		{
			case Kind::Bool:
			case Kind::Signed:
			case Kind::Unsigned:
			case Kind::Float: value = Leaf(shape, node); return;
			case Kind::String:
			{
				if (!node.IsScalar())
					Fail(node, std::format("expected a string, found {}", Text(node)));
				value = shape.make(&node.Scalar());
				return;
			}
			case Kind::Enum:
			{
				if (!node.IsScalar())
					Fail(
					    node,
					    std::format("expected a '{}' name, found {}", value.type().name(), Text(node)));
				entt::meta_any enumerator = Reflect::EnumValue(value.type(), node.Scalar());
				if (!enumerator)
					Fail(node, std::format("'{}' is not a '{}'", node.Scalar(), value.type().name()));
				value = std::move(enumerator);
				return;
			}
			case Kind::Class: Fields(value, node, false); return;
			case Kind::Sequence: Sequence(value, node); return;
			case Kind::Optional:
			{
				// Into the value it holds already, if any, as a nested class keeps its defaults.
				entt::meta_any held = shape.optional->hasValue(value)
				                          ? shape.optional->value(value)
				                          : shape.optional->valueType().construct();
				Value(held, node);
				value = shape.optional->make(held);
				return;
			}
		}
	}

	entt::meta_any Leaf(const Shape &shape, const YAML::Node &node)
	{
		alignas(8) std::array<std::byte, Detail::kMaxLeafBytes> components{};
		if (shape.count == 1)
			Component(shape, node, components.data());
		else
		{
			if (!node.IsSequence() || node.size() != shape.count)
				Fail(
				    node, std::format(
				              "expected a sequence of {} numbers, found {}", shape.count,
				              node.IsSequence() ? std::format("{}", node.size()) : Text(node)));
			for (std::size_t index = 0; index < shape.count; ++index)
				Component(shape, node[index], components.data() + index * shape.width);
		}
		return shape.make(components.data());
	}

	void Component(const Shape &shape, const YAML::Node &node, std::byte *bytes)
	{
		const auto store = [bytes](auto value) { std::memcpy(bytes, &value, sizeof(value)); };
		const std::string text = node.IsScalar() ? node.Scalar() : std::string{};
		switch (shape.kind)
		{
			case Kind::Bool:
				// YAML 1.2's two spellings, not 1.1's yes, no, on and off.
				if (text != "true" && text != "false")
					Fail(node, std::format("expected true or false, found {}", Describe(node)));
				store(text == "true");
				return;
			case Kind::Float:
			{
				bool parsed = false;
				if (shape.width == 4)
				{
					float value = 0.0f;
					parsed = node.IsScalar() && ParseFloat(text, value);
					store(value);
				}
				else
				{
					double value = 0.0;
					parsed = node.IsScalar() && ParseFloat(text, value);
					store(value);
				}
				if (!parsed)
					Fail(node, std::format("expected a number, found {}", Describe(node)));
				return;
			}
			case Kind::Signed:
			case Kind::Unsigned:
				if (!node.IsScalar() || !Integer(shape, text, bytes))
					Fail(
					    node, std::format(
					              "expected a whole number that fits {} {}-bit, found {}",
					              shape.kind == Kind::Signed ? "a signed" : "an unsigned",
					              shape.width * 8, Describe(node)));
				return;
			default: return;
		}
	}

	static bool Integer(const Shape &shape, std::string_view text, std::byte *bytes)
	{
		// Parsed at the field's own width, so from_chars reports a value out of its range.
		const auto parse = [text, bytes]<typename T>(T value)
		{
			if (!ParseNumber(text, value))
				return false;
			std::memcpy(bytes, &value, sizeof(T));
			return true;
		};
		if (shape.kind == Kind::Signed)
		{
			switch (shape.width)
			{
				case 1: return parse(std::int8_t{});
				case 2: return parse(std::int16_t{});
				case 4: return parse(std::int32_t{});
				default: return parse(std::int64_t{});
			}
		}
		switch (shape.width)
		{
			case 1: return parse(std::uint8_t{});
			case 2: return parse(std::uint16_t{});
			case 4: return parse(std::uint32_t{});
			default: return parse(std::uint64_t{});
		}
	}

	static std::string Describe(const YAML::Node &node)
	{
		return node.IsScalar() ? "'" + node.Scalar() + "'" : Text(node);
	}

	void Sequence(entt::meta_any &value, const YAML::Node &node)
	{
		if (!node.IsSequence())
			Fail(node, std::format("expected a sequence, found {}", Describe(node)));
		entt::meta_sequence_container elements = value.as_sequence_container();
		elements.clear();
		const entt::meta_type type = elements.value_type();
		for (std::size_t index = 0; index < node.size(); ++index)
		{
			const auto scope = m_path.Element(index);
			entt::meta_any element = type.construct();
			Value(element, node[index]);
			if (!elements.insert(elements.end(), std::move(element)))
				throw UnsupportedTypeError{
				    std::format("YamlArchive: {}: the element could not be inserted", m_path.Text())};
		}
	}

	Path m_path;
};
} // namespace

std::string YamlArchive::Write(const entt::meta_any &value, FormatVersion version)
{
	Writer writer{Detail::RootName(value.type())};
	writer.Root(value, version);
	return writer.Finish();
}

entt::meta_any YamlArchive::Read(
    std::string_view text, const entt::meta_type &type, FormatVersion expected)
{
	const std::string_view root = Detail::RootName(type);
	YAML::Node document;
	try
	{
		document = YAML::Load(std::string{text});
	}
	catch (const YAML::Exception &error)
	{
		throw FormatError{
		    std::format("YamlArchive: {}: {} (line {})", root, error.msg, error.mark.line + 1)};
	}

	entt::meta_any value = type.construct();
	if (!value)
		throw UnsupportedTypeError{
		    std::format("YamlArchive: {}: the type is not default constructible", root)};
	Reader reader{root};
	reader.Root(value, document, expected);
	return value;
}
} // namespace Jettison::Serialise
