#include "GLCommandBuffer.h"
#include "GLFramebuffer.h"
#include "GLPipeline.h"
#include "GLShader.h"
#include "GLShaderResourceBindings.h"
#include "GLBuffer.h"
#include "GLTexture.h"

void GLCommandBuffer::beginPass(IFramebuffer* fb, const ClearValue& colorClear, const ClearValue& depthClear) {
    auto* glFb = static_cast<GLFramebuffer*>(fb);
    glBindFramebuffer(GL_FRAMEBUFFER, glFb->handle());

    GLbitfield clearMask = 0;
    if (colorClear.active) {
        glClearNamedFramebufferfv(glFb->handle(), GL_COLOR, 0, &colorClear.color[0]);
        clearMask |= GL_COLOR_BUFFER_BIT;
    }

    if(depthClear.active) {
        glClearNamedFramebufferfv(glFb->handle(), GL_DEPTH, 0, &depthClear.depth);
        clearMask |= GL_DEPTH_BUFFER_BIT;
    }

    int count = glFb->colorCount();
    if (count > 0) {
        std::vector<GLenum> drawBufs(count);
        for (int i = 0; i < count; i++) {
            glClearNamedFramebufferfv(glFb->handle(), GL_COLOR, i, &colorClear.color[0]);
            drawBufs[i] = GL_COLOR_ATTACHMENT0 + i;
        }
        glNamedFramebufferDrawBuffers(glFb->handle(), count, drawBufs.data());
    }

    m_currentFramebuffer = glFb;
}

void GLCommandBuffer::endPass(void) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_currentFramebuffer = nullptr;
}

void GLCommandBuffer::setGraphicPipeline(IGraphicsPipeline* pso) {
    auto* glPso = static_cast<GLPipeline*>(pso);
    if (!glPso->isValid()) {
        return;
    }
    m_currentPipeline = glPso;

    GLuint program = glPso->vs()->program();
    glUseProgram(program);

    if (glPso->depth().depthTest) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GLPipeline::toGLCompareOp(glPso->depth().depthCompareOp));
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(glPso->depth().depthWrite ? GL_TRUE : GL_FALSE);

    for (int i = 0; i < glPso->blendCount(); i++) {
        auto& blend = glPso->blend(i);
        if (blend.enable) {
            glEnablei(GL_BLEND, i);
            glBlendFuncSeparatei(i,
                 GLPipeline::toGLBlendFactor(blend.srcColorBlendFactor),
                 GLPipeline::toGLBlendFactor(blend.dstColorBlendFactor),
                 GL_ONE,
                 GL_ZERO);
        } else {
            glDisablei(GL_BLEND, i);
        }
    }

    auto& raster = glPso->raster();
    switch (raster.cullMode) {
        case GLPipeline::RasterizerState::CullMode::NONE: glDisable(GL_CULL_FACE); break;
        case GLPipeline::RasterizerState::CullMode::FRONT: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
        case GLPipeline::RasterizerState::CullMode::BACK: glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
    }

    glPolygonMode(GL_FRONT_AND_BACK, raster.polygonMode == GLPipeline::RasterizerState::PolygonMode::FILL ? GL_FILL : GL_LINE);
}

void GLCommandBuffer::setShaderResources(IShaderResourceBindings* bindings) {
    auto* glBind = static_cast<GLShaderResourceBindings*>(bindings);

    for (auto& [slot, ubo] : glBind->ubos()) {
        size_t size = ubo.size ? ubo.size : ubo.buffer->desc().byte_size;
        glBindBufferRange(GL_UNIFORM_BUFFER, slot, ubo.buffer->handle(), ubo.offset, size);
    }

    for (auto& [slot, tex] : glBind->textures()) {
        glBindTextureUnit(slot, tex->handle());
    }
}

void GLCommandBuffer::setVertexInput(int bindingSlot, IBuffer* buffer, int stride) {
    auto* glBuffer = static_cast<GLBuffer*>(buffer);
    m_vertexBindings[bindingSlot] = { glBuffer, static_cast<GLintptr>(stride) };
}

void GLCommandBuffer::setIndexBuffer(IBuffer* buffer, E_INDEX_FORMAT format) {
   m_indexBuffer = static_cast<GLBuffer*>(buffer); 
   m_indexFormat = format;
}

void GLCommandBuffer::drawIndexed(int indexCount, int firstIndex, int vertexOffset) {
    if (!m_currentPipeline || !m_currentFramebuffer) {
        return;
    }

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glVertexArrayElementBuffer(vao, m_indexBuffer->handle());
    
    for (auto& [slot, vb] : m_vertexBindings) {

    }
}

void GLCommandBuffer::setViewport(int x, int y, int w, int h) {
    glViewport(x, y, w, h);
}
