#include "Shape.h"

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Binary.h>
#include <jettison/serialise/Migration.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Jettison::Serialise
{
namespace
{
using Detail::Kind;
using Detail::Path;
using Detail::Shape;
using Reflect::Attribute;

constexpr std::array<std::byte, 4> kMagic{
    std::byte{'J'}, std::byte{'T'}, std::byte{'S'}, std::byte{'N'}};
// The layout described in Archive.h. A change to it is a new revision, read or refused here; a
// change to what a type saves is the type's own version, which FormatVersion carries.
constexpr std::uint16_t kLayoutRevision = 1;

// A leaf component, an integer or a float's bits, moved between its own width in memory and a
// 64-bit value, so the byte order on disk is set by the shifts that write it, not by the host.
std::uint64_t LoadBits(const std::byte *component, std::size_t width)
{
	const auto load = [component]<typename T>(T bits)
	{
		std::memcpy(&bits, component, sizeof(T));
		return std::uint64_t{bits};
	};
	switch (width)
	{
		case 1: return load(std::uint8_t{});
		case 2: return load(std::uint16_t{});
		case 4: return load(std::uint32_t{});
		default: return load(std::uint64_t{});
	}
}

void StoreBits(std::byte *component, std::uint64_t bits, std::size_t width)
{
	const auto store = [component](auto narrow) { std::memcpy(component, &narrow, sizeof(narrow)); };
	switch (width)
	{
		case 1: store(static_cast<std::uint8_t>(bits)); return;
		case 2: store(static_cast<std::uint16_t>(bits)); return;
		case 4: store(static_cast<std::uint32_t>(bits)); return;
		default: store(bits); return;
	}
}

// ---- Writing ------------------------------------------------------------------------------

class Writer
{
  public:
	explicit Writer(std::string_view root) : m_path{root}
	{
	}

	std::vector<std::byte> Finish(std::string_view root, FormatVersion version)
	{
		const std::uint32_t rootIndex = Intern(root);
		std::vector<std::byte> out;
		out.insert(out.end(), kMagic.begin(), kMagic.end());
		Unsigned(out, kLayoutRevision, 2);
		Unsigned(out, Count(m_names.size()), 4);
		for (const std::string_view name : m_names)
			String(out, name);
		Unsigned(out, rootIndex, 4);
		Unsigned(out, version.version, 4);
		out.insert(out.end(), m_payload.begin(), m_payload.end());
		return out;
	}

	void Class(const entt::meta_any &value)
	{
		// An empty optional is left out, as a field the data does not name.
		std::vector<std::pair<entt::meta_data, entt::meta_any>> fields;
		for (const entt::meta_data &field : Reflect::Fields(value.type(), Attribute::Saved))
			if (entt::meta_any member = field.get(value); !Detail::IsEmptyOptional(member))
				fields.emplace_back(field, std::move(member));
		Unsigned(m_payload, Count(fields.size()), 4);
		for (const auto &[field, member] : fields)
		{
			const auto scope = m_path.Field(field.name());
			Unsigned(m_payload, Intern(field.name()), 4);
			// The length is patched in once the value is written.
			const std::size_t lengthAt = m_payload.size();
			Unsigned(m_payload, 0, 4);
			Value(member);
			const std::size_t length = m_payload.size() - lengthAt - 4;
			std::array<std::byte, 4> bytes{};
			Store(bytes.data(), Count(length), 4);
			std::ranges::copy(bytes, m_payload.begin() + static_cast<std::ptrdiff_t>(lengthAt));
		}
	}

  private:
	static void Store(std::byte *out, std::uint64_t value, std::size_t width)
	{
		for (std::size_t index = 0; index < width; ++index)
			out[index] = static_cast<std::byte>((value >> (8 * index)) & 0xffu);
	}

	static void Unsigned(std::vector<std::byte> &out, std::uint64_t value, std::size_t width)
	{
		std::array<std::byte, 8> bytes{};
		Store(bytes.data(), value, width);
		out.insert(out.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(width));
	}

	void String(std::vector<std::byte> &out, std::string_view text) const
	{
		Unsigned(out, Count(text.size()), 4);
		const auto *bytes = reinterpret_cast<const std::byte *>(text.data());
		out.insert(out.end(), bytes, bytes + text.size());
	}

	std::uint32_t Count(std::size_t size) const
	{
		if (size > std::numeric_limits<std::uint32_t>::max())
			throw UnsupportedTypeError{std::format(
			    "BinaryArchive: {}: {} is too many for a 32-bit count", m_path.Text(), size)};
		return static_cast<std::uint32_t>(size);
	}

  public:
	// Starts from a table, as a record's field does, so the names its value uses are added after it.
	void Preload(std::span<const std::string> names)
	{
		for (const std::string &name : names)
			Intern(name);
	}
	const std::vector<std::string> &Names() const
	{
		return m_names;
	}
	std::vector<std::byte> TakePayload()
	{
		return std::move(m_payload);
	}

	// The table is in order of first use, so the same values write the same bytes. Names are
	// copied: a caller's name need not outlive the writer.
	std::uint32_t Intern(std::string_view name)
	{
		const auto [found, added] = m_indices.try_emplace(std::string{name}, Count(m_names.size()));
		if (added)
			m_names.emplace_back(name);
		return found->second;
	}

	void Unsigned(std::uint64_t value, std::size_t width)
	{
		Unsigned(m_payload, value, width);
	}

	void Bytes(std::span<const std::byte> bytes)
	{
		Unsigned(m_payload, Count(bytes.size()), 4);
		m_payload.insert(m_payload.end(), bytes.begin(), bytes.end());
	}

	void Value(const entt::meta_any &value)
	{
		const Shape shape = Detail::ShapeOf(value.type(), m_path.Text());
		switch (shape.kind)
		{
			case Kind::Bool:
			case Kind::Signed:
			case Kind::Unsigned:
			case Kind::Float:
			{
				// Each component little-endian at its own width; a float as its IEEE 754 bits.
				const auto *bytes = static_cast<const std::byte *>(shape.view(value));
				for (std::uint8_t index = 0; index < shape.count; ++index)
				{
					Unsigned(
					    m_payload, LoadBits(bytes + std::size_t{index} * shape.width, shape.width),
					    shape.width);
				}
				return;
			}
			case Kind::String:
				String(m_payload, *static_cast<const std::string *>(shape.view(value)));
				return;
			case Kind::Enum:
			{
				const std::string_view name = Reflect::EnumName(value);
				if (name.empty())
					throw UnsupportedTypeError{std::format(
					    "BinaryArchive: {}: the value is not an enumerator registered for '{}'",
					    m_path.Text(), value.type().name())};
				Unsigned(m_payload, Intern(name), 4);
				return;
			}
			case Kind::Class: Class(value); return;
			case Kind::Sequence:
			{
				// begin() is not const on a container, even one over a const value.
				entt::meta_sequence_container elements = value.as_sequence_container();
				Unsigned(m_payload, Count(elements.size()), 4);
				std::size_t index = 0;
				for (const entt::meta_any element : elements)
				{
					const auto scope = m_path.Element(index++);
					Value(element);
				}
				return;
			}
			case Kind::Optional:
				if (!shape.optional->hasValue(value))
					throw UnsupportedTypeError{std::format(
					    "BinaryArchive: {}: an empty optional can only be a field, which is left out",
					    m_path.Text())};
				Value(shape.optional->value(value));
				return;
		}
	}

  private:
	std::vector<std::byte> m_payload;
	std::vector<std::string> m_names;
	std::unordered_map<std::string, std::uint32_t> m_indices;
	Path m_path;
};

// ---- Reading ------------------------------------------------------------------------------

class Reader
{
  public:
	Reader(std::span<const std::byte> bytes, std::string_view root) : m_bytes{bytes}, m_path{root}
	{
	}
	// A value with no header, whose names index a table the caller keeps.
	Reader(
	    std::span<const std::byte> bytes, std::string_view root, const std::vector<std::string> &names)
	    : m_bytes{bytes}, m_pNames{&names}, m_path{root}
	{
	}
	// It points into itself.
	Reader(const Reader &) = delete;
	Reader &operator=(const Reader &) = delete;

	void Header(std::string_view root, FormatVersion expected)
	{
		if (const std::uint32_t version = Open(root); version != expected.version)
			throw FormatVersionError{root, version, expected.version};
	}

	// The header, up to the payload: the version the archive says it is.
	std::uint32_t Open(std::string_view root)
	{
		if (m_bytes.size() < kMagic.size() || !std::ranges::equal(m_bytes.first(kMagic.size()), kMagic))
			Fail("the data is not a Jettison binary archive");
		m_offset = kMagic.size();
		if (const auto revision = Unsigned(2); revision != kLayoutRevision)
			Fail(
			    std::format(
			        "binary layout revision {} found, revision {} expected", revision,
			        kLayoutRevision));
		// Not reserved, for the reason Value gives for a sequence.
		const std::uint32_t count = Count();
		for (std::uint32_t index = 0; index < count; ++index)
			m_names.push_back(String());
		if (const std::string_view found = Name(); found != root)
			Fail(std::format("the archive holds a '{}'", found));
		return static_cast<std::uint32_t>(Unsigned(4));
	}

	const std::vector<std::string> &Names() const
	{
		return *m_pNames;
	}
	std::span<const std::byte> Rest() const
	{
		return m_bytes.subspan(m_offset);
	}

	std::size_t Remaining() const
	{
		return m_bytes.size() - m_offset;
	}

	void End() const
	{
		if (m_offset != m_bytes.size())
			Fail(std::format("{} bytes follow the value", m_bytes.size() - m_offset));
	}

	void Class(entt::meta_any &value)
	{
		const std::uint32_t count = Count();
		std::vector<std::string_view> seen;
		for (std::uint32_t index = 0; index < count; ++index)
		{
			const std::string_view name = Name();
			const auto scope = m_path.Field(name);
			const entt::meta_data field = Reflect::FindField(value.type(), name);
			if (!field || !Reflect::HasAll(Reflect::AttributesOf(field), Attribute::Saved))
				Fail(std::format("'{}' has no saved field '{}'", value.type().name(), name));
			if (std::ranges::find(seen, field.name()) != seen.end())
				Fail("the field is given twice");
			seen.push_back(field.name());

			const std::uint32_t length = Count();
			if (length > m_bytes.size() - m_offset)
				Fail(std::format("the field holds {} bytes, past the end of the data", length));
			const std::size_t start = m_offset;
			entt::meta_any member = field.get(value);
			Value(member);
			// The length catches a field whose type changed size, which would otherwise shift
			// every field after it.
			if (m_offset - start != length)
				Fail(
				    std::format(
				        "the field holds {} bytes, and its type reads {}", length, m_offset - start));
			if (!field.set(value, std::move(member)))
				throw UnsupportedTypeError{
				    std::format("BinaryArchive: {}: the field could not be set", m_path.Text())};
		}
	}

	[[noreturn]] void Fail(std::string_view problem) const
	{
		throw FormatError{
		    std::format("BinaryArchive: {}: {} (at byte {})", m_path.Text(), problem, m_offset)};
	}

	std::span<const std::byte> Take(std::size_t size)
	{
		if (size > m_bytes.size() - m_offset)
			Fail(std::format("the data ends {} bytes short", size - (m_bytes.size() - m_offset)));
		const auto taken = m_bytes.subspan(m_offset, size);
		m_offset += size;
		return taken;
	}

	std::uint64_t Unsigned(std::size_t width)
	{
		std::uint64_t value = 0;
		const auto bytes = Take(width);
		for (std::size_t index = 0; index < width; ++index)
			value |= std::uint64_t{std::to_integer<std::uint8_t>(bytes[index])} << (8 * index);
		return value;
	}

	std::uint32_t Count()
	{
		return static_cast<std::uint32_t>(Unsigned(4));
	}

	std::string String()
	{
		// Take checks the length against what is left before anything is allocated.
		const auto bytes = Take(Count());
		return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
	}

	std::string_view Name()
	{
		const std::uint32_t index = Count();
		if (index >= m_pNames->size())
			Fail(std::format("name {} is past the end of the table of {}", index, m_pNames->size()));
		return (*m_pNames)[index];
	}

	// Into the object value holds or refers to: replacing the meta_any itself would leave a
	// reference's object as it was.
	void Assign(entt::meta_any &value, entt::meta_any made) const
	{
		if (!value.assign(std::move(made)))
			throw UnsupportedTypeError{
			    std::format("BinaryArchive: {}: the value could not be assigned", m_path.Text())};
	}

	void Value(entt::meta_any &value)
	{
		const Shape shape = Detail::ShapeOf(value.type(), m_path.Text());
		switch (shape.kind)
		{
			case Kind::Bool:
			case Kind::Signed:
			case Kind::Unsigned:
			case Kind::Float:
			{
				alignas(8) std::array<std::byte, Detail::kMaxLeafBytes> components{};
				for (std::uint8_t index = 0; index < shape.count; ++index)
				{
					const std::uint64_t bits = Unsigned(shape.width);
					// Any other byte in a bool is undefined behaviour once it is read as one.
					if (shape.kind == Kind::Bool && bits > 1)
						Fail(std::format("{} is not a bool", bits));
					StoreBits(components.data() + std::size_t{index} * shape.width, bits, shape.width);
				}
				Assign(value, shape.make(components.data()));
				return;
			}
			case Kind::String:
			{
				const std::string text = String();
				Assign(value, shape.make(&text));
				return;
			}
			case Kind::Enum:
			{
				const std::string_view name = Name();
				entt::meta_any enumerator = Reflect::EnumValue(value.type(), name);
				if (!enumerator)
					Fail(std::format("'{}' is not a '{}'", name, value.type().name()));
				Assign(value, std::move(enumerator));
				return;
			}
			case Kind::Class: Class(value); return;
			case Kind::Sequence:
			{
				entt::meta_sequence_container elements = value.as_sequence_container();
				elements.clear();
				const entt::meta_type type = elements.value_type();
				// Not reserved: a corrupt count would allocate before the data ran out. Every element
				// reads at least one byte, so a count past the data fails at its end.
				const std::uint32_t count = Count();
				for (std::uint32_t index = 0; index < count; ++index)
				{
					const auto scope = m_path.Element(index);
					entt::meta_any element = type.construct();
					Value(element);
					if (!elements.insert(elements.end(), std::move(element)))
						throw UnsupportedTypeError{std::format(
						    "BinaryArchive: {}: the element could not be inserted", m_path.Text())};
				}
				return;
			}
			case Kind::Optional:
			{
				entt::meta_any held = shape.optional->hasValue(value)
				                          ? shape.optional->value(value)
				                          : shape.optional->valueType().construct();
				Value(held);
				Assign(value, shape.optional->make(held));
				return;
			}
		}
	}

	std::span<const std::byte> m_bytes;
	std::size_t m_offset{0};
	std::vector<std::string> m_names;
	// The table names index: the archive's own, or a record's.
	const std::vector<std::string> *m_pNames{&m_names};
	Path m_path;
};
} // namespace

struct BinaryWriter::Impl
{
	explicit Impl(std::string_view format) : name{format}, writer{format}
	{
	}

	std::string name;
	Writer writer;
};

BinaryWriter::BinaryWriter(std::string_view format) : m_pImpl{std::make_unique<Impl>(format)}
{
}

BinaryWriter::~BinaryWriter() = default;

void BinaryWriter::WriteUnsigned(std::uint64_t value, std::size_t width)
{
	if (width != 1 && width != 2 && width != 4 && width != 8)
		throw std::invalid_argument{
		    std::format("BinaryWriter: {} bytes is not an integer width", width)};
	m_pImpl->writer.Unsigned(value, width);
}

void BinaryWriter::WriteName(std::string_view name)
{
	m_pImpl->writer.Unsigned(m_pImpl->writer.Intern(name), 4);
}

void BinaryWriter::WriteValue(const entt::meta_any &value)
{
	m_pImpl->writer.Value(value);
}

void BinaryWriter::WriteBytes(std::span<const std::byte> bytes)
{
	m_pImpl->writer.Bytes(bytes);
}

std::vector<std::byte> BinaryWriter::Finish(FormatVersion version)
{
	return m_pImpl->writer.Finish(m_pImpl->name, version);
}

struct BinaryReader::Impl
{
	Impl(std::span<const std::byte> bytes, std::string_view format) : name{format}, reader{bytes, name}
	{
	}
	// Over data migrated to the current version, which the reader keeps.
	Impl(std::vector<std::byte> owned, std::string_view format)
	    : name{format}, migrated{std::move(owned)}, reader{migrated, name}
	{
	}
	// Over a record's field.
	Impl(
	    std::span<const std::byte> bytes, const std::vector<std::string> &names,
	    std::string_view context)
	    : name{context}, reader{bytes, name, names}
	{
	}

	std::string name;
	std::vector<std::byte> migrated;
	Reader reader;
	// The table the records read share, copied from the archive's at the first.
	std::shared_ptr<std::vector<std::string>> pRecordNames;
};

BinaryReader::BinaryReader(
    std::span<const std::byte> bytes, std::string_view format, FormatVersion expected)
    : m_pImpl{std::make_unique<Impl>(bytes, format)}
{
	m_pImpl->reader.Header(m_pImpl->name, expected);
}

BinaryReader::BinaryReader(std::span<const std::byte> bytes, const BinaryMigrations &migrations)
{
	if (PeekVersion(bytes, migrations.Format()) == migrations.Current().version)
		m_pImpl = std::make_unique<Impl>(bytes, migrations.Format());
	else
		m_pImpl = std::make_unique<Impl>(Migrate(bytes, migrations), migrations.Format());
	m_pImpl->reader.Header(m_pImpl->name, migrations.Current());
}

std::vector<std::byte> BinaryReader::ReadBytes()
{
	const auto count = static_cast<std::uint32_t>(m_pImpl->reader.Unsigned(4));
	const std::span<const std::byte> bytes = m_pImpl->reader.Take(count);
	return {bytes.begin(), bytes.end()};
}

BinaryReader::BinaryReader(
    std::span<const std::byte> bytes, const std::vector<std::string> &names, std::string_view context)
    : m_pImpl{std::make_unique<Impl>(bytes, names, context)}
{
}

BinaryReader::~BinaryReader() = default;
BinaryReader::BinaryReader(BinaryReader &&) noexcept = default;
BinaryReader &BinaryReader::operator=(BinaryReader &&) noexcept = default;

BinaryRecord BinaryReader::ReadRecord()
{
	Reader &reader = m_pImpl->reader;
	if (!m_pImpl->pRecordNames)
		m_pImpl->pRecordNames = std::make_shared<std::vector<std::string>>(reader.Names());
	BinaryRecord record;
	record.m_pNames = m_pImpl->pRecordNames;
	record.m_context = m_pImpl->name;
	// Not reserved, for the reason Reader::Value gives for a sequence.
	const std::uint32_t count = reader.Count();
	for (std::uint32_t index = 0; index < count; ++index)
	{
		std::string name{reader.Name()};
		if (record.Has(name))
			reader.Fail(std::format("the record gives field '{}' twice", name));
		const std::span<const std::byte> bytes = reader.Take(reader.Count());
		record.m_fields.push_back({std::move(name), {bytes.begin(), bytes.end()}});
	}
	return record;
}

std::uint64_t BinaryReader::ReadUnsigned(std::size_t width)
{
	if (width != 1 && width != 2 && width != 4 && width != 8)
		throw std::invalid_argument{
		    std::format("BinaryReader: {} bytes is not an integer width", width)};
	return m_pImpl->reader.Unsigned(width);
}

std::string_view BinaryReader::ReadName()
{
	return m_pImpl->reader.Name();
}

void BinaryReader::ReadValue(entt::meta_any &value)
{
	m_pImpl->reader.Value(value);
}

std::size_t BinaryReader::Remaining() const
{
	return m_pImpl->reader.Remaining();
}

void BinaryReader::Fail(std::string_view problem) const
{
	m_pImpl->reader.Fail(problem);
}

void BinaryReader::End() const
{
	m_pImpl->reader.End();
}

// ---- Records ------------------------------------------------------------------------------

namespace
{
std::uint32_t InternInto(std::vector<std::string> &names, std::string_view name)
{
	const auto found = std::ranges::find(names, name);
	if (found != names.end())
		return static_cast<std::uint32_t>(found - names.begin());
	names.emplace_back(name);
	return static_cast<std::uint32_t>(names.size() - 1);
}

void PutUnsigned(std::vector<std::byte> &out, std::uint64_t value, std::size_t width)
{
	for (std::size_t index = 0; index < width; ++index)
		out.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xffu));
}
} // namespace

