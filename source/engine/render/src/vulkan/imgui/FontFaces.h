#pragma once

#include <cstddef>
#include <vector>

namespace Jettison::Vfs
{
class FileSystem;
}

struct ImFont;

namespace Jettison::Render
{
class ImGuiRenderer;

// The dev tools' faces, loaded as ImGui 1.93 wants them, which is measured and not assumed:
//
//  - The default face is the *first* one added, not the last: `ImGui::GetDefaultFont()` answers
//    `io.FontDefault ? io.FontDefault : Fonts[0]` (imgui.cpp), so the UI face is named in
//    `io.FontDefault` and the order the faces are added in carries no meaning. Measured before the
//    name was set, the whole UI drew in the mono face, Hack, because it is loaded first.
//  - A face loaded from memory has no name at all: the file's base name is filled in by
//    `AddFontFromFileTTF` alone (imgui_draw.cpp), and the dev tools read through the mounts, so
//    every face arrives from memory. The console finds its mono face by name (devtools/Views.cpp),
//    so each face is named here, with the same string the file loader would have derived. Measured
//    without it: the console found no mono face at run time, while a test that loads from a file
//    found one, which is how the gap was missed.
//
// The bytes are kept here, unread by ImGui between frames: ImGui reads glyphs from them as it
// bakes, and `FontDataOwnedByAtlas` is false, so they must outlive the context.
struct DevToolFaces
{
	std::vector<std::vector<std::byte>> m_bytes;
	// The mono face the console pushes for its lines, the first of the mono files the assets carry.
	ImFont *m_pMono{nullptr};
	// The face the UI is drawn in, and the one named in `io.FontDefault`.
	ImFont *m_pUi{nullptr};
};

// Reads the faces through the assets' mounts — from a pack as from a directory — rasterised at
// `size` (ImGuiRenderer::kFontSize, which is the size the theme's spacing is set against), names
// them, and sets `io.FontDefault` to the UI face. A face the asset tree does not have is skipped;
// `m_pMono` and `m_pUi` answer null when there is none, which is how a run with no fonts and every
// test without faces draws: the UI face is then ImGui's own default.
DevToolFaces LoadDevToolFaces(const Vfs::FileSystem &assets, float size);
} // namespace Jettison::Render
