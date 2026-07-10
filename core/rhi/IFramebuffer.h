#ifndef __IFRAMEBUFFER_H
#define __IFRAMEBUFFER_H

class ITexture;

class IFramebuffer {
public:
    virtual ~IFramebuffer() = default;

    virtual void attachColor(int index, ITexture* texture, int mipLevel = 0, int layer = 0) = 0;
    virtual void attachDepthStencil(ITexture* texture, int mipLevel = 0, int layer = 0) = 0;

    virtual bool create(void) = 0;
};

#endif // __IFRAMEBUFFER_H
