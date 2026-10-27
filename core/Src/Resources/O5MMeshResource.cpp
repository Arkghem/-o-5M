#include "Resources/O5MMeshResource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace {

glm::vec4 computeBoundsSphere(const std::vector<O5MMeshResource::Vertex>& vertices) {
    if (vertices.empty()) {
        return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    glm::vec3 lower(std::numeric_limits<float>::max());
    glm::vec3 upper(std::numeric_limits<float>::lowest());
    for (const auto& vertex : vertices) {
        lower = glm::min(lower, vertex.m_pos);
        upper = glm::max(upper, vertex.m_pos);
    }

    const glm::vec3 center = (lower + upper) * 0.5f;
    const float radius = std::max(glm::length(upper - center), 1e-4f);
    return glm::vec4(center, radius);
}

} // namespace

void O5MMeshResource::setGeometry(MeshGeometry geometry) {
    if (isloaded()) {
        unload();
    }

    if (geometry.vertices.size() > std::numeric_limits<uint32_t>::max() ||
        geometry.indices.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument(
            "O5MMeshResource::setGeometry: vertex/index count exceeds uint32");
    }

    if (geometry.subMeshes.empty() && !geometry.indices.empty()) {
        geometry.subMeshes.push_back({
            0,
            static_cast<uint32_t>(geometry.indices.size()),
            {}
        });
    }

    for (const SubMesh& subMesh : geometry.subMeshes) {
        const uint64_t end =
            static_cast<uint64_t>(subMesh.firstIndex) + subMesh.indexCount;
        if (end > geometry.indices.size()) {
            throw std::invalid_argument(
                "O5MMeshResource::setGeometry: submesh range exceeds index buffer");
        }
    }

    if (geometry.boundsSphere.w <= 0.0f && !geometry.vertices.empty()) {
        geometry.boundsSphere = computeBoundsSphere(geometry.vertices);
    }

    m_subMeshes = geometry.subMeshes;
    m_boundsSphere = geometry.boundsSphere;
    m_geometry = std::move(geometry);
}

bool O5MMeshResource::doLoad(void) {
    if (!m_geometry.has_value()) {
        return false;
    }

    MeshGeometry& geometry = *m_geometry;
    if (geometry.vertices.empty() || geometry.indices.empty() ||
        geometry.vertices.size() > std::numeric_limits<uint32_t>::max() ||
        geometry.indices.size() > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    m_meshData = std::make_unique<MeshData>();
    createVertexBuffer(geometry.vertices);
    createIndexBuffer(geometry.indices);

    m_meshData->m_vertexCount = static_cast<uint32_t>(geometry.vertices.size());
    m_meshData->m_indexCount = static_cast<uint32_t>(geometry.indices.size());

    // The GPU owns the data now; keep only the draw ranges and bounds.
    m_geometry.reset();
    return true;
}

void O5MMeshResource::doUnload(void) {
    m_meshData.reset();
}

void O5MMeshResource::createVertexBuffer(const std::vector<Vertex>& vertices) {
    const vk::DeviceSize bufferSize = vertices.size() * sizeof(Vertex);

    auto [stagingBuffer, stagingMemory] =
        m_device.createBuffer(bufferSize,
                              vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = stagingMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, vertices.data(), bufferSize);
    stagingMemory.unmapMemory();

    std::tie(m_meshData->m_vertexBuffer, m_meshData->m_vertexBufferMemory) =
        m_device.createBuffer(bufferSize,
                              vk::BufferUsageFlagBits::eVertexBuffer |
                                  vk::BufferUsageFlagBits::eTransferDst |
                                  vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eDeviceLocal);

    m_device.copyBuffer(stagingBuffer, m_meshData->m_vertexBuffer, bufferSize);
}

void O5MMeshResource::createIndexBuffer(const std::vector<uint32_t>& indices) {
    const vk::DeviceSize bufferSize = indices.size() * sizeof(uint32_t);

    auto [stagingBuffer, stagingMemory] =
        m_device.createBuffer(bufferSize,
                              vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eHostVisible |
                                  vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = stagingMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, indices.data(), bufferSize);
    stagingMemory.unmapMemory();

    std::tie(m_meshData->m_indexBuffer, m_meshData->m_indexBufferMemory) =
        m_device.createBuffer(bufferSize,
                              vk::BufferUsageFlagBits::eIndexBuffer |
                                  vk::BufferUsageFlagBits::eTransferDst |
                                  vk::BufferUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eDeviceLocal);

    m_device.copyBuffer(stagingBuffer, m_meshData->m_indexBuffer, bufferSize);
}
