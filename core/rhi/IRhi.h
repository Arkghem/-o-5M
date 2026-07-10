#ifndef __IRHI_H
#define __IRHI_H

#include <memory>

class IBuffer;
class BufferDesc;

class ITexture;
class TextureDesc;

class IShader;
enum class E_SHADER_TYPE : uint8_t;

class IFramebuffer;
class IGraphicsPipeline;
class ICommandBuffer;

class IRhi {
public:
    virtual ~IRhi(void) = default;

    virtual std::unique_ptr<IBuffer> newBuffer(const BufferDesc&) = 0;
    virtual std::unique_ptr<ITexture> newTexture(const TextureDesc&) = 0;

    virtual std::unique_ptr<IShader> newShader(E_SHADER_TYPE, const char* source) = 0;

    virtual std::unique_ptr<IFramebuffer> newFramebuffer(void)= 0;
    virtual std::unique_ptr<IGraphicsPipeline> newGraphicsPipeline(void) = 0;

    virtual ICommandBuffer* commandBuffer(void) = 0;
};
#endif //__IRHI_H
