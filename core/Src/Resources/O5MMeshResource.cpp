#include "Resources/O5MMeshResource.h"

#include <algorithm>
#include <cstring>
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


void readAttribute(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    const char* name,
    std::vector<float>& out,
    int& numComponets
) {
    out.clear();
    numComponets = 0;

    const auto attribute = primitive.attributes.find(name);
    if (attribute == primitive.attributes.end())
        return;

    const tinygltf::Accessor& accessor = model.accessors[attribute->second];
    if (accessor.bufferView < 0)
        return;

    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const std::vector<unsigned char>& buffer = model.buffers[view.buffer].data;
    if (buffer.empty())
        return;

    const int componentCount = tinygltf::GetNumComponentsInType(accessor.type);
    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    const int stride = accessor.ByteStride(view);

    if (componentCount <= 0 || componentSize <= 0 || stride <= 0)
        return;

    const unsigned char* base = buffer.data() + view.byteOffset + accessor.byteOffset;

    const auto toFloat = [&accessor](const unsigned char* src) -> float {
        switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_BYTE: {
                int8_t value = 0;
                memcpy(&value, src, sizeof(value));
                return accessor.normalized ? std::max(value / 127.0f, -1.0f) : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                uint8_t value = 0;
                memcpy(&value, src, sizeof(value));
                return accessor.normalized ? value / 255.0f : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_SHORT: {
                int16_t value = 0;
                memcpy(&value, src, sizeof(value));
                return accessor.normalized ? std::max(value / 32767.0f, -1.0f) : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                uint16_t value = 0;
                memcpy(&value, src, sizeof(value));
                return accessor.normalized ? value / 65535.0f : static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_INT: {
                int32_t value = 0;
                memcpy(&value, src, sizeof(value));
                return static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                uint32_t value = 0;
                memcpy(&value, src, sizeof(value));
                return static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_DOUBLE: {
                double value = 0.0;
                memcpy(&value, src, sizeof(value));
                return static_cast<float>(value);
            }
            case TINYGLTF_COMPONENT_TYPE_FLOAT: {
                float value = 0.0f;
                memcpy(&value, src, sizeof(value));
                return value;
            }
            default:
                return 0.0f;
        }
    };

    numComponets = componentCount;
    out.resize(accessor.count * static_cast<size_t>(componentCount));

    for (size_t i = 0; i < accessor.count; i++) {
        const unsigned char* element = base + i * static_cast<size_t>(stride);
        for (int c = 0; c < componentCount; c++) {
            out[i * static_cast<size_t>(componentCount) + static_cast<size_t>(c)] =
                toFloat(element + c * static_cast<size_t>(componentSize));
        }
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
            std::vector<float> positions;
            std::vector<float> normals;
            std::vector<float> tangents;
            std::vector<float> colors;
            std::vector<float> texCoords;

            int positionComponents = 0;
            int normalComponents = 0;
            int tangentComponents = 0;
            int colorComponents = 0;
            int texCoordComponents = 0;

            readAttribute(model, primitive, "POSITION", positions, positionComponents);
            readAttribute(model, primitive, "NORMAL", normals, normalComponents);
            readAttribute(model, primitive, "TANGENT", tangents, tangentComponents);
            readAttribute(model, primitive, "COLOR_0", colors, colorComponents);
            readAttribute(model, primitive, "TEXCOORD_0", texCoords, texCoordComponents);

            if (positionComponents < 3)
                continue;

            const size_t vertexCount = positions.size() / static_cast<size_t>(positionComponents);

            for (size_t v = 0; v < vertexCount; v++) {
                Vertex vertex{};
                vertex.m_pos = glm::make_vec3(&positions[v * positionComponents]);
                vertex.m_norm = normalComponents < 3 ? glm::vec4(0.0f) : glm::vec4(glm::make_vec3(&normals[v * normalComponents]), 0.0f);
                vertex.m_tangent = tangentComponents < 3 ? glm::vec3(0.0f) : glm::make_vec3(&tangents[v * tangentComponents]);
                vertex.m_color = colorComponents < 3 ? glm::vec3(1.0f) : glm::make_vec3(&colors[v * colorComponents]);
                vertex.m_texCoord = texCoordComponents < 2 ? glm::vec2(0.0f) : glm::make_vec2(&texCoords[v * texCoordComponents]);

                vertices.push_back(vertex);
            }

            if (primitive.indices >= 0) {
                const tinygltf::Accessor& accessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
                const uint32_t* bufferIndices = reinterpret_cast<const uint32_t*>(&(model.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));

                int indicesByteStride = 0;
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

                memcpy(indices.data(), bufferIndices, model.accessors[primitive.indices].count * indicesByteStride);
            }
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
