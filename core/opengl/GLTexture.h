#ifndef __GLTEXTURE_H__
#define __GLTEXTURE_H__

#include <glad/glad.h>

#include "ITexture.h"

class GLTexture : public ITexture {
public:
    explicit GLTexture(const TextureDesc& desc);

    bool create(void) override;
    void upload(const void* data, int miplevel = 0, int layer = 0) override;

    const TextureDesc& desc(void) const;
    GLuint handle(void) const;
private:
    static GLenum toGLInternalFormat(TextureDesc::E_TEXTURE_FORMAT format);
    static GLenum toGLFormat(TextureDesc::E_TEXTURE_FORMAT format);
    static GLenum toGLType(TextureDesc::E_TEXTURE_FORMAT format);

    TextureDesc m_desc;
    GLuint m_handle = 0;
};

#endif //__GLTEXTURE_H__
