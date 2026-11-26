// The dev tools' font loader (vulkan/imgui/FontFaces.h): the faces the assets ship, named as the
// console reads them, with the face the UI draws in named rather than ordered. Measured here
// because both facts are the runtime's, not a test's: a face loaded from memory has no name, and
// the default face is the first one added, so a test that loads from a file and reads Fonts.back()
// misses both.
#include <imgui.h>

#include <doctest.h>

#include <filesystem>
#include <string>
#include <unistd.h>

#include "jettison/vfs/FileSystem.h"
#include "vulkan/imgui/FontFaces.h"
#include "vulkan/imgui/ImGuiRenderer.h" // kFontSize, the size the theme's spacing is set against

using namespace Jettison;

namespace
{
class HeadlessImGui
{
  public:
	HeadlessImGui() : m_context{ImGui::CreateContext()}
	{
		// Owned, made current, and destroyed by pointer: ImGui's DestroyContext(nullptr) takes the
		// *current* context, and CreateContext hands the previous one back as current, so two
		// fixtures in one case, with the general allocator handing the same address back (measured),
		// would destroy one context twice.
		ImGui::SetCurrentContext(m_context);
		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = {640.0f, 360.0f};
		io.DeltaTime = 1.0f / 60.0f;
		io.IniFilename = nullptr;
	}


	~HeadlessImGui()
	{
		ImGuiContext *outer = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_context);
		ImGui::DestroyContext(m_context);
		if (outer != m_context)
			ImGui::SetCurrentContext(outer);
	}


	HeadlessImGui(const HeadlessImGui &) = delete;


	HeadlessImGui &operator=(const HeadlessImGui &) = delete;

  private:
	ImGuiContext *m_context{nullptr};
};


Vfs::FileSystem AssetFonts()
{
	Vfs::FileSystem files;
	files.AddMount({"assets", JETTISON_ASSET_SOURCE_DIR, false});
	return files;
}
} // namespace


TEST_CASE("Fonts: the faces the dev tools draw with are named, and the UI face is the drawn one")
{
	const Vfs::FileSystem assets = AssetFonts();
	const HeadlessImGui imgui;
	ImGuiIO &io = ImGui::GetIO();

	const Render::DevToolFaces faces =
	    Render::LoadDevToolFaces(assets, Render::ImGuiRenderer::kFontSize);
	REQUIRE(faces.m_pMono != nullptr);
	REQUIRE(faces.m_pUi != nullptr);

	// The names, on the record: what the console's mono rule reads, and what the memory loader does
	// not supply on its own.
	MESSAGE(
	    "mono: " << std::string{faces.m_pMono->GetDebugName()}
	             << ", ui: " << std::string{faces.m_pUi->GetDebugName()});
	CHECK(std::string{faces.m_pMono->GetDebugName()} == "Hack-Regular.ttf");
	CHECK(std::string{faces.m_pUi->GetDebugName()} == "Roboto-Medium.ttf");

	// The face the UI draws in is named, not last: the mono face is added first, so the order leaves
	// Hack as the face of every window that asks for none (ImGui's GetDefaultFont is io.FontDefault
	// ? io.FontDefault : Fonts[0], imgui.cpp:9235 at the pinned version).
	CHECK(io.FontDefault == faces.m_pUi);

	REQUIRE(io.Fonts->Build());
	unsigned char *pixels = nullptr;
	int width = 0;
	int height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	REQUIRE(pixels != nullptr);

	ImGui::NewFrame();
	CHECK(ImGui::GetFont() == faces.m_pUi); // the face a window gets when it asks for none
	ImGui::EndFrame();

	// The bytes are the loader's to keep, and more than the two named faces: the lists' fallbacks
	// load too, and the atlas reads glyphs from them as it bakes, so they outlive the context.
	CHECK(faces.m_bytes.size() >= 4);
	for (const std::vector<std::byte> &bytes : faces.m_bytes)
		CHECK(bytes.size() > 100); // ImGui's own heuristic for a font file
}


TEST_CASE("Fonts: a pack with no faces leaves the loader with none, and ImGui's own default draws")
{
	const HeadlessImGui imgui;
	ImGuiIO &io = ImGui::GetIO();

	// An assets tree with no fonts in it, on a scratch mount: the loader is asked for faces, gets
	// none, and says so, and the run draws in ImGui's own face — which is how every test and a run
	// with an asset pack that ships no faces at all looks.
	const std::filesystem::path scratch =
	    std::filesystem::temp_directory_path() /
	    ("jettison-font-faces-" + std::to_string(static_cast<long long>(::getpid())));
	std::filesystem::remove_all(scratch);
	std::filesystem::create_directories(scratch / "fonts");
	Vfs::FileSystem empty;
	empty.AddMount({"assets", scratch, false});
	const Render::DevToolFaces faces =
	    Render::LoadDevToolFaces(empty, Render::ImGuiRenderer::kFontSize);
	std::filesystem::remove_all(scratch);
	CHECK(faces.m_pMono == nullptr);
	CHECK(faces.m_pUi == nullptr);
	CHECK(faces.m_bytes.empty());
	CHECK(io.FontDefault == nullptr); // named null, which is how ImGui's own default is asked for

	// ImGui adds its own face to the atlas, and a frame needs the atlas built, which a backend
	// normally does: no backend here, so the frame's own requirement is met the way the dev tools'
	// tests meet it.
	unsigned char *pixels = nullptr;
	int width = 0;
	int height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	REQUIRE(pixels != nullptr);
	ImGui::NewFrame();
	CHECK(ImGui::GetFont() != nullptr); // ImGui adds its own face, ProggyClean, and draws in it
	ImGui::EndFrame();
}