bool BinaryRecord::Has(std::string_view name) const
{
	return std::ranges::find(m_fields, name, &Field::name) != m_fields.end();
}

BinaryReader BinaryRecord::Open(std::string_view name) const
{
	const auto found = std::ranges::find(m_fields, name, &Field::name);
	if (found == m_fields.end())
		throw std::logic_error{
		    std::format("BinaryRecord: {}: the record has no field '{}'", m_context, name)};
	return BinaryReader{found->bytes, *m_pNames, std::format("{}.{}", m_context, name)};
}

void BinaryRecord::SetValue(std::string_view name, const entt::meta_any &value)
{
	if (!m_pNames)
		m_pNames = std::make_shared<std::vector<std::string>>();
	Writer writer{std::format("{}.{}", m_context, name)};
	writer.Preload(*m_pNames);
	writer.Value(value);
	// The names the value added go after the ones the table had, so the indices it wrote hold.
	const std::vector<std::string> &names = writer.Names();
	m_pNames->insert(
	    m_pNames->end(), names.begin() + static_cast<std::ptrdiff_t>(m_pNames->size()), names.end());
	std::vector<std::byte> bytes = writer.TakePayload();
	const auto found = std::ranges::find(m_fields, name, &Field::name);
	if (found != m_fields.end())
		found->bytes = std::move(bytes);
	else
		m_fields.push_back({std::string{name}, std::move(bytes)});
}

