#ifndef __O5MTEXTURERESOURCE_H
#define __O5MTEXTURERESOURCE_H

#include <cstdint>
#include <memory>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "Resources/O5MResource.h"
#include "RHI/O5MDevice.h"

// The shader must not decode a normal/ORM texture as sRGB, and must decode
// baseColor/emissive as sRGB. Keep that decision on the resource so every
// consumer (including RenderGraph imports) sees the same format.
enum class O5MTextureColorSpace : uint8_t {
    Srgb,
    Linear
};

//Not sure should we store ImageView or Sampler anything here
class O5MTextureResource : public O5MResource {
private:
    struct TextureData{
        //don't change order
        vk::raii::Image m_image = nullptr;
        vk::raii::DeviceMemory m_deviceMemory = nullptr;
        vk::raii::ImageView m_imageView = nullptr; //temproally we will simply create a default imageView, later this part will be moved into renderGraph
        // vk::raii::Sampler m_sampler = nullptr;
        vk::DeviceSize m_offset = 0;
    };

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_channels = 0;

    O5MDevice& m_device;
    O5MTextureColorSpace m_colorSpace = O5MTextureColorSpace::Srgb;

    std::unique_ptr<TextureData> m_data;

public:
    O5MTextureResource(const std::string& name, O5MDevice& device,
                       O5MTextureColorSpace colorSpace = O5MTextureColorSpace::Srgb) :
        O5MResource(name), m_device(device), m_colorSpace(colorSpace)
    {};

    ~O5MTextureResource() override { unload(); };

public:
    vk::Image getImage(void) const { return *m_data->m_image; }
    vk::DeviceMemory getDeviceMemory(void) const { return *m_data->m_deviceMemory; }

    vk::ImageView getImageView(void) const { return *m_data->m_imageView; }
    // vk::Sampler getSampler(void) const { return *m_data->m_sampler; }

    vk::DeviceSize getOffset(void) const { return m_data->m_offset; }

    int getWidth(void) const { return m_width; }
    int getHeight(void) const { return m_height; }
    O5MTextureColorSpace getColorSpace(void) const { return m_colorSpace; }
    vk::Format getFormat(void) const {
        return m_colorSpace == O5MTextureColorSpace::Srgb
            ? vk::Format::eR8G8B8A8Srgb
            : vk::Format::eR8G8B8A8Unorm;
    }

private:
    bool doLoad(void) override;
    void doUnload(void) override;
};

#endif // __O5MTEXTURERESOURCE_H
