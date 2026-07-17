#include "GLPipeline.h"
#include "GLShader.h"

void GLPipeline::setShaderStages(IShader* vs, IShader* fs, IShader* gs) {
    m_vs = static_cast<GLShader*>(vs);
    m_fs = static_cast<GLShader*>(fs);
}

void GLPipeline::setVertexInputLayout(const VertexInputLayout& layout) {
    m_layout = layout;
}

void GLPipeline::setRasterizerState(const RasterizerState& state) {
    m_raster = state;
}

void GLPipeline::setDepthStencilState(const DepthStencilState& state) {
    m_depth = state;
}

void GLPipeline::setBlendState(int idx, const BlendState& state) {
    if (m_blends.size() <= idx) {
        m_blends.resize(idx + 1);
    }
    m_blends[idx] = state;
}

bool GLPipeline::create(void) {
    if (!m_vs || !m_fs) {
        return false;
    }

    m_isValid = true;
    return true;
}

GLenum GLPipeline::toGLCompareOp(DepthStencilState::CompareOp op) {
    switch (op) {
        case DepthStencilState::CompareOp::NEVER: return GL_NEVER;
        case DepthStencilState::CompareOp::LESS: return GL_LESS;
        case DepthStencilState::CompareOp::EQUAL: return GL_EQUAL;
        case DepthStencilState::CompareOp::LESS_OR_EQUAL: return GL_LEQUAL;
        case DepthStencilState::CompareOp::GREATER: return GL_GREATER;
        case DepthStencilState::CompareOp::ALWAYS: return GL_ALWAYS;
    }
    return GL_NEVER;
}

GLenum GLPipeline::toGLBlendFactor(BlendState::Factor factor) {
    switch (factor) {
        case BlendState::Factor::ZERO: return GL_ZERO;
        case BlendState::Factor::ONE: return GL_ONE;
        case BlendState::Factor::SRC_ALPHA: return GL_SRC_ALPHA;
        case BlendState::Factor::ONE_MINUS_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendState::Factor::DST_ALPHA: return GL_DST_ALPHA;
        case BlendState::Factor::ONE_MINUS_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
    }
    return GL_ZERO;
}
