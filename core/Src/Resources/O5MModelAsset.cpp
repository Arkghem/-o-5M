#include "Resources/O5MModelAsset.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Resources/O5MMaterialResource.h"
#include "Resources/O5MMeshResource.h"
#include "Resources/O5MResourceManager.h"
#include "Resources/O5MTextureResource.h"

namespace {

using Vertex = O5MMeshResource::Vertex;

float componentToFloat(const tinygltf::Accessor& accessor,
                       const unsigned char* source) {
    switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE: {
            int8_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            return accessor.normalized
                ? std::max(static_cast<float>(value) / 127.0f, -1.0f)
                : static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            uint8_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            return accessor.normalized
                ? static_cast<float>(value) / 255.0f
                : static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT: {
            int16_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            return accessor.normalized
                ? std::max(static_cast<float>(value) / 32767.0f, -1.0f)
                : static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            uint16_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            return accessor.normalized
                ? static_cast<float>(value) / 65535.0f
                : static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_INT: {
            int32_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            return accessor.normalized
                ? std::max(static_cast<float>(value) / 2147483647.0f, -1.0f)
                : static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
            uint32_t value = 0;
            std::memcpy(&value, source, sizeof(value));
            return accessor.normalized
                ? static_cast<float>(value) / 4294967295.0f
                : static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_DOUBLE: {
            double value = 0.0;
            std::memcpy(&value, source, sizeof(value));
            return static_cast<float>(value);
        }
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            float value = 0.0f;
            std::memcpy(&value, source, sizeof(value));
            return value;
        }
        default:
            return 0.0f;
    }
}

bool readAttribute(const tinygltf::Model& model,
                   const tinygltf::Primitive& primitive,
                   const char* name,
                   std::vector<float>& out,
                   int& components,
                   bool& present,
                   std::string& error) {
    out.clear();
    components = 0;
    present = false;

    const auto attribute = primitive.attributes.find(name);
    if (attribute == primitive.attributes.end()) {
        return true;
    }
    present = true;

    const int accessorIndex = attribute->second;
    if (accessorIndex < 0 ||
        static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
        error = std::string("attribute ") + name + " has invalid accessor";
        return false;
    }

    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    if (accessor.sparse.isSparse) {
        error = std::string("attribute ") + name + " uses an unsupported sparse accessor";
        return false;
    }
    if (accessor.bufferView < 0 ||
        static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size()) {
        error = std::string("attribute ") + name + " has no bufferView";
        return false;
    }

    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 ||
        static_cast<size_t>(view.buffer) >= model.buffers.size()) {
        error = std::string("attribute ") + name + " has an invalid buffer";
        return false;
    }
    const std::vector<unsigned char>& buffer = model.buffers[view.buffer].data;
    if (buffer.empty()) {
        error = std::string("attribute ") + name + " references an empty buffer";
        return false;
    }

    const int componentCount = tinygltf::GetNumComponentsInType(accessor.type);
    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    const int stride = accessor.ByteStride(view);
    if (componentCount <= 0 || componentSize <= 0 || stride <= 0 ||
        stride < componentCount * componentSize) {
        error = std::string("attribute ") + name + " has an invalid layout";
        return false;
    }
    if (accessor.count > 0 &&
        accessor.count > (std::numeric_limits<size_t>::max() /
                          static_cast<size_t>(componentCount))) {
        error = std::string("attribute ") + name + " is too large";
        return false;
    }

    const size_t baseOffset =
        view.byteOffset + accessor.byteOffset;
    if (baseOffset > buffer.size()) {
        error = std::string("attribute ") + name + " is out of bounds";
        return false;
    }
    if (accessor.count > 0) {
        const size_t lastOffset =
            baseOffset +
            (accessor.count - 1) * static_cast<size_t>(stride) +
            static_cast<size_t>(componentCount * componentSize);
        if (lastOffset > buffer.size()) {
            error = std::string("attribute ") + name + " is out of bounds";
            return false;
        }
    }

    components = componentCount;
    out.resize(accessor.count * static_cast<size_t>(componentCount));
    for (size_t i = 0; i < accessor.count; ++i) {
        const unsigned char* element =
            buffer.data() + baseOffset + i * static_cast<size_t>(stride);
        for (int c = 0; c < componentCount; ++c) {
            out[i * static_cast<size_t>(componentCount) + static_cast<size_t>(c)] =
                componentToFloat(accessor, element + c * componentSize);
        }
    }
    return true;
}

bool appendIndices(const tinygltf::Model& model,
                   const tinygltf::Primitive& primitive,
                   uint32_t vertexBase,
                   std::vector<uint32_t>& out,
                   std::string& error) {
    const int accessorIndex = primitive.indices;
    if (accessorIndex < 0 ||
        static_cast<size_t>(accessorIndex) >= model.accessors.size()) {
        error = "primitive has an invalid index accessor";
        return false;
    }

    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    if (accessor.sparse.isSparse) {
        error = "primitive uses an unsupported sparse index accessor";
        return false;
    }
    if (accessor.type != TINYGLTF_TYPE_SCALAR) {
        error = "primitive index accessor is not SCALAR";
        return false;
    }
    if (accessor.bufferView < 0 ||
        static_cast<size_t>(accessor.bufferView) >= model.bufferViews.size()) {
        error = "primitive index accessor has no bufferView";
        return false;
    }

    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    if (view.buffer < 0 ||
        static_cast<size_t>(view.buffer) >= model.buffers.size()) {
        error = "primitive index accessor has an invalid buffer";
        return false;
    }
    const std::vector<unsigned char>& buffer = model.buffers[view.buffer].data;
    if (buffer.empty()) {
        error = "primitive index accessor references an empty buffer";
        return false;
    }

    const int componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    if (componentSize <= 0 ||
        (accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
         accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
         accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)) {
        error = "primitive index accessor has an unsupported component type";
        return false;
    }

    const int stride = accessor.ByteStride(view);
    if (stride < componentSize) {
        error = "primitive index accessor has an invalid stride";
        return false;
    }

    const size_t baseOffset = view.byteOffset + accessor.byteOffset;
    if (baseOffset > buffer.size()) {
        error = "primitive index accessor is out of bounds";
        return false;
    }
    if (accessor.count > 0) {
        const size_t lastOffset =
            baseOffset +
            (accessor.count - 1) * static_cast<size_t>(stride) +
            static_cast<size_t>(componentSize);
        if (lastOffset > buffer.size()) {
            error = "primitive index accessor is out of bounds";
            return false;
        }
    }

    out.reserve(out.size() + accessor.count);
    for (size_t i = 0; i < accessor.count; ++i) {
        const unsigned char* source =
            buffer.data() + baseOffset + i * static_cast<size_t>(stride);
        uint32_t index = 0;
        switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                uint8_t value = 0;
                std::memcpy(&value, source, sizeof(value));
                index = value;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                uint16_t value = 0;
                std::memcpy(&value, source, sizeof(value));
                index = value;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                std::memcpy(&index, source, sizeof(index));
                break;
            }
            default:
                break;
        }
        if (index > std::numeric_limits<uint32_t>::max() - vertexBase) {
            error = "primitive index overflows the vertex offset";
            return false;
        }
        out.push_back(index + vertexBase);
    }
    return true;
}

