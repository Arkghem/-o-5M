#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <memory>

#include "IBuffer.h"
#include "GLRhi.h"
#include "GLBuffer.h"

std::unique_ptr<IBuffer> GLRhi::newBuffer(const BufferDesc& desc) {
    return std::make_unique<GLBuffer>(desc);
}

