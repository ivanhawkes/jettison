#pragma once

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Plugin.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <jettison/render/Overlay.h>
#include <jettison/resource/ResourceHandle.h>
#include <jettison/ui/StringTable.h>
#include <jettison/vfs/FileSystem.h>

namespace Jettison::Resource
{
class ResourceManager;
}

// What RmlUi is given to reach the engine: files through the VFS, time and messages through the
// engine's clock and log, and drawing into a Render::Overlay.
namespace Jettison::Ui::Detail
{
// Every file RmlUi opens (documents, stylesheets, templates, fonts, images) is read whole through the
// VFS when it is opened, so a mod's mount over the base shadows any of them.
class FileInterface final : public Rml::FileInterface
{
  public:
	explicit FileInterface(const Vfs::FileSystem &files) : m_files{files}
	{
	}

	Rml::FileHandle Open(const Rml::String &path) override;

	void Close(Rml::FileHandle file) override;

	size_t Read(void *buffer, size_t size, Rml::FileHandle file) override;

	bool Seek(Rml::FileHandle file, long offset, int origin) override;

	size_t Tell(Rml::FileHandle file) override;

	size_t Length(Rml::FileHandle file) override;

	// The paths whose file has changed, been added or gone, or been shadowed by another mount's since
	// RmlUi last opened them, each taken as seen, so it is reported once a change: what
	// UiSystem::ReloadChanged reads documents again on, as the resource manager reloads a file.
	std::vector<std::string> TakeChanged();

  private:
	struct OpenFile
	{
		std::vector<std::byte> bytes;
		std::size_t offset{0};
	};

	OpenFile *Find(Rml::FileHandle file);

	void Note(const std::string &path);

	const Vfs::FileSystem &m_files;
	std::map<Rml::FileHandle, std::unique_ptr<OpenFile>> m_open;

	// What each path RmlUi has opened named when it was opened, nothing for a path no mount had, and
	// a stylesheet's patch beside it. Only the topmost file of a path is compared, as the resource
	// manager compares it, so a patch edited under another mount's patch of the same stylesheet is
	// seen once something above it changes.
	std::map<std::string, std::optional<Vfs::FileInfo>, std::less<>> m_opened;
	Rml::FileHandle m_next{1};
};

// The UI's clock is the presentation time the frame gives it, its messages are the log's, and a
// document's text is translated by the string table as RmlUi reads it.
class SystemInterface final : public Rml::SystemInterface
{
  public:
	double GetElapsedTime() override
	{
		return m_time;
	}

	bool LogMessage(Rml::Log::Type type, const Rml::String &message) override;

	// Every text RmlUi reads passes here, a data view's included, so a character no face loaded maps
	// is found here too: logged once a character, naming the language, and counted a problem.
	int TranslateString(Rml::String &translated, const Rml::String &input) override;

	// The characters a face loaded maps.
	void Cover(const std::vector<char32_t> &characters)
	{
		m_covered.insert(characters.begin(), characters.end());
	}

	StringTable &Strings()
	{
		return m_strings;
	}

	const StringTable &Strings() const
	{
		return m_strings;
	}

	void SetTime(double seconds)
	{
		m_time = seconds;
	}

	// RmlUi's warnings and errors, and the string table's.
	std::size_t Problems() const
	{
		return m_problems + m_strings.Problems();
	}

  private:
	void CheckCovered(const Rml::String &text);

	double m_time{0.0};
	std::size_t m_problems{0};
	StringTable m_strings;
	std::unordered_set<char32_t> m_covered;
	std::unordered_set<char32_t> m_uncovered;
};

// RmlUi hands a document's element text to the system interface, but not its attributes: this hands
// it each attribute holding a [[key]] as the element is made, a placeholder, a title or a value, so
// it reads in the language as element text does. A data-* attribute is an expression, not text, and
// is left as it is.
class AttributeTranslator final : public Rml::Plugin
{
  public:
	explicit AttributeTranslator(SystemInterface &system) : m_system{system}
	{
	}

	int GetEventClasses() override
	{
		return EVT_ELEMENT;
	}

	void OnElementCreate(Rml::Element *element) override;

  private:
	SystemInterface &m_system;
};

// A document's image as the I/O worker decodes it, ready for the overlay.
struct DecodedImage
{
	Render::OverlayTexture texture;
};

// The loader of DecodedImage, which reads and decodes an image on the I/O worker. Throws
// std::logic_error if the manager has it already.
void RegisterImageLoader(Resource::ResourceManager &resources);

// RmlUi's drawing, recorded into the overlay: geometry compiled once and kept here, appended to the
// overlay each time it is drawn; textures held by the overlay, which the renderer uploads. Clip masks,
// layers, filters and shaders are left at RmlUi's defaults, which draw nothing for them
// (docs/roadmap.md, J23).
class RenderInterface final : public Rml::RenderInterface
{
  public:
	// With no resource manager a document's images are decoded on the main thread as it lays out.
	RenderInterface(
	    Render::Overlay &overlay, const Vfs::FileSystem &files, Resource::ResourceManager *pResources)
	    : m_overlay{overlay}, m_files{files}, m_pResources{pResources}
	{
	}

	~RenderInterface() override;

	RenderInterface(const RenderInterface &) = delete;

	RenderInterface &operator=(const RenderInterface &) = delete;

	Rml::CompiledGeometryHandle CompileGeometry(
	    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;

	void RenderGeometry(
	    Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
	    Rml::TextureHandle texture) override;

	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	// An image file by its path, PNG, JPEG or anything else stb_image reads. With a resource manager it
	// is read and decoded on the I/O worker, and until it is there RmlUi is given a texture that
	// draws nothing, of no size; one that fails is logged once and stays so.
	Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source) override;

	Rml::TextureHandle GenerateTexture(
	    Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;

	void ReleaseTexture(Rml::TextureHandle texture) override;

	void EnableScissorRegion(bool enable) override;

	void SetScissorRegion(Rml::Rectanglei region) override;

	void SetTransform(const Rml::Matrix4f *transform) override;

	std::size_t GeometryCount() const
	{
		return m_geometry.size();
	}

	// The images the worker has decoded since RmlUi asked for them, by path, which RmlUi is to ask
	// for again. One that failed is logged here, once, and stays blank.
	std::vector<std::string> TakeArrived();

	// After the documents are laid out: lets go of the images that arrived and that no element asked
	// for again (its source changed while it decoded, say), so none is held for the run. One asked
	// for later is read again.
	void DropUnclaimed();

	// Whether RmlUi is drawing: an image first asked for then is decoded on the spot, since a
	// decorator reads its image's size once, as its element is first drawn, and keeps it.
	void SetDrawing(bool drawing)
	{
		m_drawing = drawing;
	}

  private:
	struct Geometry
	{
		std::vector<Render::OverlayVertex> vertices;
		std::vector<std::uint32_t> indices;
	};

	struct ImageLoad
	{
		Resource::ResourceHandle<DecodedImage> handle;
		bool failed{false};
	};

	void ApplyScissor();

	Rml::TextureHandle Add(Rml::Vector2i &dimensions, Render::OverlayTexture texture);

	Render::Overlay &m_overlay;
	const Vfs::FileSystem &m_files;
	Resource::ResourceManager *m_pResources;
	std::map<std::string, ImageLoad, std::less<>> m_images;
	bool m_drawing{false};
	std::unordered_map<Rml::CompiledGeometryHandle, Geometry> m_geometry;
	Rml::CompiledGeometryHandle m_nextGeometry{1};
	bool m_scissorEnabled{false};
	Render::OverlayRect m_scissor;
};
} // namespace Jettison::Ui::Detail
