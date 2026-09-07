#include "O5MTextureResource.h"

bool O5MTextureResource::doLoad(void) {
    const std::string& filePath = getfilePath();

    unsigned char* data = loadImageData(filePath, &m_width, &m_height, &m_channels);
    if(!data) {
        return false;
    }

    createVulkanImage(data, m_width, m_height, m_channels);
    freeImageData(data);

    return true; 
}

void O5MTextureResource::doUnload(void) {
    if (isloaded()) {
        m_data.reset();
    }
}
