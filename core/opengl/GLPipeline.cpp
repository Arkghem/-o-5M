#include "GLPipeline.h"
#include "GLShader.h"

void GLPipeline::setShaderStages(IShader* vs, IShader* fs, IShader* gs) {
    m_vs = static_cast<GLShader*>(vs);
    m_fs = static_cast<GLShader*>(fs);
    m_gs = static_cast<GLShader*>(gs);
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

    m_program = glCreateProgram();

    m_vs->setProgram(m_program);
    m_fs->setProgram(m_program);
    m_gs->setProgram(m_program);
    
    m_vs->link();
    m_fs->link();
    m_gs->link();

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

GLPipeline::AttributeFormatInfo GLPipeline::attributeFormatInfo(VertexInputLayout::Attribute::FORMAT format) {
    switch (format) {
        case VertexInputLayout::Attribute::FLOAT32:
            return {GL_FLOAT, 1, GL_FALSE};
        case VertexInputLayout::Attribute::FLOAT32X2:
            return {GL_FLOAT, 2, GL_FALSE};
        case VertexInputLayout::Attribute::FLOAT32X3:
            return {GL_FLOAT, 3, GL_FALSE};
        case VertexInputLayout::Attribute::FLOAT32X4:
            return {GL_FLOAT, 4, GL_FALSE};
        case VertexInputLayout::Attribute::UINT8_UNORM:
            return {GL_UNSIGNED_BYTE, 1, GL_TRUE};
        case VertexInputLayout::Attribute::UINT8X2_UNORM:
            return {GL_UNSIGNED_BYTE, 2, GL_TRUE};
        case VertexInputLayout::Attribute::UINT8X4_UNORM:
            return {GL_UNSIGNED_BYTE, 4, GL_TRUE};
        case VertexInputLayout::Attribute::UINT16_UNORM:
            return {GL_UNSIGNED_SHORT, 1, GL_TRUE};
        case VertexInputLayout::Attribute::UINT16X2_UNORM:
            return {GL_UNSIGNED_SHORT, 2, GL_TRUE};
        case VertexInputLayout::Attribute::UINT16X4_UNORM:
            return {GL_UNSIGNED_SHORT, 4, GL_TRUE};
        case VertexInputLayout::Attribute::INT8_SNorm:
            return {GL_BYTE, 1, GL_TRUE};
        case VertexInputLayout::Attribute::INT8X2_SNORM:
            return {GL_BYTE, 2, GL_TRUE};
        case VertexInputLayout::Attribute::INT8X4_SNORM:
            return {GL_BYTE, 4, GL_TRUE};
        case VertexInputLayout::Attribute::UINT8:
            return {GL_UNSIGNED_BYTE, 1, GL_FALSE};
        case VertexInputLayout::Attribute::UINT8X2:
            return {GL_UNSIGNED_BYTE, 2, GL_FALSE};
        case VertexInputLayout::Attribute::UINT8X4:
            return {GL_UNSIGNED_BYTE, 4, GL_FALSE};
        case VertexInputLayout::Attribute::INT32:
            return {GL_INT, 1, GL_FALSE};
        case VertexInputLayout::Attribute::INT32X2:
            return {GL_INT, 2, GL_FALSE};
        case VertexInputLayout::Attribute::INT32X3:
            return {GL_INT, 3, GL_FALSE};
        case VertexInputLayout::Attribute::INT32X4:
            return {GL_INT, 4, GL_FALSE};
    }
    return {GL_FLOAT, 0, GL_FALSE};
}
