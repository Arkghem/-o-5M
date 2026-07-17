#ifndef __GLPIPELINE_H__
#define __GLPIPELINE_H__

#include <glad/glad.h>
#include <vector>

#include "IGraphicsPipeline.h"

class GLShader;

class GLPipeline : public IGraphicsPipeline
{
public:
    void setShaderStages(IShader* vs, IShader* fs, IShader* gs) override;
    void setVertexInputLayout(const VertexInputLayout& layout) override;
    void setRasterizerState(const RasterizerState& state) override;
    void setDepthStencilState(const DepthStencilState& state) override;
    void setBlendState(int idx, const BlendState& state) override;

    bool create(void) override;
        
    GLShader* vs(void) const { return m_vs; }
    GLShader* fs(void) const { return m_fs; }
    const RasterizerState& raster(void) const { return m_raster; }
    const DepthStencilState& depth(void) const { return m_depth; }
    const BlendState& blend(int i) const { return m_blends[i]; }
    int blendCount(void) const { return m_blends.size(); }
    bool isValid(void) const { return m_isValid; }

    static GLenum toGLCompareOp(DepthStencilState::CompareOp op);
    static GLenum toGLBlendFactor(BlendState::Factor factor);
private:
    GLShader* m_vs = nullptr;
    GLShader* m_fs = nullptr;
    RasterizerState m_raster;
    DepthStencilState m_depth;
    std::vector<BlendState> m_blends;
    VertexInputLayout m_layout;
    bool m_isValid = false;
};
#endif //__GLPIPELINE_H__
