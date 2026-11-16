#include "vulkan/imgui/FontFaces.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <string>
#include <utility>

#include "jettison/vfs/FileSystem.h"

namespace Jettison::Render
{
namespace
{
// The faces the dev tools draw with. Roboto is in Tailwind's default stack and carries accented
// Latin, Greek and Cyrillic, so the UI face needs no fallback for the words a log line holds; Hack
// is the mono face of the Menlo and Consolas class that stack names, and is the face the console
// looks for by name (devtools/Views.cpp). The names after the first in each list are what an asset
// pack that ships no Hack, say, has instead: a missing face is skipped, as it always has been.
constexpr std::array<const char *, 3> kMonoFaces{
    "Hack-Regular.ttf", "Cousine-Regular.ttf", "VeraMono.ttf"};
constexpr std::array<const char *, 3> kUiFaces{
    "Roboto-Medium.ttf", "Karla-Regular.ttf", "DroidSans.ttf"};
} // namespace

DevToolFaces LoadDevToolFaces(const Vfs::FileSystem &assets, float size)
{
	DevToolFaces faces;
	const auto add = [&faces, &assets, size](const char *file, ImFont *&slot)
	{
		std::optional<std::vector<std::byte>> bytes = assets.Read(std::string{"fonts/"} + file);
		if (!bytes || bytes->empty())
			return;
		std::vector<std::byte> &data = faces.m_bytes.emplace_back(std::move(*bytes));
		ImFontConfig config;
		config.FontDataOwnedByAtlas = false;
		// The name AddFontFromFileTTF would have derived, by hand, because the memory loader leaves
		// it empty and the console reads the name (FontFaces.h says why). ImFontConfig's name is 40
		// bytes and a face's file is called less than that, so snprintf has room for the terminator;
		// snprintf and not std::format_to because the buffer is a fixed C array of that size.
		const int written = std::snprintf(config.Name, sizeof(config.Name), "%s", file);
		static_cast<void>(written); // the assert below is compiled out of a release build, and the
		// standard's own rule is that a check a run depends on goes through Log::Error; this one is
		// a development assert on an internal invariant, so it stays an assert and the value is read.
		IM_ASSERT(written > 0 && written < static_cast<int>(sizeof(config.Name)));
		if (ImFont *face = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
		        data.data(), static_cast<int>(data.size()), size, &config);
		    face && !slot)
			slot = face; // the first face of the list the assets carry wins, as the list is ranked
	};
	for (const char *file : kMonoFaces)
		add(file, faces.m_pMono);
	for (const char *file : kUiFaces)
		add(file, faces.m_pUi);
	// The UI face is the face everything that does not ask for mono draws in, and it is named, not
	// ordered: ImGui's default face is the first one added, Fonts[0], unless this says otherwise
	// (FontFaces.h). Null leaves ImGui's own default, which is what a run with no faces wants.
	ImGui::GetIO().FontDefault = faces.m_pUi;
	return faces;
}
} // namespace Jettison::Render
