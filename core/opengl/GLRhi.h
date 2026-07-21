#ifndef __GLRHI_H__
#define __GLRHI_H__

#include "IRhi.h"

class GLFWwindow;

class GLCommandBuffer;

class GLRhi : public IRhi {
public:
    bool init(GLFWwindow* window);

    std::unique_ptr<IBuffer> newBuffer(const BufferDesc&) override;
    std::unique_ptr<ITexture> newTexture(const TextureDesc&) override;
    std::unique_ptr<IShader> newShader(E_SHADER_TYPE, const char* source) override;
    std::unique_ptr<IFramebuffer> newFramebuffer(void) override;
    std::unique_ptr<IGraphicsPipeline> newGraphicsPipeline(void) override;
    std::unique_ptr<IShaderResourceBindings> newShaderResourceBindings(void) override;

    ICommandBuffer* commandBuffer(void) override; 

    void beginFrame(void) override {};
    void endFrame(void) override;
private:
    GLFWwindow* m_window;
    std::unique_ptr<GLCommandBuffer> m_commandBuffer;
};
#endif //__GLRHI_H__
