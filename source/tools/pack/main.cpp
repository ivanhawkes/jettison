#include <jettison/core/Log.h>
#include <jettison/vfs/Pack.h>

#include <charconv>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// jettison_pack <directory> <pack> [--level <1-22>] [--directory <virtual path>]...: the files under
// the directory, or under the directories named within it, into a pack, which a Vfs::FileSystem
// mounts as it would the directory.

namespace
{
constexpr Jettison::Core::Category kPack{"pack"};

int Usage()
{
	Jettison::Core::Log(
	    Jettison::Core::Severity::Error, kPack,
	    "Usage: jettison_pack <directory> <pack> [--level <1-22>] [--directory <virtual path>]...");
	return 2;
}
} // namespace

int main(int argc, char **argv)
{
	using Jettison::Core::Log;
	using Jettison::Core::Severity;

	if (argc < 3)
		return Usage();
	Jettison::Vfs::PackOptions options;
	for (int index = 3; index < argc; ++index)
	{
		const std::string_view flag{argv[index]};
		if (index + 1 == argc)
			return Usage();
		const std::string_view value{argv[++index]};
		if (flag == "--level")
		{
			const auto [end, error] =
			    std::from_chars(value.data(), value.data() + value.size(), options.level);
			if (error != std::errc{} || end != value.data() + value.size())
				return Usage();
		}
		else if (flag == "--directory")
			options.directories.emplace_back(value);
		else
			return Usage();
	}

	const std::filesystem::path pack{argv[2]};
	try
	{
		const Jettison::Vfs::PackSummary summary =
		    Jettison::Vfs::WritePack(std::filesystem::path{argv[1]}, pack, options);
		for (const std::string &skipped : summary.skipped)
			Log(Severity::Warning, kPack, "{} is left out: no virtual path can name it", skipped);
		Log(Severity::Info, kPack, "{}: {} files, {} of them compressed, {} bytes in {}", pack.string(),
		    summary.files, summary.compressed, summary.bytes, summary.packedBytes);
	}
	catch (const std::exception &error)
	{
		Log(Severity::Error, kPack, "{}", error.what());
		return 1;
	}
	return 0;
}
