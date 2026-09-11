#include "O5MTextureResource.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>

bool O5MTextureResource::doLoad(void) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        static_cast<const stbi_uc*>(getData()),
        static_cast<int>(getSize()),
        &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        return false;
    }

    m_width = static_cast<uint32_t>(width);
    m_height = static_cast<uint32_t>(height);
    m_channels = static_cast<uint32_t>(channels);

    vk::DeviceSize imageSize = m_width * m_height * 4;
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(m_width, m_height))));
    
    auto [stagingBuffer, stagingMemory] = 
        m_device.createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible);

    void* dataStaging = stagingMemory.mapMemory(0, imageSize);
    memcpy(dataStaging, pixels, imageSize);
    stagingMemory.unmapMemory();
    stbi_image_free(pixels);

    m_data = std::make_unique<TextureData>();
    std::tie(m_data->m_image, m_data->m_deviceMemory) = 
        m_device.createImage2D(
            vk::Format::eR8G8B8A8Srgb,
            {m_width, m_height},
            mipLevels,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
    m_device.copyBufferToImage(stagingBuffer, m_data->m_image, vk::Format::eR8G8B8A8Srgb, {m_width, m_height});
    stbi_image_free(pixels);

    return true;
}

void O5MTextureResource::doUnload(void) {
    if (isloaded()) {
        m_data.reset();
    }
}
