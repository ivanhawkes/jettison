#pragma once

#include <vulkan/vulkan.h>

#include <string>

#include <jettison/render/Texture.h>

#include "vulkan/DeviceContext.h"

namespace Jettison::Render
{
VkFormat ToVkFormat(TextureFormat format);

// A decoded texture made a device-local image with every level it has, and its view, through a
// staging buffer and a submission waited on, so it is ready to sample when this returns. Throws
// std::runtime_error naming the texture for one the device does not sample (BC7 without block
// compression) or one with no texels, and leaves image and view empty when it throws. Materials
// and the overlay's textures both come here.
void UploadTexture(
    DeviceContext &deviceContext, const TextureData &data, const std::string &name, Image &image,
    VkImageView &view);
} // namespace Jettison::Render
