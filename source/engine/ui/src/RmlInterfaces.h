#pragma once

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include <jettison/render/Overlay.h>
#include <jettison/ui/StringTable.h>

namespace Jettison::Vfs
{
class FileSystem;
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

  private:
	struct OpenFile
	{
		std::vector<std::byte> bytes;
		std::size_t offset{0};
	};

	OpenFile *Find(Rml::FileHandle file);

	const Vfs::FileSystem &m_files;
	std::map<Rml::FileHandle, std::unique_ptr<OpenFile>> m_open;
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
	int TranslateString(Rml::String &translated, const Rml::String &input) override;

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
	double m_time{0.0};
	std::size_t m_problems{0};
	StringTable m_strings;
};

// RmlUi's drawing, recorded into the overlay: geometry compiled once and kept here, appended to the
// overlay each time it is drawn; textures held by the overlay, which the renderer uploads. Clip masks,
// layers, filters and shaders are left at RmlUi's defaults, which draw nothing for them
// (docs/roadmap.md, "Carried forward").
class RenderInterface final : public Rml::RenderInterface
{
  public:
	RenderInterface(Render::Overlay &overlay, const Vfs::FileSystem &files)
	    : m_overlay{overlay}, m_files{files}
	{
	}

	Rml::CompiledGeometryHandle CompileGeometry(
	    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(
	    Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
	    Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	// An image file by its path, PNG, JPEG or anything else stb_image reads, decoded as it loads.
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

  private:
	struct Geometry
	{
		std::vector<Render::OverlayVertex> vertices;
		std::vector<std::uint32_t> indices;
	};

	void ApplyScissor();

	Render::Overlay &m_overlay;
	const Vfs::FileSystem &m_files;
	std::unordered_map<Rml::CompiledGeometryHandle, Geometry> m_geometry;
	Rml::CompiledGeometryHandle m_nextGeometry{1};
	bool m_scissorEnabled{false};
	Render::OverlayRect m_scissor;
};
} // namespace Jettison::Ui::Detail