void BinaryRecord::Remove(std::string_view name)
{
	const auto found = std::ranges::find(m_fields, name, &Field::name);
	if (found == m_fields.end())
		throw std::logic_error{
		    std::format("BinaryRecord: {}: the record has no field '{}' to remove", m_context, name)};
	m_fields.erase(found);
}

void BinaryRecord::Rename(std::string_view from, std::string_view to)
{
	const auto found = std::ranges::find(m_fields, from, &Field::name);
	if (found == m_fields.end())
		throw std::logic_error{
		    std::format("BinaryRecord: {}: the record has no field '{}' to rename", m_context, from)};
	if (Has(to))
		throw std::logic_error{std::format(
		    "BinaryRecord: {}: the record has a field '{}' already, which '{}' would merge with",
		    m_context, to, from)};
	found->name = std::string{to};
}

void BinaryRecord::ReadInto(entt::meta_any &value) const
{
	// The record as a class's value is written, its names in the shared table, read as any is.
	std::vector<std::string> empty;
	std::vector<std::string> &names = m_pNames ? *m_pNames : empty;
	std::vector<std::byte> bytes;
	PutUnsigned(bytes, m_fields.size(), 4);
	for (const Field &field : m_fields)
	{
		PutUnsigned(bytes, InternInto(names, field.name), 4);
		PutUnsigned(bytes, field.bytes.size(), 4);
		bytes.insert(bytes.end(), field.bytes.begin(), field.bytes.end());
	}
	Reader reader{bytes, m_context, names};
	reader.Class(value);
	reader.End();
}

