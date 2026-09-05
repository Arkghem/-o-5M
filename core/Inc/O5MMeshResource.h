#ifndef __O5MMESHRESOURCE_H
#define __O5MMESHRESOURCE_H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "O5MResource.h"

class O5MMeshResource : public O5MResource {
private:
    struct Vertex {
        glm::vec3 m_pos;
        glm::vec3 m_color;
        glm::vec2 m_texCoord;
    };

    struct MeshData{
        vk::raii::Buffer m_vertexBuffer;
        vk::raii::DeviceMemory m_vertexBufferMemory;
        vk::DeviceSize m_vertexBufferOffset;
        uint32_t m_vertexCount = 0;

        vk::raii::Buffer m_indexBuffer;
        vk::raii::DeviceMemory m_indexBufferMemory;
        vk::DeviceSize m_indexBufferOffset;
        uint32_t m_indexCount = 0;
    };

    vk::raii::Device m_device = nullptr;
    std::unique_ptr<MeshData> m_data;
public:
    O5MMeshResource(const std::string &name) : O5MResource(name){};
    ~O5MMeshResource() override { unload(); };
public:
    vk::Buffer getVertexBuffer(void) const { return *m_data->m_vertexBuffer; }
    vk::DeviceMemory getVertexBufferMemory(void) const { return *m_data->m_vertexBufferMemory; }
    vk::DeviceSize getVertexBufferOffset(void) const { return m_data->m_vertexBufferOffset; }
    uint32_t getVertexCount(void) const { return m_data->m_vertexCount; }

    vk::Buffer getIndexBuffer(void) const { return *m_data->m_indexBuffer; }
    vk::DeviceMemory getIndexBufferMemory(void) const { return *m_data->m_indexBufferMemory; }
    vk::DeviceSize getIndexBufferOffset(void) const { return m_data->m_indexBufferOffset; }
    uint32_t getIndexCount(void) const { return m_data->m_indexCount; }
    vk::Device getDevice(void) const { return *m_device; };
private:
    bool doLoad(void) override;
    void doUnload(void) override;

    bool loadMeshData(std::string& filePath, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    void createVertexBuffer(std::vector<Vertex>& vertices);
    void createIndexBuffer(std::vector<uint32_t>& indices);
};

#endif // __O5MMESHRESOURCE_H
