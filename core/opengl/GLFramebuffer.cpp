#include <iostream>

#include "GLFramebuffer.h"
#include "GLTexture.h"

bool GLFramebuffer::create(void) {
    glCreateFramebuffers(1, &m_handle);

    for (auto& [index, tex] : m_colorAttachments) {
        auto* glTex = static_cast<GLTexture*>(tex.texture);
        GLenum attachment = GL_COLOR_ATTACHMENT0 + index;
        glNamedFramebufferTexture(m_handle, attachment, glTex->handle(), tex.mipLevel);
    }

    if (m_depthAttachment.texture) {
        auto* glTex = static_cast<GLTexture*>(m_depthAttachment.texture);
        glNamedFramebufferTexture(m_handle, GL_DEPTH_STENCIL_ATTACHMENT, glTex->handle(), m_depthAttachment.mipLevel);
    }

    GLenum status = glCheckNamedFramebufferStatus(m_handle, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr<<"Framebuffer not complete: "<<status<<std::endl;
        return false;
    }

    return true;
}

void GLFramebuffer::attachColor(int idx, ITexture* tex, int mipLevel, int layer) {
    m_colorAttachments[idx] = { tex, mipLevel, layer };
}

void GLFramebuffer::attachDepthStencil(ITexture* tex, int mipLevel, int layer) {
    m_depthAttachment = { tex, mipLevel, layer };
}

GLuint GLFramebuffer::handle(void) const { return m_handle; }

int GLFramebuffer::colorCount(void) const { return m_colorAttachments.size(); }
