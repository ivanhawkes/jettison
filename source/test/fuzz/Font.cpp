#include <jettison/ui/UiSystem.h>

#include <freetype/freetype.h>
#include <ft2build.h>
#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include "Fuzz.h"
#include "vulkan/imgui/FontFaces.h"

using namespace Jettison;

namespace
{
constexpr int kFrameWidth = 640;
constexpr int kFrameHeight = 360;
constexpr float kFaceSize = 16.0f;
constexpr float kFrameSeconds = 1.0f / 60.0f;
constexpr const char *kText = "Fuzzy jackdaws 0123456789 & é ß Ω Ж 日本";


// The family a face names itself, as RmlUi files it, read with a FreeType of the harness's own;
// empty for a face FreeType does not open.
std::string Family(std::span<const std::byte> bytes)
{
	FT_Library library = nullptr;
	if (FT_Init_FreeType(&library) != 0)
		return {};
	std::string family;
	FT_Face face = nullptr;
	if (FT_New_Memory_Face(
	        library, reinterpret_cast<const FT_Byte *>(bytes.data()),
	        static_cast<FT_Long>(bytes.size()), 0, &face) == 0)
	{
		if (face->family_name != nullptr)
			family = face->family_name;
		FT_Done_Face(face);
	}
	FT_Done_FreeType(library);
	return family;
}


// The game UI's load of a face, and a document's text drawn in it, so its glyphs are rasterised.
void LoadInUi(const Fuzz::Scratch &scratch, std::span<const std::byte> bytes)
{
	std::string family;
	for (const char character : Family(bytes))
		if (character != '"' && character != '\\' && character != '<' && character != '&')
			family.push_back(character);
	const std::string document = "<rml><head><style>body { font-family: \"" + family +
	                             "\"; font-size: 20px; }</style></head><body><p>" + kText +
	                             "</p></body></rml>";
	scratch.Put("ui/text.rml", std::as_bytes(std::span{document.data(), document.size()}));
	Ui::UiSystem ui{scratch.Files(), kFrameWidth, kFrameHeight};
	try
	{
		ui.LoadFont("fonts/fuzz.ttf");
	}
	catch (const std::runtime_error &)
	{
		return;
	}
	ui.Show(ui.LoadDocument("ui/text.rml"));
	ui.Update(0.0);
}


// The dev tools' load of their faces, the input as their mono face, and a frame of text drawn in it.
void LoadInDevTools(const Fuzz::Scratch &scratch)
{
	ImGuiContext *pContext = ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize = {static_cast<float>(kFrameWidth), static_cast<float>(kFrameHeight)};
	io.DeltaTime = kFrameSeconds;
	io.IniFilename = nullptr;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

	// The faces' bytes outlive the context, which reads glyphs from them.
	const Render::DevToolFaces faces = Render::LoadDevToolFaces(scratch.Files(), kFaceSize);
	ImGui::NewFrame();
	ImGui::TextUnformatted(kText);
	ImGui::Render();
	ImGui::DestroyContext(pContext);
}
} // namespace


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A face loaded by each of the engine's two loaders of fonts, as a mod's file shadowing the font
// each reads.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	static const Fuzz::Scratch scratch{"font"};
	const auto bytes = Fuzz::Bytes(pData, size);
	scratch.Put("fonts/fuzz.ttf", bytes);
	scratch.Put("fonts/Hack-Regular.ttf", bytes);
	LoadInUi(scratch, bytes);
	LoadInDevTools(scratch);
	return 0;
}
