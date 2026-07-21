#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <memory>
#include <iostream>

#include "GLRhi.h"
#include "GLBuffer.h"
#include "GLTexture.h"
#include "GLShader.h"
#include "GLShaderResourceBindings.h"
#include "GLFramebuffer.h"
#include "GLPipeline.h"
#include "GLCommandBuffer.h"

bool GLRhi::init(GLFWwindow* window) {
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback([](GLenum src, GLenum type, GLuint id, GLenum severity,
                              GLsizei length, const GLchar* message, const void* userParam) {
                if (severity != GL_DEBUG_SEVERITY_NOTIFICATION)
                    std::cerr<<"GL: "<<message<<std::endl;
            }, nullptr);

    m_commandBuffer = std::make_unique<GLCommandBuffer>();
    m_window = window;
    return true;
}

std::unique_ptr<IBuffer> GLRhi::newBuffer(const BufferDesc& desc) {
    return std::make_unique<GLBuffer>(desc);
}

std::unique_ptr<ITexture> GLRhi::newTexture(const TextureDesc& desc) {
    return std::make_unique<GLTexture>(desc);
}

std::unique_ptr<IShader> GLRhi::newShader(E_SHADER_TYPE type, const char* source) {
    return std::make_unique<GLShader>(type, source);
}

std::unique_ptr<IShaderResourceBindings> GLRhi::newShaderResourceBindings(void) { 
    return std::make_unique<GLShaderResourceBindings>(); 
}

std::unique_ptr<IFramebuffer> GLRhi::newFramebuffer(void) {
    return std::make_unique<GLFramebuffer>();
}

std::unique_ptr<IGraphicsPipeline> GLRhi::newGraphicsPipeline(void) { 
    return std::make_unique<GLPipeline>(); 
}

ICommandBuffer* GLRhi::commandBuffer(void) {
    return m_commandBuffer.get();
}

void GLRhi::endFrame(void) {
    glfwSwapBuffers(m_window);
}