std::vector<std::byte> BinaryArchive::Write(const entt::meta_any &value, FormatVersion version)
{
	BinaryWriter writer{Detail::RootName(value.type())};
	writer.WriteValue(value);
	return writer.Finish(version);
}

entt::meta_any BinaryArchive::Read(
    std::span<const std::byte> bytes, const entt::meta_type &type, FormatVersion expected)
{
	const std::string_view root = Detail::RootName(type);
	BinaryReader reader{bytes, root, expected};
	entt::meta_any value = type.construct();
	if (!value)
		throw UnsupportedTypeError{
		    std::format("BinaryArchive: {}: the type is not default constructible", root)};
	reader.ReadValue(value);
	reader.End();
	return value;
}

entt::meta_any BinaryArchive::Read(
    std::span<const std::byte> bytes, const entt::meta_type &type, const BinaryMigrations &migrations)
{
	const std::string_view root = Detail::RootName(type);
	if (migrations.Format() != root)
		throw std::logic_error{std::format(
		    "BinaryArchive: the migrations of '{}' were given to read a '{}'", migrations.Format(),
		    root)};
	BinaryReader reader{bytes, migrations};
	entt::meta_any value = type.construct();
	if (!value)
		throw UnsupportedTypeError{
		    std::format("BinaryArchive: {}: the type is not default constructible", root)};
	reader.ReadValue(value);
	reader.End();
	return value;
}

