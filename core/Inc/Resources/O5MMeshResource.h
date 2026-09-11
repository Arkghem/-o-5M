#ifndef __O5MMESHRESOURCE_H
#define __O5MMESHRESOURCE_H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "Resources/O5MResource.h"
#include "RHI/O5MDevice.h"

class O5MMeshResource : public O5MResource {
private:
    struct Vertex {
        glm::vec3 m_pos;
        glm::vec3 m_color;
        glm::vec2 m_texCoord;
    };

    struct MeshData{
        vk::raii::Buffer m_vertexBuffer = nullptr;
        vk::raii::DeviceMemory m_vertexBufferMemory = nullptr;
        vk::DeviceSize m_vertexBufferOffset = 0;
        uint32_t m_vertexCount = 0;

        vk::raii::Buffer m_indexBuffer = nullptr;
        vk::raii::DeviceMemory m_indexBufferMemory = nullptr;
        vk::DeviceSize m_indexBufferOffset = 0;
        uint32_t m_indexCount = 0;
    };

    O5MDevice& m_device;
    std::unique_ptr<MeshData> m_meshData;
public:
    O5MMeshResource(const std::string& name, O5MDevice& device) : 
        O5MResource(name),
        m_device(device){};
    ~O5MMeshResource() override { unload(); };
public:
    vk::Buffer getVertexBuffer(void) const { return *m_meshData->m_vertexBuffer; }
    vk::DeviceMemory getVertexBufferMemory(void) const { return *m_meshData->m_vertexBufferMemory; }
    vk::DeviceSize getVertexBufferOffset(void) const { return m_meshData->m_vertexBufferOffset; }
    uint32_t getVertexCount(void) const { return m_meshData->m_vertexCount; }

    vk::Buffer getIndexBuffer(void) const { return *m_meshData->m_indexBuffer; }
    vk::DeviceMemory getIndexBufferMemory(void) const { return *m_meshData->m_indexBufferMemory; }
    vk::DeviceSize getIndexBufferOffset(void) const { return m_meshData->m_indexBufferOffset; }
    uint32_t getIndexCount(void) const { return m_meshData->m_indexCount; }
private:
    bool doLoad(void) override;
    void doUnload(void) override;

    bool loadMeshData(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    void createVertexBuffer(std::vector<Vertex>& vertices);
    void createIndexBuffer(std::vector<uint32_t>& indices);
};

#endif // __O5MMESHRESOURCE_H
