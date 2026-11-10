#include <jettison/core/StringId.h>

#include <mutex>
#include <string>
#include <unordered_map>

#include <jettison/core/Assert.h>

namespace Jettison::Core
{
namespace
{
#ifndef NDEBUG
// Hash to text, for every ID made at run time. unordered_map nodes do not move on rehash, so a
// view of a stored string stays valid for as long as the program runs: nothing is ever removed.
struct Table
{
	std::mutex mutex;
	std::unordered_map<std::uint64_t, std::string> texts;
};

Table &GetTable()
{
	static Table table;
	return table;
}
#endif
} // namespace

void StringId::Record([[maybe_unused]] std::string_view text) const
{
#ifndef NDEBUG
	Table &table = GetTable();
	const std::scoped_lock lock{table.mutex};
	const auto [entry, inserted] = table.texts.try_emplace(m_hash, text);
	// Two names, one ID: everything keyed on it would confuse the two. Rename one of them.
	JETTISON_VERIFY(inserted || entry->second == text);
#endif
}

std::string_view StringId::Text() const
{
#ifndef NDEBUG
	Table &table = GetTable();
	const std::scoped_lock lock{table.mutex};
	if (const auto entry = table.texts.find(m_hash); entry != table.texts.end())
		return entry->second;
#endif
	return "<unknown>";
}
} // namespace Jettison::Core
