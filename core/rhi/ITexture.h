#ifndef ITEXTURE_H
#define ITEXTURE_H

#include <memory>

struct TextureDesc {
    int width = 0;
    int height = 0;
    int depth = 1;

    enum E_TEXTURE_FORMAT {
        R8_UNORM,
        RGBA8_UNORM,
        RGBA16_SFLOAT,
        RGBA32_SFLOAT,
        D16_UNORM,
        D24_UNORM_S8_UINT,
        D32_SFLOAT,
    };

    E_TEXTURE_FORMAT format = RGBA8_UNORM;
    
    int miplevels = 1;
    int layers = 1;

    enum E_TEXTURE_FLAG : uint32_t{
        RENDERTARGET    = 1 << 0,
        CUBEMAP         = 1 << 1,
        TEXTUREARRAY    = 1 << 2,
        GENERATEMIPS    = 1 << 3,
    };

    uint32_t flags = 0;
};

class ITexture {
public:
    virtual ~ITexture() = default;

    virtual bool create(void) = 0;
    virtual void upload(const void* data, int miplevel = 0, int layer = 0) = 0; //what is this layer?

    virtual int width(void) const = 0;
    virtual int height(void) const = 0;
    virtual int depth(void) const = 0;
    
    virtual int miplevels(void) const = 0;
    virtual int layers(void) const = 0;
};

#endif //ITEXTURE_H
