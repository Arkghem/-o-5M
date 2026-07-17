#ifndef __GLBUFFER_H__
#define __GLBUFFER_H__

#include <glad/glad.h>

#include "IBuffer.h"

class GLBuffer : public IBuffer {
public:
    explicit GLBuffer(const BufferDesc& desc);
    ~GLBuffer(void) override;

    bool create(void) override;
    void upload(const void* data, size_t size, size_t offset) override;

    void* map(size_t offset, size_t size) override;
    void unmap(void) override;

    size_t sizeInBytes(void) const override;
    const BufferDesc& desc(void) const;
    GLuint handle(void) const;
private:
    static GLenum toGLHint(BufferDesc::E_MEMORYHINT h);

    BufferDesc m_desc;
    GLuint m_handle = 0;
};

#endif //__GLBUFFER_H__
