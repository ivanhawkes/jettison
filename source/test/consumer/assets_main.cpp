// The consumer's half that needs the engine's assets: an executable pointed at them by
// jettison_use_shared_assets from this project's own scope, which the engine's directory-scoped
// variable could not serve. It runs from its own directory and exits non-zero if the staged assets
// are not where the definition says.
#include <cstdio>
#include <filesystem>

int main()
{
	const std::filesystem::path assets{JETTISON_ASSET_SUBDIRECTORY};
	for (const char *directory : {"fonts", "strings", "ui"})
	{
		if (!std::filesystem::is_directory(assets / directory))
		{
			std::fprintf(
			    stderr, "consumer_assets: no %s in %s\n", directory, JETTISON_ASSET_SUBDIRECTORY);
			return 1;
		}
	}
	return 0;
}
