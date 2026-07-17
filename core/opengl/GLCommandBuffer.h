#ifndef __GLCOMMANDBUFFER_H__
#define __GLCOMMANDBUFFER_H__

#include <glad/glad.h>
#include <map>

#include "ICommandBuffer.h"

class GLFramebuffer;
class GLPipeline;
class GLBuffer;

class GLCommandBuffer : public ICommandBuffer {
public:
    void beginPass(IFramebuffer* fb, const ClearValue& colorClear, const ClearValue& depthClear) override;
    void endPass(void) override;

    void setGraphicPipeline(IGraphicsPipeline* pso) override;
    void setViewport(int x, int y, int w, int h) override;
    void setScissor(int x, int y, int w, int h) override;
    void setShaderResources(IShaderResourceBindings* bindings) override;
    void setVertexInput(int bindingSlot, IBuffer* buffer, int stride) override;
    void setIndexBuffer(IBuffer* buffer, E_INDEX_FORMAT format) override;

    void draw(int vertexCount, int firstVertex = 0) override;
    void drawIndexed(int indexCount, int firstIndex = 0, int vertexOffset = 0) override;
    void drawInstanced(int vertexCount, int instanceCount, int firstVertex = 0, int firstInstance = 0) override;

    void pushDebugGroup(const char* name) override;
    void popDebugGroup(void) override;
private:
    struct VBBinding {
        GLBuffer* buffer;
        GLintptr offset;
    };

    GLFramebuffer* m_currentFramebuffer = nullptr;
    GLPipeline* m_currentPipeline = nullptr;
    GLBuffer* m_indexBuffer = nullptr;
    E_INDEX_FORMAT m_indexFormat = UINT16;
    std::map<int, struct VBBinding> m_vertexBindings;
};
#endif //__GLCOMMANDBUFFER_H__