glm::vec3 readVec3OrDefault(const std::vector<float>& data,
                            int components,
                            size_t index,
                            const glm::vec3& fallback) {
    if (components < 3 || data.size() < (index + 1) * static_cast<size_t>(components)) {
        return fallback;
    }
    const float* value = &data[index * static_cast<size_t>(components)];
    return glm::vec3(value[0], value[1], value[2]);
}

glm::vec2 readVec2OrDefault(const std::vector<float>& data,
                            int components,
                            size_t index,
                            const glm::vec2& fallback) {
    if (components < 2 || data.size() < (index + 1) * static_cast<size_t>(components)) {
        return fallback;
    }
    const float* value = &data[index * static_cast<size_t>(components)];
    return glm::vec2(value[0], value[1]);
}

glm::vec4 computeBoundsSphere(const std::vector<Vertex>& vertices) {
    if (vertices.empty()) {
        return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    glm::vec3 lower(std::numeric_limits<float>::max());
    glm::vec3 upper(std::numeric_limits<float>::lowest());
    for (const Vertex& vertex : vertices) {
        lower = glm::min(lower, vertex.m_pos);
        upper = glm::max(upper, vertex.m_pos);
    }

    const glm::vec3 center = (lower + upper) * 0.5f;
    const float radius = std::max(glm::length(upper - center), 1e-4f);
    return glm::vec4(center, radius);
}

glm::vec4 vec4Or(const std::vector<double>& values, const glm::vec4& fallback) {
    glm::vec4 result = fallback;
    for (size_t i = 0; i < 4 && i < values.size(); ++i) {
        result[static_cast<int>(i)] = static_cast<float>(values[i]);
    }
    return result;
}

uint32_t alphaModeCode(const std::string& mode, const std::string& materialName) {
    if (mode == "MASK") {
        std::cerr << "[gltf] material '" << materialName
                  << "': alphaMode MASK imported as flags (blending not implemented)\n";
        return 1;
    }
    if (mode == "BLEND") {
        std::cerr << "[gltf] material '" << materialName
                  << "': alphaMode BLEND imported as flags (blending not implemented)\n";
        return 2;
    }
    if (mode != "OPAQUE") {
        std::cerr << "[gltf] material '" << materialName
                  << "': unknown alphaMode '" << mode << "', using OPAQUE\n";
    }
    return 0;
}

glm::mat4 nodeLocalTransform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        glm::mat4 result(1.0f);
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                result[column][row] =
                    static_cast<float>(node.matrix[static_cast<size_t>(column * 4 + row)]);
            }
        }
        return result;
    }

    glm::vec3 translation(0.0f);
    if (node.translation.size() == 3) {
        translation = glm::vec3(static_cast<float>(node.translation[0]),
                                static_cast<float>(node.translation[1]),
                                static_cast<float>(node.translation[2]));
    }

    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    if (node.rotation.size() == 4) {
        // glTF stores quaternions as [x, y, z, w]; glm::quat takes (w, x, y, z).
        rotation = glm::quat(static_cast<float>(node.rotation[3]),
                             static_cast<float>(node.rotation[0]),
                             static_cast<float>(node.rotation[1]),
                             static_cast<float>(node.rotation[2]));
    }

    glm::vec3 scale(1.0f);
    if (node.scale.size() == 3) {
        scale = glm::vec3(static_cast<float>(node.scale[0]),
                          static_cast<float>(node.scale[1]),
                          static_cast<float>(node.scale[2]));
    }

    return glm::translate(glm::mat4(1.0f), translation) *
           glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

} // namespace