// ---- Migration ----------------------------------------------------------------------------

BinaryDocument BinaryDocument::Parse(std::span<const std::byte> bytes, std::string_view format)
{
	Reader reader{bytes, format};
	BinaryDocument document;
	document.format = std::string{format};
	document.version = reader.Open(format);
	document.names = reader.Names();
	const std::span<const std::byte> payload = reader.Rest();
	document.payload.assign(payload.begin(), payload.end());
	return document;
}

std::vector<std::byte> BinaryDocument::Emit() const
{
	// Finish's layout, with the table as it stands: the format's name where it is in it, or after
	// the rest when a rename took it out.
	std::vector<std::string_view> table{names.begin(), names.end()};
	auto root = std::ranges::find(table, std::string_view{format});
	if (root == table.end())
		root = table.insert(table.end(), format);
	const auto rootIndex = static_cast<std::uint64_t>(root - table.begin());

	std::vector<std::byte> out;
	const auto put = [&out](std::uint64_t value, std::size_t width)
	{
		for (std::size_t index = 0; index < width; ++index)
			out.push_back(static_cast<std::byte>((value >> (8 * index)) & 0xffu));
	};
	out.insert(out.end(), kMagic.begin(), kMagic.end());
	put(kLayoutRevision, 2);
	put(table.size(), 4);
	for (const std::string_view name : table)
	{
		put(name.size(), 4);
		const auto *text = reinterpret_cast<const std::byte *>(name.data());
		out.insert(out.end(), text, text + name.size());
	}
	put(rootIndex, 4);
	put(version, 4);
	out.insert(out.end(), payload.begin(), payload.end());
	return out;
}

void BinaryDocument::Rename(std::string_view from, std::string_view to)
{
	const auto found = std::ranges::find(names, from);
	if (found == names.end())
		throw std::logic_error{
		    std::format("BinaryDocument: {}: the archive does not use the name '{}'", format, from)};
	if (std::ranges::find(names, to) != names.end())
		throw std::logic_error{std::format(
		    "BinaryDocument: {}: the archive already uses '{}', which '{}' would merge with", format,
		    to, from)};
	if (format == from)
		format = std::string{to};
	*found = std::string{to};
}

std::uint32_t PeekVersion(std::span<const std::byte> bytes, std::string_view format)
{
	Reader reader{bytes, format};
	return reader.Open(format);
}

std::vector<std::byte> Migrate(std::span<const std::byte> bytes, const BinaryMigrations &migrations)
{
	const std::uint32_t found = PeekVersion(bytes, migrations.Format());
	if (found == migrations.Current().version)
		return {bytes.begin(), bytes.end()};
	migrations.Check(found);
	BinaryDocument document = BinaryDocument::Parse(bytes, migrations.Format());
	migrations.Run(document, found);
	return document.Emit();
}
} // namespace Jettison::Serialise
