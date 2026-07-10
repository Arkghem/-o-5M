#ifndef __ICOMMAND_BUFFER_H
#define __ICOMMAND_BUFFER_H

#include <memory>

class IFramebuffer;
class IGraphicsPipeline;
class IShaderResourceBindings;
class IBuffer;

class ClearValue;

class ICommandBuffer {
public:
    virtual ~ICommandBuffer() = default;

    virtual void beginPass(IFramebuffer* fb,
                           const ClearValue& colorClear,
                           const ClearValue& depthClear) = 0;
    virtual void endPass(void) = 0;

    virtual void setGraphicPipeline(IGraphicsPipeline* pso) = 0;
    virtual void setViewport(int x, int y, int w, int h) = 0;
    virtual void setScissor(int x, int y, int w, int h) = 0;
    virtual void setShaderResources(IShaderResourceBindings* bindings) = 0;
    
    enum E_INDEX_FORMAT { UINT16, UINT32 };
    virtual void setVertexInput(int bindingSlot, IBuffer* buffer, int stride) = 0;
    virtual void setIndexBuffer(IBuffer* buffer, E_INDEX_FORMAT format) = 0;

    virtual void draw(int vertexCount, int firstVertex = 0) = 0;
    virtual void drawIndexed(int indexCount, int firstIndex = 0, int vertexOffset = 0) = 0;
    virtual void drawInstanced(int vertexCount, int instanceCount, int firstVertex = 0, int firstInstance = 0) = 0;

    virtual void pushDebugGroup(const char* name) = 0;
    virtual void popDebugGroup(void) = 0;
};

#endif //__ICOMMAND_BUFFER_H
