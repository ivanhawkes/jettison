#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <jettison/render/vulkan/DeviceContext.h>

namespace Jettison::Render
{
class Model;

// Each model primitive's material on the device: its base-colour texture, decoded and mipmapped,
// and the descriptor set (set 1 of the mesh program) that binds it. Primitives without a texture
// bind a one-texel white fallback. A material does not change from frame to frame, so there is
// one set per primitive rather than one per frame in flight.
//
// Keyed on the Model's address, which a scene reload frees and the next allocation may reuse:
// the renderer calls Forget when the scene's generation moves, with the device idle.
class MaterialCache
{
  public:
	explicit MaterialCache(std::shared_ptr<DeviceContext> pDeviceContext)
	    : m_pDeviceContext{std::move(pDeviceContext)}
	{
	}

	MaterialCache(const MaterialCache &) = delete;
	MaterialCache &operator=(const MaterialCache &) = delete;

	// The layout is the renderer's; the cache allocates against it.
	void Init(VkDescriptorSetLayout materialLayout);
	void Destroy();

	// The set for a primitive, made on first use. Primitives are made in order, which the draw
	// loop asks for them in.
	VkDescriptorSet Ensure(const Model &model, std::size_t primitiveIndex);

	// Frees every model's material. The device has to be idle: a set or texture a submission in
	// flight reads cannot be freed underneath it.
	void Forget();

  private:
	struct Texture
	{
		Image image{};
		VkImageView view{VK_NULL_HANDLE};
		uint32_t mipLevels{1};
	};
	struct Material
	{
		Texture texture;
		bool ownsTexture{false};
		VkDescriptorPool pool{VK_NULL_HANDLE};
		VkDescriptorSet set{VK_NULL_HANDLE};
	};

	Texture CreateTexture(const unsigned char *pixels, int width, int height, const std::string &name);
	void DestroyTexture(Texture &texture);
	void GenerateMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mipLevels);

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	VkDescriptorSetLayout m_layout{VK_NULL_HANDLE};
	VkSampler m_sampler{VK_NULL_HANDLE};
	Texture m_fallback;
	std::unordered_map<const Model *, std::vector<Material>> m_materials;
};
} // namespace Jettison::Render
