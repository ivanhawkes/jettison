#include <jettison/ui/StringTable.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "Fuzz.h"

using namespace Jettison;


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A string table, the base language's, read as a language's tables are, then everything that reads
// a text it holds by key: the numbers' formats, which index the table's own, and a key's forms.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	static const Fuzz::Scratch scratch{"strings"};
	scratch.Put("strings/en/fuzz.yaml", Fuzz::Bytes(pData, size));
	Ui::StringTable strings;
	try
	{
		strings.Load(scratch.Files(), "en");
	}
	catch (const std::runtime_error &)
	{
		return 0;
	}
	using namespace std::chrono;
	strings.Number(-1'234'567);
	strings.Decimal(-1234.5678, 2);
	strings.Date(year{2026} / November / 28);
	strings.Time(hours{15} + minutes{5});
	int replaced = 0;
	strings.Translate("[[fuzz]] [[fuzz.one]] [[]] [[numbers.date", replaced);
	const std::vector<std::string> arguments{"one", "two", "three"};
	strings.FormatArguments("fuzz", arguments);
	strings.PluralArguments("fuzz", 3, arguments);
	return 0;
}
