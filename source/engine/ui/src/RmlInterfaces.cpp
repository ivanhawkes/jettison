#include "RmlInterfaces.h"

#include <RmlUi/Core/Log.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include <jettison/core/Log.h>
#include <jettison/render/Texture.h>
#include <jettison/vfs/FileSystem.h>

namespace Jettison::Ui::Detail
{
namespace
{
constexpr Core::Category kUi{"ui"};

static_assert(sizeof(Rml::Vertex) == sizeof(Render::OverlayVertex));
static_assert(offsetof(Rml::Vertex, colour) == offsetof(Render::OverlayVertex, color));
static_assert(offsetof(Rml::Vertex, tex_coord) == offsetof(Render::OverlayVertex, uv));
} // namespace


Rml::FileHandle FileInterface::Open(const Rml::String &path)
{
	try
	{
		// RmlUi asks for a path a document wrote, which may not be one the VFS accepts; that is a file
		// it does not have, not a reason to stop.
		if (!Vfs::FileSystem::IsValidPath(path))
			return 0;
		auto bytes = m_files.Read(path);
		if (!bytes)
			return 0;
		const Rml::FileHandle handle = m_next++;
		m_open.emplace(handle, std::make_unique<OpenFile>(OpenFile{std::move(*bytes), 0}));
		return handle;
	}
	catch (const std::exception &error)
	{
		Core::Log(Core::Severity::Error, kUi, "Could not read '{}': {}", path, error.what());
		return 0;
	}
}

void FileInterface::Close(Rml::FileHandle file)
{
	m_open.erase(file);
}

FileInterface::OpenFile *FileInterface::Find(Rml::FileHandle file)
{
	const auto found = m_open.find(file);
	return found != m_open.end() ? found->second.get() : nullptr;
}

size_t FileInterface::Read(void *buffer, size_t size, Rml::FileHandle file)
{
	OpenFile *open = Find(file);
	if (!open)
		return 0;
	const std::size_t count = std::min(size, open->bytes.size() - open->offset);
	std::memcpy(buffer, open->bytes.data() + open->offset, count);
	open->offset += count;
	return count;
}

bool FileInterface::Seek(Rml::FileHandle file, long offset, int origin)
{
	OpenFile *open = Find(file);
	if (!open)
		return false;
	long base = 0;
	if (origin == SEEK_CUR)
		base = static_cast<long>(open->offset);
	else if (origin == SEEK_END)
		base = static_cast<long>(open->bytes.size());
	const long target = base + offset;
	if (target < 0 || target > static_cast<long>(open->bytes.size()))
		return false;
	open->offset = static_cast<std::size_t>(target);
	return true;
}

size_t FileInterface::Tell(Rml::FileHandle file)
{
	const OpenFile *open = Find(file);
	return open ? open->offset : 0;
}

size_t FileInterface::Length(Rml::FileHandle file)
{
	const OpenFile *open = Find(file);
	return open ? open->bytes.size() : 0;
}


bool SystemInterface::LogMessage(Rml::Log::Type type, const Rml::String &message)
{
	Core::Severity severity = Core::Severity::Info;
	switch (type)
	{
		case Rml::Log::LT_ALWAYS:
		case Rml::Log::LT_ERROR:
		case Rml::Log::LT_ASSERT: severity = Core::Severity::Error; break;
		case Rml::Log::LT_WARNING: severity = Core::Severity::Warning; break;
		case Rml::Log::LT_INFO: severity = Core::Severity::Info; break;
		case Rml::Log::LT_DEBUG: severity = Core::Severity::Debug; break;
		default: severity = Core::Severity::Info; break;
	}
	if (severity >= Core::Severity::Warning && type != Rml::Log::LT_ALWAYS)
		++m_problems;
	Core::Log(severity, kUi, "{}", message);
	// True carries on: a failed RmlUi assertion is reported, not a trap into a debugger.
	return true;
}


Rml::CompiledGeometryHandle RenderInterface::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	Geometry geometry;
	geometry.vertices.resize(vertices.size());
	std::memcpy(geometry.vertices.data(), vertices.data(), vertices.size() * sizeof(Rml::Vertex));
	geometry.indices.reserve(indices.size());
	for (const int index : indices)
		geometry.indices.push_back(static_cast<std::uint32_t>(index));
	const Rml::CompiledGeometryHandle handle = m_nextGeometry++;
	m_geometry.emplace(handle, std::move(geometry));
	return handle;
}

