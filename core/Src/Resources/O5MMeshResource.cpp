#include "Resources/O5MMeshResource.h"

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

bool O5MMeshResource::doLoad(void) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    if (!loadMeshData(vertices, indices)) {
        return false;
    }

    m_meshData= std::make_unique<MeshData>();

    createVertexBuffer(vertices);
    createIndexBuffer(indices);

    m_meshData->m_vertexCount = static_cast<uint32_t>(vertices.size());
    m_meshData->m_indexCount = static_cast<uint32_t>(indices.size());

    return true;
}

void O5MMeshResource::doUnload(void) {
    if (isloaded()) {
        m_meshData.reset();
    }
}

bool O5MMeshResource::loadMeshData(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool result = loader.LoadBinaryFromMemory(&model, &err, &warn, reinterpret_cast<const unsigned char*>(getData()),getSize());

    if (!warn.empty()) {
        printf("Warn: %s\n", warn.c_str());
    }

    if (!err.empty()) {
        printf("Err: %s\n", err.c_str());
    }

    if (!result)
        return false;

    vertices.clear();
    indices.clear();

    for (const auto& mesh : model.meshes) {
        for (const auto& primitive : mesh.primitives) {
            const float* bufferPos = nullptr;
            const uint32_t* bufferIndices = nullptr;
            const float* bufferTexCoordSet0 = nullptr;

            int vertexStride = 0;
            int indicesByteStride = 0;

            // position
            if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
                const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("POSITION")->second];
                const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                bufferPos = reinterpret_cast<float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
                vertexStride = accessor.ByteStride(view) ? accessor.ByteStride(view) / sizeof(float) : 3;
            }
       
            // index
            if (primitive.indices >= 0) {
                const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                bufferIndices = reinterpret_cast<uint32_t*>(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]);
                indices.resize(accessor.count);
                switch (accessor.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: 
                        indicesByteStride = sizeof(uint32_t);
                        break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: 
                        throw std::runtime_error("this  component type is not supported.");
                        break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: 
                        throw std::runtime_error("this  component type is not supported.");
                        break;
                    default:
                        break;
                }
            }

            // texCoord
            if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                const tinygltf::Accessor& accessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
                const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                bufferTexCoordSet0 = reinterpret_cast<const float*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
            }

            // vertices
            for (size_t v = 0; v < model.accessors[primitive.attributes.find("POSITION")->second].count; v++) {
                Vertex vertex{};
                vertex.m_pos = glm::make_vec3(&bufferPos[v * vertexStride]);
                vertex.m_color = glm::vec3(1.0f, 1.0f, 1.0f);
                vertex.m_texCoord = bufferTexCoordSet0 ? glm::make_vec2(&bufferTexCoordSet0[v * 2]) : glm::vec2(0.0f); vertices.push_back(vertex);
            }

            memcpy(indices.data(), bufferIndices, model.accessors[primitive.indices].count * indicesByteStride);
        }
    }
    return true; 
}

void O5MMeshResource::createVertexBuffer(std::vector<Vertex>& vertices) {
    vk::DeviceSize bufferSize = vertices.size() * sizeof(Vertex);

    auto [stagingBuffer, stagingMemory] = 
        m_device.createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = stagingMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, vertices.data(), bufferSize);
    stagingMemory.unmapMemory();

    std::tie(m_meshData->m_vertexBuffer, m_meshData->m_vertexBufferMemory) = 
        m_device.createBuffer(bufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

    m_device.copyBuffer(stagingBuffer, m_meshData->m_vertexBuffer, bufferSize);
}

void O5MMeshResource::createIndexBuffer(std::vector<uint32_t>& indices) {
    vk::DeviceSize bufferSize = indices.size() * sizeof(uint32_t);
    
    auto [stagingBuffer, stagingMemory] = 
        m_device.createBuffer(bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    void* dataStaging = stagingMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, indices.data(), bufferSize);
    stagingMemory.unmapMemory();

    std::tie(m_meshData->m_indexBuffer, m_meshData->m_indexBufferMemory) = 
        m_device.createBuffer(bufferSize, vk::BufferUsageFlagBits::eIndexBuffer |  vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

    m_device.copyBuffer(stagingBuffer, m_meshData->m_indexBuffer, bufferSize);
}
