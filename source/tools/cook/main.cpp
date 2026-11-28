#include "CookTree.h"

#include <exception>
#include <filesystem>

#include <jettison/core/Log.h>

// jettison_cook <source> <output>: the source tree cooked into the output tree once (CookTree.h).
// Exits 1 when any source was refused, after the rest are cooked and each refusal is reported.

namespace
{
constexpr Jettison::Core::Category kCook{"cook"};

constexpr int kUsageError = 2;
} // namespace


int main(int argc, char **argv)
{
	using Jettison::Core::Log;
	using Jettison::Core::Severity;

	if (argc != 3)
	{
		Log(Severity::Error, kCook, "Usage: jettison_cook <source> <output>");
		return kUsageError;
	}
	try
	{
		const Jettison::Cook::CookReport report =
		    Jettison::Cook::CookTree(std::filesystem::path{argv[1]}, std::filesystem::path{argv[2]});
		Log(Severity::Info, kCook, "{} cooked, {} unchanged, {} deleted, {} refused",
		    report.cooked.size(), report.skipped.size(), report.deleted.size(), report.refusals.size());
		return report.refusals.empty() ? 0 : 1;
	}
	catch (const std::exception &error)
	{
		Log(Severity::Error, kCook, "{}", error.what());
		return 1;
	}
}