O5MModelAsset::~O5MModelAsset() {
    release();
}

O5MModelAsset::O5MModelAsset(O5MModelAsset&& other) noexcept :
    m_parts(std::move(other.m_parts)),
    m_meshes(std::move(other.m_meshes)),
    m_materials(std::move(other.m_materials)),
    m_textures(std::move(other.m_textures)),
    m_defaultMaterial(other.m_defaultMaterial),
    m_ownedIds(std::move(other.m_ownedIds)) {
    other.m_parts.clear();
    other.m_meshes.clear();
    other.m_materials.clear();
    other.m_textures.clear();
    other.m_defaultMaterial = {};
    other.m_ownedIds.clear();
}

O5MModelAsset& O5MModelAsset::operator=(O5MModelAsset&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    release();

    m_parts = std::move(other.m_parts);
    m_meshes = std::move(other.m_meshes);
    m_materials = std::move(other.m_materials);
    m_textures = std::move(other.m_textures);
    m_defaultMaterial = other.m_defaultMaterial;
    m_ownedIds = std::move(other.m_ownedIds);

    other.m_parts.clear();
    other.m_meshes.clear();
    other.m_materials.clear();
    other.m_textures.clear();
    other.m_defaultMaterial = {};
    other.m_ownedIds.clear();
    return *this;
}

void O5MModelAsset::release() {
    O5MResourceManager& manager = O5MResourceManager::getInstance();
    for (auto it = m_ownedIds.rbegin(); it != m_ownedIds.rend(); ++it) {
        manager.release(*it);
    }
    m_ownedIds.clear();
    m_parts.clear();
    m_meshes.clear();
    m_materials.clear();
    m_textures.clear();
    m_defaultMaterial = {};
}

