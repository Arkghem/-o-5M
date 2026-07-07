#ifndef __IRHI_H
#define __IRHI_H

#include <memory>

class IBuffer;
class BufferDesc;
class ITexture;
class TextureDesc;
class IShader;
class ShaderDesc;
class IFramebuffer;
class FramebufferDesc;
class IGraphicsPipeline;
class GraphicsPipelineDesc;

class IRhi {
public:
    virtual std::unique_ptr<IBuffer> newBuffer(const BufferDesc&) = 0;
    virtual std::unique_ptr<ITexture> newTexture(const TextureDesc&) = 0;
    virtual std::unique_ptr<IShader> newShader(const ShaderDesc&) = 0;
    virtual std::unique_ptr<IFramebuffer> newFramebuffer(const FramebufferDesc&) = 0;
    virtual std::unique_ptr<IGraphicsPipeline> newGraphicsPipeline(const GraphicsPipelineDesc&) = 0;

    virtual void beginFrame() = 0; //still don't know what this does
    virtual void endFrame() = 0;
};

#endif //__IRHI_H
