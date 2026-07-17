#include "GLShaderResourceBindings.h"
#include "GLBuffer.h"
#include "GLTexture.h"

void GLShaderResourceBindings::bindUniformBuffer(int binding, IBuffer* buffer, size_t offset, size_t size) {
    m_UBOs[binding] = { static_cast<GLBuffer*>(buffer), offset, size };
}

void GLShaderResourceBindings::bindTexture(int binding, ITexture* texture) {
    m_textures[binding] = static_cast<GLTexture*>(texture);
}

bool GLShaderResourceBindings::create(void) {
    return true;
}

const std::map<int, GLShaderResourceBindings::UBOBinding>& GLShaderResourceBindings::ubos(void) const {
    return m_UBOs;
}

const std::map<int, GLTexture*>& GLShaderResourceBindings::textures(void) const {
    return m_textures;
}
