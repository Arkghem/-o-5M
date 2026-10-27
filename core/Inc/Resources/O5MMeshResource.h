#ifndef __O5MMESHRESOURCE_H
#define __O5MMESHRESOURCE_H

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "Resources/O5MResource.h"
#include "RHI/O5MDevice.h"

class O5MMaterialResource;

// Geometry resource: one glTF mesh owns one vertex/index buffer pair. Each
// primitive becomes a SubMesh range with its default material. The resource is
// source-agnostic; only O5MModelAsset knows how to fill it from a file.
class O5MMeshResource : public O5MResource {
public:
    struct Vertex {
        glm::vec3 m_pos;
        glm::vec3 m_color;
        glm::vec4 m_norm;
        glm::vec3 m_tangent;
        glm::vec2 m_texCoord;
    };

    struct SubMesh {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        O5MResourceHandle<O5MMaterialResource> material;
    };

    struct MeshGeometry {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<SubMesh> subMeshes;
        // radius == 0 means "not computed"; setGeometry() derives it if needed.
        glm::vec4 boundsSphere{0.0f, 0.0f, 0.0f, 0.0f};
    };

private:
    struct MeshData {
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

    std::optional<MeshGeometry> m_geometry;
    std::vector<SubMesh> m_subMeshes;
    glm::vec4 m_boundsSphere{0.0f, 0.0f, 0.0f, 1.0f};

public:
    O5MMeshResource(const std::string& name, O5MDevice& device) :
        O5MResource(name),
        m_device(device) {}

    ~O5MMeshResource() override { unload(); }

public:
    vk::Buffer getVertexBuffer(void) const {
        return m_meshData ? *m_meshData->m_vertexBuffer : vk::Buffer{};
    }
    vk::DeviceMemory getVertexBufferMemory(void) const {
        return m_meshData ? *m_meshData->m_vertexBufferMemory : vk::DeviceMemory{};
    }
    vk::DeviceSize getVertexBufferOffset(void) const {
        return m_meshData ? m_meshData->m_vertexBufferOffset : 0;
    }
    uint32_t getVertexCount(void) const {
        return m_meshData ? m_meshData->m_vertexCount : 0;
    }

    vk::Buffer getIndexBuffer(void) const {
        return m_meshData ? *m_meshData->m_indexBuffer : vk::Buffer{};
    }
    vk::DeviceMemory getIndexBufferMemory(void) const {
        return m_meshData ? *m_meshData->m_indexBufferMemory : vk::DeviceMemory{};
    }
    vk::DeviceSize getIndexBufferOffset(void) const {
        return m_meshData ? m_meshData->m_indexBufferOffset : 0;
    }
    uint32_t getIndexCount(void) const {
        return m_meshData ? m_meshData->m_indexCount : 0;
    }

    bool hasGeometry(void) const { return m_geometry.has_value(); }
    std::span<const SubMesh> getSubMeshes(void) const { return m_subMeshes; }
    glm::vec4 getBoundsSphere(void) const { return m_boundsSphere; }

    // Takes ownership of CPU geometry; load() uploads it once. The importer is
    // the normal producer. Throws std::invalid_argument on inconsistent ranges.
    void setGeometry(MeshGeometry geometry);

private:
    bool doLoad(void) override;
    void doUnload(void) override;

    void createVertexBuffer(const std::vector<Vertex>& vertices);
    void createIndexBuffer(const std::vector<uint32_t>& indices);
};

#endif // __O5MMESHRESOURCE_H
