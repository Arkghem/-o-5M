#ifndef O5M_MATERIAL_RESOURCE_H
#define O5M_MATERIAL_RESOURCE_H

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "Resources/O5MResource.h"
#include "Resources/O5MTextureResource.h"

// Pure CPU-side material data. It deliberately knows nothing about glTF or the
// file that produced it: the importer translates file data into this structure,
// and runtime code can create/share/override materials through handles alone.
class O5MMaterialResource : public O5MResource {
public:
    struct MaterialParamsGLSL {   // std140, 4 x vec4 - keep in sync with shaders
        glm::vec4 albedoFactor{1.0f, 1.0f, 1.0f, 1.0f}; // rgb + alpha
        glm::vec4 mrFactor{1.0f, 1.0f, 0.0f, 0.0f};     // x metallic, y roughness
        glm::vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 1.0f}; // rgb + emissive strength
        glm::vec4 flags{0.0f, 0.5f, 1.0f, 1.0f};
        // flags.x: bit0 doubleSided; bits1-2 alphaMode (0=opaque, 1=mask, 2=blend)
        // flags.y: alphaCutoff; flags.z: normalScale; flags.w: occlusionStrength
    };

    struct TextureSlots {         // missing texture -> default (invalid) handle
        O5MResourceHandle<O5MTextureResource> baseColor;
        O5MResourceHandle<O5MTextureResource> metallicRoughness;
        O5MResourceHandle<O5MTextureResource> normal;
        O5MResourceHandle<O5MTextureResource> occlusion;
        O5MResourceHandle<O5MTextureResource> emissive;
    };

    static constexpr uint32_t kFlagDoubleSided = 1u << 0;
    static constexpr uint32_t kAlphaModeShift = 1;
    static constexpr uint32_t kAlphaModeMask = 0x3u << kAlphaModeShift;

public:
    O5MMaterialResource(const std::string& name,
                        const MaterialParamsGLSL& params,
                        const TextureSlots& textures) :
        O5MResource(name), m_params(params), m_textureSlots(textures) {}

    O5MMaterialResource(const std::string& name,
                        const MaterialParamsGLSL& params) :
        O5MMaterialResource(name, params, TextureSlots{}) {}

    ~O5MMaterialResource() override { unload(); }

    const MaterialParamsGLSL& getMaterialParams(void) const { return m_params; }
    void setMaterialParams(const MaterialParamsGLSL& params) { m_params = params; }

    const TextureSlots& getTextures(void) const { return m_textureSlots; }

    bool doLoad(void) override;
    void doUnload(void) override;

private:
    MaterialParamsGLSL m_params;
    TextureSlots m_textureSlots;
};

#endif // !O5M_MATERIAL_RESOURCE_H
