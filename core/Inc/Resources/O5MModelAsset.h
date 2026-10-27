#ifndef O5M_MODEL_ASSET_H
#define O5M_MODEL_ASSET_H

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Resources/O5MResource.h"

class O5MDevice;
class O5MMeshResource;
class O5MMaterialResource;
class O5MTextureResource;

// One flattened glTF node that references a renderable mesh.
struct O5MModelPart {
    O5MResourceHandle<O5MMeshResource> mesh;
    glm::mat4 worldTransform{1.0f};
};

// Import result and ownership root. It is intentionally NOT registered in
// O5MResourceManager: the resources it creates are registered and independently
// addressable, while the asset only groups them, records the file's default
// mesh/material pairing, and releases them in reverse dependency order.
class O5MModelAsset final {
public:
    O5MModelAsset() = default;
    ~O5MModelAsset();

    O5MModelAsset(const O5MModelAsset&) = delete;
    O5MModelAsset& operator=(const O5MModelAsset&) = delete;
    O5MModelAsset(O5MModelAsset&& other) noexcept;
    O5MModelAsset& operator=(O5MModelAsset&& other) noexcept;

    // Parses one GLB. `baseDir` is used to resolve external image URIs; pass
    // the directory containing the GLB. Throws std::runtime_error on failure;
    // every resource created before the failure is released first.
    static O5MModelAsset importGLB(O5MDevice& device,
                                   std::string name,
                                   std::span<const uint8_t> bytes,
                                   std::string baseDir = {});

    // Idempotent. Releases mesh resources first, then materials, then textures.
    void release();

    bool isValid(void) const { return !m_ownedIds.empty(); }

    std::span<const O5MModelPart> parts(void) const { return m_parts; }
    std::span<const O5MResourceHandle<O5MMeshResource>> meshes(void) const {
        return m_meshes;
    }
    std::span<const O5MResourceHandle<O5MMaterialResource>> materials(void) const {
        return m_materials;
    }
    std::span<const O5MResourceHandle<O5MTextureResource>> textures(void) const {
        return m_textures;
    }
    O5MResourceHandle<O5MMaterialResource> defaultMaterial(void) const {
        return m_defaultMaterial;
    }

private:
    std::vector<O5MModelPart> m_parts;
    std::vector<O5MResourceHandle<O5MMeshResource>> m_meshes;
    std::vector<O5MResourceHandle<O5MMaterialResource>> m_materials;
    std::vector<O5MResourceHandle<O5MTextureResource>> m_textures;
    O5MResourceHandle<O5MMaterialResource> m_defaultMaterial;

    // Creation order. release() walks this backwards so meshes die before the
    // materials/textures they reference.
    std::vector<uint64_t> m_ownedIds;
};

#endif // O5M_MODEL_ASSET_H
