#ifndef __GLPIPELINE_H__
#define __GLPIPELINE_H__

#include <glad/glad.h>
#include <vector>

#include "IGraphicsPipeline.h"

class GLShader;

class GLPipeline : public IGraphicsPipeline
{
public:
    struct AttributeFormatInfo {
        GLenum format;
        GLint componentCount;
        GLboolean normalisze;
    };
public:
    void setShaderStages(IShader* vs, IShader* fs, IShader* gs) override;
    void setVertexInputLayout(const VertexInputLayout& layout) override;
    void setRasterizerState(const RasterizerState& state) override;
    void setDepthStencilState(const DepthStencilState& state) override;
    void setBlendState(int idx, const BlendState& state) override;

    bool create(void) override;
        
    GLShader* vs(void) const { return m_vs; }
    GLShader* fs(void) const { return m_fs; }
    GLShader* gs(void) const { return m_gs; }

    GLint program(void) const { return m_program; }

    const RasterizerState& raster(void) const { return m_raster; }
    const DepthStencilState& depth(void) const { return m_depth; }
    const BlendState& blend(int i) const { return m_blends[i]; }
    const VertexInputLayout& layout(void) const { return m_layout; }

    int blendCount(void) const { return m_blends.size(); }
    bool isValid(void) const { return m_isValid; }

    static GLenum toGLCompareOp(DepthStencilState::CompareOp op);
    static GLenum toGLBlendFactor(BlendState::Factor factor);
    static AttributeFormatInfo attributeFormatInfo(VertexInputLayout::Attribute::FORMAT format);
private:
    GLuint m_program = 0;
    GLShader* m_vs = nullptr;
    GLShader* m_fs = nullptr;
    GLShader* m_gs = nullptr;
    RasterizerState m_raster;
    DepthStencilState m_depth;
    std::vector<BlendState> m_blends;
    VertexInputLayout m_layout;
    bool m_isValid = false;
};
#endif //__GLPIPELINE_H__
