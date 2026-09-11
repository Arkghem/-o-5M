#ifndef __O5MTEXTURERESOURCE_H
#define __O5MTEXTURERESOURCE_H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "Resources/O5MResource.h"
#include "RHI/O5MDevice.h"

//Not sure should we store ImageView or Sampler anything here
class O5MTextureResource : public O5MResource {
private:
    struct TextureData{
        //don't change order
        vk::raii::Image m_image = nullptr;
        vk::raii::DeviceMemory m_deviceMemory = nullptr;
        // vk::raii::ImageView m_imageView = nullptr;
        // vk::raii::Sampler m_sampler = nullptr;
        vk::DeviceSize m_offset = 0;
    };

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_channels = 0;
    O5MDevice& m_device;
    std::unique_ptr<TextureData> m_data;
public:
    O5MTextureResource(const std::string& name, O5MDevice& device) : 
        O5MResource(name), m_device(device)
    {};
    ~O5MTextureResource() override { unload(); };
public:
    vk::Image getImage(void) const { return *m_data->m_image; }
    vk::DeviceMemory getDeviceMemory(void) const { return *m_data->m_deviceMemory; }

    // vk::ImageView getImageView(void) const { return *m_data->m_imageView; }
    // vk::Sampler getSampler(void) const { return *m_data->m_sampler; }

    vk::DeviceSize getOffset(void) const { return m_data->m_offset; }

    int getWidth(void) const { return m_width; }
    int getHeight(void) const { return m_height; }
private:
    bool doLoad(void) override;
    void doUnload(void) override;
};

#endif // __O5MTEXTURERESOURCE_H
