#ifndef __ISHADERRESOURCEBINDINGS_H
#define __ISHADERRESOURCEBINDINGS_H

#include <memory>

class IBuffer;
class ITexture;

class IShaderResourceBindings {
public:
    virtual ~IShaderResourceBindings() = default;   

    virtual void bindUniformBuffer(int binding, IBuffer* buffer, size_t offset = 0, size_t size = 0) = 0;

    virtual void bindTexture(int binding, ITexture* texture) = 0;

    virtual bool create(void) = 0;
};

#endif //__ISHADERRESOURCEBINDINGS_H
