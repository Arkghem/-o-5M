#include "GLTexture.h"

GLTexture::GLTexture(const TextureDesc& desc) : m_desc(desc) {}

bool GLTexture::create(void) {
    GLenum target = (m_desc.flags & TextureDesc::CUBEMAP) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;

    glCreateTextures(target, 1, &m_handle);

    GLenum internalFmt = toGLInternalFormat(m_desc.format);

    if (target == GL_TEXTURE_CUBE_MAP) {
        glTextureStorage2D(m_handle, m_desc.miplevels, internalFmt, m_desc.width, m_desc.height);
    } else {
       int samples = m_desc.samples; 

       if (samples > 1) {
            glTextureStorage2DMultisample(m_handle, samples, internalFmt, m_desc.width, m_desc.height, GL_TRUE);
       } else {
           glTextureStorage2D(m_handle, m_desc.miplevels, internalFmt, m_desc.width, m_desc.height);
       }
    }
    return true;
}

void GLTexture::upload(const void* data, int miplevel, int layer) {
    GLenum format = toGLFormat(m_desc.format);
    GLenum type = toGLType(m_desc.format);

    if (m_desc.flags & TextureDesc::CUBEMAP) {
        glTextureSubImage3D(m_handle, miplevel, 0, 0, layer, m_desc.width >> miplevel, m_desc.height >> miplevel, 1, format, type, data);
    } else {
        glTextureSubImage2D(m_handle, miplevel, 0, 0, m_desc.width >> miplevel, m_desc.height >> miplevel, format, type, data);
    }
}

const TextureDesc& GLTexture::desc(void) const { return m_desc; }

GLuint GLTexture::handle(void) const { return m_handle; }

int GLTexture::width(void) const { return m_desc.width; }

int GLTexture::height(void) const { return m_desc.height; }

int GLTexture::depth(void) const { return m_desc.depth; }

int GLTexture::miplevels(void) const { return m_desc.miplevels; }

int GLTexture::layers(void) const { return m_desc.layers; }

int GLTexture::samples(void) const { return m_desc.samples; }

GLenum GLTexture::toGLInternalFormat(TextureDesc::E_TEXTURE_FORMAT format) {
    switch (format) {
    case TextureDesc::R8_UNORM:
        return GL_R8;
    case TextureDesc::RGBA8_UNORM:
        return GL_RGBA8;
    case TextureDesc::RGBA16_SFLOAT:
        return GL_RGBA16F;
    case TextureDesc::RGBA32_SFLOAT:
        return GL_RGBA32F;
    case TextureDesc::D16_UNORM:
        return GL_DEPTH_COMPONENT16;
    case TextureDesc::D24_UNORM_S8_UINT:
        return GL_DEPTH24_STENCIL8;
    case TextureDesc::D32_SFLOAT:
        return GL_DEPTH_COMPONENT32F;
    }
    return GL_RGBA8;
}

GLenum GLTexture::toGLFormat(TextureDesc::E_TEXTURE_FORMAT format) {
    switch (format) {
    case TextureDesc::R8_UNORM:
        return GL_RED;
    case TextureDesc::RGBA8_UNORM:
        return GL_RGBA;
    case TextureDesc::RGBA16_SFLOAT:
        return GL_RGBA;
    case TextureDesc::RGBA32_SFLOAT:
        return GL_RGBA;
    case TextureDesc::D16_UNORM:
        return GL_DEPTH_COMPONENT;
    case TextureDesc::D24_UNORM_S8_UINT:
        return GL_DEPTH_STENCIL;
    case TextureDesc::D32_SFLOAT:
        return GL_DEPTH_COMPONENT;
    }
    return GL_RGBA;
}

GLenum GLTexture::toGLType(TextureDesc::E_TEXTURE_FORMAT format) {
    switch (format) {
    case TextureDesc::R8_UNORM:
        return GL_UNSIGNED_BYTE;
    case TextureDesc::RGBA8_UNORM:
        return GL_UNSIGNED_BYTE;
    case TextureDesc::RGBA16_SFLOAT:
        return GL_FLOAT;
    case TextureDesc::RGBA32_SFLOAT:
        return GL_FLOAT;
    case TextureDesc::D16_UNORM:
        return GL_UNSIGNED_SHORT;
    case TextureDesc::D24_UNORM_S8_UINT:
        return GL_UNSIGNED_INT_24_8;
    case TextureDesc::D32_SFLOAT:
        return GL_FLOAT;
    }
    return GL_UNSIGNED_BYTE;
}
