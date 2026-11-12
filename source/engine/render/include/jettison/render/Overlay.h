#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace Jettison::Render
{
// A vertex of the overlay, in the frame's pixels from its top left, y down. The colour is RGBA, red
// first in memory, encoded sRGB as a stylesheet gives it, with alpha premultiplied. Twenty bytes,
// laid out as RmlUi's vertex is, so the UI copies its geometry in without converting it.
struct OverlayVertex
{
	glm::vec2 position{0.0f};
	std::array<std::uint8_t, 4> color{255, 255, 255, 255};
	glm::vec2 uv{0.0f};
};
static_assert(sizeof(OverlayVertex) == 20);

// A texture the overlay holds, by an identifier it never gives out twice, so a draw naming one that
// was removed cannot draw another in its place. None draws untextured (as white).
using OverlayTextureId = std::uint64_t;
inline constexpr OverlayTextureId kNoOverlayTexture = 0;

// RGBA8, encoded sRGB, alpha premultiplied, the top row first: a font's glyphs or an image.
struct OverlayTexture
{
	std::uint32_t width{0};
	std::uint32_t height{0};
	std::vector<std::byte> texels;
};

// A rectangle of the frame, in pixels from its top left.
struct OverlayRect
{
	std::int32_t x{0};
	std::int32_t y{0};
	std::int32_t width{0};
	std::int32_t height{0};

	friend bool operator==(const OverlayRect &, const OverlayRect &) = default;
};

// One draw: a run of the frame's indices, into its vertices from vertexOffset, moved by translation
// (in pixels) and then by the transform, if it has one, clipped to the scissor, if it has one.
struct OverlayDraw
{
	std::uint32_t firstIndex{0};
	std::uint32_t indexCount{0};
	std::int32_t vertexOffset{0};
	glm::vec2 translation{0.0f};
	OverlayTextureId texture{kNoOverlayTexture};
	std::optional<OverlayRect> scissor;
	// An index into Transforms().
	std::optional<std::uint32_t> transform;
};

// Two-dimensional drawing over the frame (docs/architecture.md, "The frame"): what the game UI
// (engine/ui) draws, in the output pass after tonemapping, over the scene and under the dev tools.
// It names no device and no UI library: the UI fills it, the renderer reads it, and a test reads it
// without either.
//
// The draws are the frame's, cleared as each frame's UI is laid out; the textures last until they
// are removed, and the renderer uploads one the first time a draw names it and lets its copy go
// once the overlay no longer holds it.
class Overlay
{
  public:
	// Takes the texels, which must be width x height x 4 bytes; throws std::invalid_argument for
	// any other count, or a texture of no texels.
	OverlayTextureId AddTexture(OverlayTexture texture);
	// Does nothing for an identifier the overlay does not hold.
	void RemoveTexture(OverlayTextureId texture);
	const OverlayTexture *FindTexture(OverlayTextureId texture) const;
	const std::map<OverlayTextureId, OverlayTexture> &Textures() const
	{
		return m_textures;
	}

	// The draws, the vertices and indices, the transforms, the scissor and the transform in force;
	// not the textures.
	void Clear();

	// What the draws after this are clipped to, and moved by; none for the whole frame, unmoved.
	void SetScissor(const std::optional<OverlayRect> &scissor)
	{
		m_scissor = scissor;
	}
	void SetTransform(const std::optional<glm::mat4> &transform);

	// Appends a draw of the vertices by the indices, which count from the first of them. Throws
	// std::invalid_argument for an index past the vertices given, or a count of indices that is not
	// whole triangles, before appending anything.
	void Draw(
	    std::span<const OverlayVertex> vertices, std::span<const std::uint32_t> indices,
	    const glm::vec2 &translation, OverlayTextureId texture);

	const std::vector<OverlayVertex> &Vertices() const
	{
		return m_vertices;
	}
	const std::vector<std::uint32_t> &Indices() const
	{
		return m_indices;
	}
	const std::vector<OverlayDraw> &Draws() const
	{
		return m_draws;
	}
	const std::vector<glm::mat4> &Transforms() const
	{
		return m_transforms;
	}

  private:
	std::map<OverlayTextureId, OverlayTexture> m_textures;
	OverlayTextureId m_nextTexture{1};
	std::vector<OverlayVertex> m_vertices;
	std::vector<std::uint32_t> m_indices;
	std::vector<OverlayDraw> m_draws;
	std::vector<glm::mat4> m_transforms;
	std::optional<OverlayRect> m_scissor;
	std::optional<std::uint32_t> m_transform;
};
} // namespace Jettison::Render
