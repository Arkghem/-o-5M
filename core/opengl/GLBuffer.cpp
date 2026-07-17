#include "GLBuffer.h"

GLBuffer::GLBuffer(const BufferDesc& desc) : m_desc(desc) {}

GLBuffer::~GLBuffer(void) {}

bool GLBuffer::create(void) { 
    glCreateBuffers(1, &m_handle);

    GLbitfield flags = 0;
    GLenum hint = toGLHint(m_desc.memory_hint);

    if (m_desc.memory_hint == BufferDesc::STATIC) {
        glNamedBufferStorage(m_handle, m_desc.byte_size, nullptr, flags | GL_DYNAMIC_STORAGE_BIT);
    } else {
        glNamedBufferData(m_handle, m_desc.byte_size, nullptr, hint);
    }
    return true;
}

void GLBuffer::upload(const void* data, size_t size, size_t offset) { 
    glNamedBufferSubData(m_handle, offset, size, data);
}

void* GLBuffer::map(size_t offset, size_t size) {
    GLbitfield access = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT;
    if (m_desc.memory_hint == BufferDesc::STREAM) {
        access |= GL_MAP_UNSYNCHRONIZED_BIT;
    }
    return glMapNamedBufferRange(m_handle, offset, size, access);
}

void GLBuffer::unmap(void) { 
    glUnmapNamedBuffer(m_handle);
}

size_t GLBuffer::sizeInBytes(void) const { 
    return m_desc.byte_size;
}

const BufferDesc& GLBuffer::desc(void) const { 
    return m_desc;
}

GLuint GLBuffer::handle(void) const { 
    return m_handle;
}

GLenum GLBuffer::toGLHint(BufferDesc::E_MEMORYHINT h) { 
    switch (h) {
    case BufferDesc::STATIC:
        return GL_STATIC_DRAW;
    case BufferDesc::DYNAMIC:
        return GL_DYNAMIC_DRAW;
    case BufferDesc::STREAM:
        return GL_STREAM_DRAW;
    }
    return GL_STATIC_DRAW;
}
