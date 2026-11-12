#include <jettison/render/Overlay.h>

#include <format>
#include <stdexcept>
#include <utility>

namespace Jettison::Render
{
OverlayTextureId Overlay::AddTexture(OverlayTexture texture)
{
	const std::size_t expected = std::size_t{texture.width} * texture.height * 4;
	if (expected == 0 || texture.texels.size() != expected)
		throw std::invalid_argument(
		    std::format(
		        "Overlay: a {} x {} texture has {} bytes of texels, not {}", texture.width,
		        texture.height, texture.texels.size(), expected));
	const OverlayTextureId id = m_nextTexture++;
	m_textures.emplace(id, std::move(texture));
	return id;
}

void Overlay::RemoveTexture(OverlayTextureId texture)
{
	m_textures.erase(texture);
}

const OverlayTexture *Overlay::FindTexture(OverlayTextureId texture) const
{
	const auto found = m_textures.find(texture);
	return found != m_textures.end() ? &found->second : nullptr;
}

void Overlay::Clear()
{
	m_vertices.clear();
	m_indices.clear();
	m_draws.clear();
	m_transforms.clear();
	m_scissor.reset();
	m_transform.reset();
}

void Overlay::SetTransform(const std::optional<glm::mat4> &transform)
{
	if (!transform)
	{
		m_transform.reset();
		return;
	}
	m_transform = static_cast<std::uint32_t>(m_transforms.size());
	m_transforms.push_back(*transform);
}

void Overlay::Draw(
    std::span<const OverlayVertex> vertices, std::span<const std::uint32_t> indices,
    const glm::vec2 &translation, OverlayTextureId texture)
{
	if (indices.size() % 3 != 0)
		throw std::invalid_argument(
		    std::format("Overlay: {} indices are not whole triangles", indices.size()));
	for (const std::uint32_t index : indices)
		if (index >= vertices.size())
			throw std::invalid_argument(
			    std::format("Overlay: index {} is past the {} vertices drawn", index, vertices.size()));
	if (indices.empty())
		return;
	m_draws.push_back(
	    {static_cast<std::uint32_t>(m_indices.size()), static_cast<std::uint32_t>(indices.size()),
	     static_cast<std::int32_t>(m_vertices.size()), translation, texture, m_scissor, m_transform});
	m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
	m_indices.insert(m_indices.end(), indices.begin(), indices.end());
}
} // namespace Jettison::Render
