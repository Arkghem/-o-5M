#ifndef __O5MTEXTURERESOURCE_H
#define __O5MTEXTURERESOURCE_H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "O5MResource.h"

class O5MTextureResource : public O5MResource {
private:
    struct TextureData{
        //don't change order
        vk::raii::Image m_image = nullptr;
        vk::raii::DeviceMemory m_deviceMemory = nullptr;
        vk::raii::ImageView m_imageView = nullptr;
        vk::raii::Sampler m_sampler = nullptr;
        vk::DeviceSize m_offset = 0;
    };

    int m_width = 0;
    int m_height = 0;
    int m_channels = 0;
    std::unique_ptr<TextureData> m_data;
public:
    O5MTextureResource(const std::string &name) : O5MResource(name){};
    ~O5MTextureResource() override { unload(); };
public:
    vk::Image getImage(void) const { return *m_data->m_image; }
    vk::DeviceMemory getDeviceMemory(void) const { return *m_data->m_deviceMemory; }
    vk::ImageView getImageView(void) const { return *m_data->m_imageView; }
    vk::Sampler getSampler(void) const { return *m_data->m_sampler; }
    vk::DeviceSize getOffset(void) const { return m_data->m_offset; }

    int getWidth(void) const { return m_width; }
    int getHeight(void) const { return m_height; }
private:
    bool doLoad(void) override;
    void doUnload(void) override;

    unsigned char* loadImageData(const std::string& fileName, int* width, int* height, int* channels);
    void freeImageData(unsigned char* data);
    void createVulkanImage(unsigned char* data, int width, int hegint, int channels);
    vk::Device getDevice(void);
};

#endif // __O5MTEXTURERESOURCE_H