O5MModelAsset O5MModelAsset::importGLB(O5MDevice& device,
                                       std::string name,
                                       std::span<const uint8_t> bytes,
                                       std::string baseDir) {
    if (bytes.empty()) {
        throw std::runtime_error("O5MModelAsset::importGLB: empty GLB (" + name + ")");
    }
    if (bytes.size() > std::numeric_limits<unsigned int>::max()) {
        throw std::runtime_error("O5MModelAsset::importGLB: GLB is too large (" + name + ")");
    }

    tinygltf::TinyGLTF loader;
    // Keep the encoded PNG/JPEG bytes: O5MTextureResource is the single decoder.
    loader.SetImagesAsIs(true);

    tinygltf::Model model;
    std::string error;
    std::string warning;
    const bool loaded = loader.LoadBinaryFromMemory(
        &model, &error, &warning,
        reinterpret_cast<const unsigned char*>(bytes.data()),
        static_cast<unsigned int>(bytes.size()),
        baseDir);
    if (!warning.empty()) {
        std::cerr << "[gltf] " << name << ": " << warning << "\n";
    }
    if (!loaded) {
        throw std::runtime_error(
            "O5MModelAsset::importGLB: parse failed (" + name + "): " + error);
    }

    O5MModelAsset asset;
    O5MResourceManager& manager = O5MResourceManager::getInstance();

    auto track = [&asset](const auto& handle) {
        asset.m_ownedIds.push_back(handle->getResourceId());
    };

    try {
        std::unordered_map<uint64_t, O5MResourceHandle<O5MTextureResource>>
            textureCache;

        auto makeTexture = [&](int textureIndex,
                               O5MTextureColorSpace colorSpace,
                               const std::string& label)
            -> O5MResourceHandle<O5MTextureResource> {
            if (textureIndex < 0) {
                return {};
            }
            if (static_cast<size_t>(textureIndex) >= model.textures.size()) {
                throw std::runtime_error(
                    name + ": material texture index out of range");
            }

            const tinygltf::Texture& texture = model.textures[textureIndex];
            if (texture.source < 0 ||
                static_cast<size_t>(texture.source) >= model.images.size()) {
                throw std::runtime_error(
                    name + ": texture '" + label + "' has no valid image source");
            }

            const tinygltf::Image& image = model.images[texture.source];
            if (image.image.empty()) {
                throw std::runtime_error(
                    name + ": image for texture '" + label + "' is empty");
            }

            const uint64_t key =
                (static_cast<uint64_t>(static_cast<uint32_t>(texture.source)) << 1) |
                (colorSpace == O5MTextureColorSpace::Srgb ? 0ull : 1ull);
            const auto cached = textureCache.find(key);
            if (cached != textureCache.end()) {
                return cached->second;
            }

            auto handle = manager.create<O5MTextureResource>(
                name + "/" + label, device, colorSpace);
            track(handle);
            handle->setData(std::vector<uint8_t>(image.image.begin(), image.image.end()));
            if (!handle->load()) {
                throw std::runtime_error(
                    name + ": failed to upload texture '" + label + "'");
            }

            textureCache.emplace(key, handle);
            asset.m_textures.push_back(handle);
            return handle;
        };

        std::vector<O5MResourceHandle<O5MMaterialResource>> materialHandles;
        materialHandles.reserve(model.materials.size());

        for (size_t materialIndex = 0;
             materialIndex < model.materials.size();
             ++materialIndex) {
            const tinygltf::Material& material = model.materials[materialIndex];
            const std::string materialName = material.name.empty()
                ? name + "/material_" + std::to_string(materialIndex)
                : name + "/" + material.name;

            O5MMaterialResource::MaterialParamsGLSL params{};
            params.albedoFactor = vec4Or(
                material.pbrMetallicRoughness.baseColorFactor,
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
            params.mrFactor = glm::vec4(
                static_cast<float>(material.pbrMetallicRoughness.metallicFactor),
                static_cast<float>(material.pbrMetallicRoughness.roughnessFactor),
                0.0f, 0.0f);

            const glm::vec4 emissive = vec4Or(
                material.emissiveFactor, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            params.emissiveFactor =
                glm::vec4(emissive.x, emissive.y, emissive.z, 1.0f);

            uint32_t flags = 0;
            if (material.doubleSided) {
                flags |= O5MMaterialResource::kFlagDoubleSided;
            }
            flags |= alphaModeCode(material.alphaMode, materialName)
                     << O5MMaterialResource::kAlphaModeShift;
            params.flags = glm::vec4(
                static_cast<float>(flags),
                static_cast<float>(material.alphaCutoff),
                static_cast<float>(material.normalTexture.scale),
                static_cast<float>(material.occlusionTexture.strength));

            O5MMaterialResource::TextureSlots slots{};
            slots.baseColor = makeTexture(
                material.pbrMetallicRoughness.baseColorTexture.index,
                O5MTextureColorSpace::Srgb, "baseColor");
            slots.metallicRoughness = makeTexture(
                material.pbrMetallicRoughness.metallicRoughnessTexture.index,
                O5MTextureColorSpace::Linear, "metallicRoughness");
            slots.normal = makeTexture(
                material.normalTexture.index,
                O5MTextureColorSpace::Linear, "normal");
            slots.occlusion = makeTexture(
                material.occlusionTexture.index,
                O5MTextureColorSpace::Linear, "occlusion");
            slots.emissive = makeTexture(
                material.emissiveTexture.index,
                O5MTextureColorSpace::Srgb, "emissive");

            auto handle = manager.create<O5MMaterialResource>(
                materialName, params, slots);
            track(handle);
            if (!handle->load()) {
                throw std::runtime_error(
                    name + ": invalid material parameters in '" + materialName + "'");
            }

            materialHandles.push_back(handle);
            asset.m_materials.push_back(handle);
        }

        bool needsDefaultMaterial = false;
        for (const tinygltf::Mesh& mesh : model.meshes) {
            for (const tinygltf::Primitive& primitive : mesh.primitives) {
                if (primitive.material < 0 ||
                    static_cast<size_t>(primitive.material) >= materialHandles.size()) {
                    needsDefaultMaterial = true;
                }
            }
        }

        if (needsDefaultMaterial) {
            O5MMaterialResource::MaterialParamsGLSL params{};
            params.albedoFactor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            params.mrFactor = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
            params.emissiveFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            params.flags = glm::vec4(0.0f, 0.5f, 1.0f, 1.0f);

            auto handle = manager.create<O5MMaterialResource>(
                name + "/default-material", params);
            track(handle);
            if (!handle->load()) {
                throw std::runtime_error(
                    name + ": invalid default material parameters");
            }

            asset.m_defaultMaterial = handle;
            asset.m_materials.push_back(handle);
        }

        asset.m_meshes.reserve(model.meshes.size());

        for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
            const tinygltf::Mesh& mesh = model.meshes[meshIndex];
            O5MMeshResource::MeshGeometry geometry;

            for (size_t primitiveIndex = 0;
                 primitiveIndex < mesh.primitives.size();
                 ++primitiveIndex) {
                const tinygltf::Primitive& primitive = mesh.primitives[primitiveIndex];
                if (primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES) {
                    std::cerr << "[gltf] " << name << ": mesh " << meshIndex
                              << " primitive " << primitiveIndex
                              << " is not TRIANGLES, skipping\n";
                    continue;
                }

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
                bool positionPresent = false;
                bool normalPresent = false;
                bool tangentPresent = false;
                bool colorPresent = false;
                bool texCoordPresent = false;
                std::string attributeError;

                const auto readOptional = [&](const char* attribute,
                                              std::vector<float>& out,
                                              int& components,
                                              bool& present) {
                    return readAttribute(model, primitive, attribute, out,
                                         components, present, attributeError);
                };

                if (!readOptional("POSITION", positions, positionComponents,
                                  positionPresent) ||
                    !readOptional("NORMAL", normals, normalComponents,
                                  normalPresent) ||
                    !readOptional("TANGENT", tangents, tangentComponents,
                                  tangentPresent) ||
                    !readOptional("COLOR_0", colors, colorComponents,
                                  colorPresent) ||
                    !readOptional("TEXCOORD_0", texCoords, texCoordComponents,
                                  texCoordPresent)) {
                    throw std::runtime_error(
                        name + ": mesh " + std::to_string(meshIndex) +
                        " primitive " + std::to_string(primitiveIndex) +
                        ": " + attributeError);
                }

                if (!positionPresent || positionComponents < 3) {
                    std::cerr << "[gltf] " << name << ": mesh " << meshIndex
                              << " primitive " << primitiveIndex
                              << " has no POSITION, skipping\n";
                    continue;
                }

                const size_t vertexCount =
                    positions.size() / static_cast<size_t>(positionComponents);
                if (vertexCount == 0) {
                    continue;
                }
                if (vertexCount > std::numeric_limits<uint32_t>::max() ||
                    geometry.vertices.size() >
                        std::numeric_limits<uint32_t>::max() - vertexCount) {
                    throw std::runtime_error(
                        name + ": mesh " + std::to_string(meshIndex) +
                        " has too many vertices");
                }

                const uint32_t vertexBase =
                    static_cast<uint32_t>(geometry.vertices.size());
                for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                    Vertex vertex{};
                    vertex.m_pos = readVec3OrDefault(
                        positions, positionComponents, vertexIndex, glm::vec3(0.0f));
                    const glm::vec3 normal = readVec3OrDefault(
                        normals, normalComponents, vertexIndex, glm::vec3(0.0f));
                    vertex.m_norm = glm::vec4(normal, 0.0f);
                    vertex.m_tangent = readVec3OrDefault(
                        tangents, tangentComponents, vertexIndex, glm::vec3(0.0f));
                    vertex.m_color = readVec3OrDefault(
                        colors, colorComponents, vertexIndex, glm::vec3(1.0f));
                    vertex.m_texCoord = readVec2OrDefault(
                        texCoords, texCoordComponents, vertexIndex, glm::vec2(0.0f));
                    geometry.vertices.push_back(vertex);
                }

                const size_t firstIndex = geometry.indices.size();
                if (primitive.indices >= 0) {
                    std::string indexError;
                    if (!appendIndices(model, primitive, vertexBase,
                                       geometry.indices, indexError)) {
                        throw std::runtime_error(
                            name + ": mesh " + std::to_string(meshIndex) +
                            " primitive " + std::to_string(primitiveIndex) +
                            ": " + indexError);
                    }
                } else {
                    geometry.indices.reserve(geometry.indices.size() + vertexCount);
                    for (size_t i = 0; i < vertexCount; ++i) {
                        geometry.indices.push_back(
                            vertexBase + static_cast<uint32_t>(i));
                    }
                }

                const size_t indexCount = geometry.indices.size() - firstIndex;
                if (indexCount == 0) {
                    continue;
                }
                if (geometry.indices.size() > std::numeric_limits<uint32_t>::max()) {
                    throw std::runtime_error(
                        name + ": mesh " + std::to_string(meshIndex) +
                        " has too many indices");
                }

                O5MResourceHandle<O5MMaterialResource> material;
                if (primitive.material >= 0 &&
                    static_cast<size_t>(primitive.material) < materialHandles.size()) {
                    material = materialHandles[primitive.material];
                } else {
                    material = asset.m_defaultMaterial;
                }
                if (material.get() == nullptr) {
                    throw std::runtime_error(
                        name + ": mesh " + std::to_string(meshIndex) +
                        " primitive " + std::to_string(primitiveIndex) +
                        " has no usable material");
                }

                geometry.subMeshes.push_back({
                    static_cast<uint32_t>(firstIndex),
                    static_cast<uint32_t>(indexCount),
                    material
                });
            }

            if (geometry.vertices.empty() || geometry.subMeshes.empty()) {
                asset.m_meshes.emplace_back();
                continue;
            }

            geometry.boundsSphere = computeBoundsSphere(geometry.vertices);

            const std::string meshName = mesh.name.empty()
                ? name + "/mesh_" + std::to_string(meshIndex)
                : name + "/" + mesh.name;
            auto handle = manager.create<O5MMeshResource>(meshName, device);
            track(handle);
            handle->setGeometry(std::move(geometry));
            if (!handle->load()) {
                throw std::runtime_error(
                    name + ": failed to upload mesh '" + meshName + "'");
            }
            asset.m_meshes.push_back(handle);
        }

        if (!model.scenes.empty()) {
            int sceneIndex = model.defaultScene;
            if (sceneIndex < 0 ||
                static_cast<size_t>(sceneIndex) >= model.scenes.size()) {
                sceneIndex = 0;
            }

            std::unordered_set<int> visiting;
            std::function<void(int, const glm::mat4&)> visitNode =
                [&](int nodeIndex, const glm::mat4& parentTransform) {
                    if (nodeIndex < 0 ||
                        static_cast<size_t>(nodeIndex) >= model.nodes.size()) {
                        throw std::runtime_error(
                            name + ": scene references an invalid node");
                    }
                    if (!visiting.insert(nodeIndex).second) {
                        throw std::runtime_error(
                            name + ": node hierarchy contains a cycle");
                    }

                    const tinygltf::Node& node = model.nodes[nodeIndex];
                    if (node.skin >= 0) {
                        std::cerr << "[gltf] " << name << ": node " << nodeIndex
                                  << " uses a skin; skinning is not supported\n";
                    }

                    const glm::mat4 worldTransform =
                        parentTransform * nodeLocalTransform(node);
                    if (node.mesh >= 0 &&
                        static_cast<size_t>(node.mesh) < asset.m_meshes.size() &&
                        asset.m_meshes[node.mesh].get() != nullptr) {
                        asset.m_parts.push_back({
                            asset.m_meshes[node.mesh],
                            worldTransform
                        });
                    }

                    for (int child : node.children) {
                        visitNode(child, worldTransform);
                    }
                    visiting.erase(nodeIndex);
                };

            for (int root : model.scenes[sceneIndex].nodes) {
                visitNode(root, glm::mat4(1.0f));
            }
        }

        return asset;
    } catch (...) {
        asset.release();
        throw;
    }
}