void RenderInterface::RenderGeometry(
    Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture)
{
	const auto found = m_geometry.find(geometry);
	if (found == m_geometry.end())
		return;
	m_overlay.Draw(
	    found->second.vertices, found->second.indices, {translation.x, translation.y},
	    static_cast<Render::OverlayTextureId>(texture));
}

void RenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	m_geometry.erase(geometry);
}

Rml::TextureHandle RenderInterface::LoadTexture(Rml::Vector2i &dimensions, const Rml::String &source)
{
	try
	{
		if (!Vfs::FileSystem::IsValidPath(source))
			throw std::runtime_error("it is not a path in the asset tree");
		const auto bytes = m_files.Read(source);
		if (!bytes)
			throw std::runtime_error("there is no such file");
		// Decoded here, on the main thread, as RmlUi lays out the element that shows it: a screen's
		// images are small, and the layout needs their size before the frame goes on.
		const Render::TextureData decoded = Render::DecodeImage(*bytes, true, source);
		const Render::TextureLevel &top = decoded.levels.front();
		Render::OverlayTexture texture{top.width, top.height, {}};
		texture.texels.assign(
		    decoded.bytes.begin() + static_cast<std::ptrdiff_t>(top.offset),
		    decoded.bytes.begin() + static_cast<std::ptrdiff_t>(top.offset + top.size));
		// RmlUi draws with alpha premultiplied, and a file holds it straight.
		for (std::size_t texel = 0; texel + 3 < texture.texels.size(); texel += 4)
		{
			const auto alpha = static_cast<unsigned>(texture.texels[texel + 3]);
			for (std::size_t channel = 0; channel < 3; ++channel)
				texture.texels[texel + channel] = static_cast<std::byte>(
				    (static_cast<unsigned>(texture.texels[texel + channel]) * alpha + 127) / 255);
		}
		dimensions = {static_cast<int>(top.width), static_cast<int>(top.height)};
		return static_cast<Rml::TextureHandle>(m_overlay.AddTexture(std::move(texture)));
	}
	catch (const std::exception &error)
	{
		Rml::Log::Message(
		    Rml::Log::LT_WARNING, "Image '%s' not loaded: %s", source.c_str(), error.what());
		return 0;
	}
}

Rml::TextureHandle RenderInterface::GenerateTexture(
    Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions)
{
	Render::OverlayTexture texture{
	    static_cast<std::uint32_t>(dimensions.x), static_cast<std::uint32_t>(dimensions.y),
	    std::vector<std::byte>(source.size())};
	std::memcpy(texture.texels.data(), source.data(), source.size());
	return static_cast<Rml::TextureHandle>(m_overlay.AddTexture(std::move(texture)));
}

void RenderInterface::ReleaseTexture(Rml::TextureHandle texture)
{
	m_overlay.RemoveTexture(static_cast<Render::OverlayTextureId>(texture));
}

void RenderInterface::EnableScissorRegion(bool enable)
{
	m_scissorEnabled = enable;
	ApplyScissor();
}

void RenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
	m_scissor = {region.Left(), region.Top(), region.Width(), region.Height()};
	ApplyScissor();
}

void RenderInterface::ApplyScissor()
{
	m_overlay.SetScissor(m_scissorEnabled ? std::optional{m_scissor} : std::nullopt);
}

void RenderInterface::SetTransform(const Rml::Matrix4f *transform)
{
	// Column-major, as GLM is (RMLUI_MATRIX_ROW_MAJOR is off).
	m_overlay.SetTransform(transform ? std::optional{glm::make_mat4(transform->data())} : std::nullopt);
}
} // namespace Jettison::Ui::Detail
