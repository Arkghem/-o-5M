#ifndef __IBUFFER_H__
#define __IBUFFER_H__

#include <memory>

struct BufferDesc {
    size_t byte_size = 0;

    enum E_USAGE : uint32_t {
        VERTEXBUFFER    = 1 << 0,
        INDEXBUFFER     = 1 << 1,
        UNIFORMBUFFER   = 1 << 2,
        STORAGEBUFFER   = 1 << 3,
    };

    enum E_MEMORYHINT {
        STATIC,
        DYNAMIC,
        STREAM,
    };

    uint32_t usage = 0;
    E_MEMORYHINT memory_hint = STATIC;
};

class IBuffer {
public:
    virtual ~IBuffer(void) = default;

    virtual bool create(void) = 0;
    virtual void upload(const void* data, size_t size, size_t offset) = 0;

    //TODO Permentally mapping
    virtual void* map(size_t offset, size_t size) = 0;
    virtual void unmap(void) = 0;

    virtual size_t sizeInBytes(void) const = 0;
};

#endif //__IBUFFER_H
