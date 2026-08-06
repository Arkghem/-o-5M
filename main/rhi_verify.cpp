// rhi_verify.cpp — Minimal RHI verification: window → shader → buffer → PSO → FBO → draw → verify.
// Exercises the complete RHI pipeline. Supports both GLRhi (OpenGL 4.6) and VKRhi (Vulkan)
// via #ifdef O5M_HAS_VULKAN. GL path uses raw GL calls for blit/readback; Vulkan path uses
// staging buffer + fence for swapchain image readback.

// ── Common RHI interface headers (both backends) ─────────────────────────
#include "IRhi.h"
#include "IBuffer.h"
#include "ITexture.h"
#include "IShader.h"
#include "IGraphicsPipeline.h"
#include "ICommandBuffer.h"
#include "IShaderResourceBindings.h"
#include "IFramebuffer.h"
#include "ClearValue.h"

// ── Backend-specific headers ─────────────────────────────────────────────
#ifdef O5M_HAS_VULKAN
    #include <vulkan/vulkan.h>
    #include "VKRhi.h"
    #include "VulkanDevice.h"   // complete type needed by std::unique_ptr<VulkanDevice> destructor
    #include "VKCommandBuffer.h" // complete type needed by std::unique_ptr<VKCommandBuffer> destructor
#else
    #include <glad/glad.h>
    #include "GLRhi.h"
    #include "GLPipeline.h"
    #include "GLBuffer.h"
    #include "GLCommandBuffer.h"
#endif

#include <GLFW/glfw3.h>
#include <iostream>
#include <cstdlib>

// ── Shader sources (shared by both backends) ─────────────────────────────
//   Vulkan: VKShader auto-upgrades #version 410 → 450 via shaderc
//   OpenGL: compiled directly by the driver's GLSL compiler
static const char* kVertexSource = R"(
#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 0) out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 1.0);
    vColor = aColor;
}
)";

static const char* kFragmentSource = R"(
#version 410 core
layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

struct Vertex { float x, y, z, r, g, b; };

static const Vertex kTriangleVertices[3] = {
    { -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f },
    {  0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f },
    {  0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f },
};

static const int kWindowWidth  = 800;
static const int kWindowHeight = 600;

// ── checkGLError — GL-only diagnostic helper ─────────────────────────────
#ifndef O5M_HAS_VULKAN
static void checkGLError(const char* tag) {
    GLenum err;
    bool hadError = false;
    while ((err = glGetError()) != GL_NO_ERROR) {
        hadError = true;
        std::cerr << "[RHI_VERIFY] GL error after " << tag
                  << ": 0x" << std::hex << err << std::dec << "\n";
    }
    if (!hadError) std::cout << "[RHI_VERIFY] OK — " << tag << "\n";
}
#endif

// ── createWindow — GLFW window with backend-aware hints ──────────────────
static GLFWwindow* createWindow(int width, int height) {
    if (!glfwInit()) { std::cerr << "[RHI_VERIFY] glfwInit failed\n"; return nullptr; }

#ifdef O5M_HAS_VULKAN
    // Vulkan: no OpenGL context — GLFW_NO_API tells GLFW not to create one.
    // The Vulkan backend creates its own surface via glfwCreateWindowSurface.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
    // OpenGL: request 4.1 core profile (macOS max without explicit opt-in to 4.6).
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
#endif

    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    auto* w = glfwCreateWindow(width, height, "RHI Verify", nullptr, nullptr);
    if (!w) { std::cerr << "[RHI_VERIFY] glfwCreateWindow failed\n"; glfwTerminate(); }
    return w;
}

// =========================================================================
// main — unified RHI verification (GL or Vulkan backend)
// =========================================================================
int main() {
    GLFWwindow* window = createWindow(kWindowWidth, kWindowHeight);
    if (!window) return EXIT_FAILURE;
    std::cout << "[RHI_VERIFY] Window created\n";

    // ── Backend selection ────────────────────────────────────────────────
#ifdef O5M_HAS_VULKAN
    VKRhi rhi;
#else
    GLRhi rhi;
#endif

    if (!rhi.init(window)) {
        std::cerr << "[RHI_VERIFY] "
#ifdef O5M_HAS_VULKAN
                  << "VKRhi"
#else
                  << "GLRhi"
#endif
                  << "::init failed\n";
        return EXIT_FAILURE;
    }
#ifdef O5M_HAS_VULKAN
    std::cout << "[RHI_VERIFY] VKRhi initialized\n";
#else
    std::cout << "[RHI_VERIFY] GLRhi initialized\n";
    checkGLError("GLRhi::init");
#endif

    // ── Shaders (same RHI interface for both backends) ────────────────────
    auto vs = rhi.newShader(E_SHADER_TYPE::VERTEX,   kVertexSource);
    auto fs = rhi.newShader(E_SHADER_TYPE::FRAGMENT, kFragmentSource);
    if (!vs->compile()) { std::cerr << "[RHI_VERIFY] VS compile failed:\n" << vs->compileLog(); return EXIT_FAILURE; }
    if (!fs->compile()) { std::cerr << "[RHI_VERIFY] FS compile failed:\n" << fs->compileLog(); return EXIT_FAILURE; }
    std::cout << "[RHI_VERIFY] Shaders compiled\n";
#ifndef O5M_HAS_VULKAN
    checkGLError("shader compilation");
#endif

    // ── Vertex buffer ─────────────────────────────────────────────────────
    BufferDesc vboDesc;
    vboDesc.byte_size   = sizeof(kTriangleVertices);
    vboDesc.usage       = BufferDesc::VERTEXBUFFER;
    vboDesc.memory_hint = BufferDesc::STATIC;
    auto vbo = rhi.newBuffer(vboDesc);
    if (!vbo->create()) { std::cerr << "[RHI_VERIFY] VBO create failed\n"; return EXIT_FAILURE; }
    vbo->upload(kTriangleVertices, sizeof(kTriangleVertices), 0);
    std::cout << "[RHI_VERIFY] VBO: " << vbo->sizeInBytes() << " bytes\n";
#ifndef O5M_HAS_VULKAN
    checkGLError("VBO");
#endif

    // ── Textures (color + depth-stencil) ──────────────────────────────────
    TextureDesc colorDesc;
    colorDesc.width  = kWindowWidth;
    colorDesc.height = kWindowHeight;
    colorDesc.format = TextureDesc::RGBA8_UNORM;
    colorDesc.flags  = TextureDesc::RENDERTARGET;
    auto colorTex = rhi.newTexture(colorDesc);
    if (!colorTex->create()) { std::cerr << "[RHI_VERIFY] Color tex create failed\n"; return EXIT_FAILURE; }

    TextureDesc depthDesc;
    depthDesc.width  = kWindowWidth;
    depthDesc.height = kWindowHeight;
#ifdef O5M_HAS_VULKAN
    // MoltenVK on Apple Silicon silently converts D24_UNORM_S8_UINT VkImages to
    // D32_SFLOAT_S8_UINT, leaving the VkImageView/VkRenderPass on D24 → mismatch.
    // D32_SFLOAT is the most portable depth format (stencil not used here).
    depthDesc.format = TextureDesc::D32_SFLOAT;
#else
    depthDesc.format = TextureDesc::D24_UNORM_S8_UINT;
#endif
    depthDesc.flags  = TextureDesc::RENDERTARGET;
    auto depthTex = rhi.newTexture(depthDesc);
    if (!depthTex->create()) { std::cerr << "[RHI_VERIFY] Depth tex create failed\n"; return EXIT_FAILURE; }
    std::cout << "[RHI_VERIFY] Textures: " << colorTex->width() << "x" << colorTex->height() << "\n";
#ifndef O5M_HAS_VULKAN
    checkGLError("textures");
#endif

    // ── Framebuffer ───────────────────────────────────────────────────────
    auto fb = rhi.newFramebuffer();
    fb->attachColor(0, colorTex.get());
    fb->attachDepthStencil(depthTex.get());
    if (!fb->create()) { std::cerr << "[RHI_VERIFY] Framebuffer create failed\n"; return EXIT_FAILURE; }
    std::cout << "[RHI_VERIFY] Framebuffer created\n";
#ifndef O5M_HAS_VULKAN
    checkGLError("framebuffer");
#endif

    // ── Pipeline State Object ─────────────────────────────────────────────
    auto pso = rhi.newGraphicsPipeline();
    pso->setShaderStages(vs.get(), fs.get());
    {
        VertexInputLayout layout;
        layout.bindings.push_back({ 24, false });
        layout.attributes.push_back({ 0, 0, VertexInputLayout::Attribute::FLOAT32X3, 0  });
        layout.attributes.push_back({ 1, 0, VertexInputLayout::Attribute::FLOAT32X3, 12 });
        pso->setVertexInputLayout(layout);
    }
    {
        IGraphicsPipeline::RasterizerState raster;
        raster.cullMode = IGraphicsPipeline::RasterizerState::BACK;
        pso->setRasterizerState(raster);
    }
    {
        IGraphicsPipeline::DepthStencilState depth;
        depth.depthTest       = true;
        depth.depthWrite      = true;
        depth.depthCompareOp  = IGraphicsPipeline::DepthStencilState::LESS;
        pso->setDepthStencilState(depth);
    }
#ifndef O5M_HAS_VULKAN
    if (!pso->create()) { std::cerr << "[RHI_VERIFY] PSO create failed\n"; return EXIT_FAILURE; }
    std::cout << "[RHI_VERIFY] Pipeline (PSO) created\n";
#else
    // Vulkan has no no-arg pipeline create: VKPipeline is built lazily by
    // setGraphicPipeline (it needs the render pass + extent at creation time).
#endif
#ifndef O5M_HAS_VULKAN
    checkGLError("pipeline");
#endif

#ifdef O5M_HAS_VULKAN
    // A VkPipeline is baked to ONE render pass (attachment formats are part of
    // the PSO). The off-screen FBO pass (RGBA8 + depth) and the swapchain pass
    // (B8G8R8A8_SRGB, no depth) are different render passes → the swapchain
    // needs its own pipeline. It must be depthless: the swapchain render pass
    // has no depth attachment, and a depth-testing pipeline bound there is
    // render-pass-incompatible.
    auto psoSwap = rhi.newGraphicsPipeline();
    psoSwap->setShaderStages(vs.get(), fs.get());
    {
        VertexInputLayout layout;
        layout.bindings.push_back({ 24, false });
        layout.attributes.push_back({ 0, 0, VertexInputLayout::Attribute::FLOAT32X3, 0  });
        layout.attributes.push_back({ 1, 0, VertexInputLayout::Attribute::FLOAT32X3, 12 });
        psoSwap->setVertexInputLayout(layout);
    }
    {
        IGraphicsPipeline::RasterizerState raster;
        raster.cullMode = IGraphicsPipeline::RasterizerState::BACK;
        psoSwap->setRasterizerState(raster);
    }
    {
        IGraphicsPipeline::DepthStencilState depth;
        depth.depthTest       = false;
        depth.depthWrite      = false;
        depth.depthCompareOp  = IGraphicsPipeline::DepthStencilState::LESS;
        psoSwap->setDepthStencilState(depth);
    }
#endif

    auto* cmd    = rhi.commandBuffer();
    bool  passed = true;

    // ── Clear values (shared by both backends) ────────────────────────────
    ClearValue colorClear;
    colorClear.active    = true;
    colorClear.color[0]  = 0.1f;
    colorClear.color[1]  = 0.1f;
    colorClear.color[2]  = 0.2f;
    colorClear.color[3]  = 1.0f;

    ClearValue depthClear;
    depthClear.active  = true;
    depthClear.depth   = 1.0f;

    // ═════════════════════════════════════════════════════════════════════
    // Render Loop
    // ═════════════════════════════════════════════════════════════════════
    for (int frame = 0; frame < 3; ++frame) {
        rhi.beginFrame();

#ifdef O5M_HAS_VULKAN
        // ── Vulkan path: two passes per frame ─────────────────────────────
        //   Pass 1 — off-screen FBO: validates VKFramebuffer synthesis
        //            (VkRenderPass + VkFramebuffer from attachColor/Depth).
        cmd->beginPass(fb.get(), colorClear, depthClear);
        cmd->setViewport(0, 0, kWindowWidth, kWindowHeight);
        cmd->setScissor(0, 0, kWindowWidth, kWindowHeight);
        cmd->setGraphicPipeline(pso.get());
        cmd->setVertexInput(0, vbo.get(), 0);
        cmd->draw(3, 0);
        cmd->endPass();

        //   Pass 2 — swapchain (beginPass(nullptr)): renders to screen.
        //            Uses the swapchain render pass + per-frame framebuffer
        //            wired by VKRhi::beginFrame() → beginRecording().
        //            psoSwap is depthless because the swapchain render pass
        //            has no depth attachment (separate from the FBO's pso).
        cmd->beginPass(nullptr, colorClear, depthClear);
        cmd->setViewport(0, 0, kWindowWidth, kWindowHeight);
        cmd->setScissor(0, 0, kWindowWidth, kWindowHeight);
        cmd->setGraphicPipeline(psoSwap.get());
        cmd->setVertexInput(0, vbo.get(), 0);
        cmd->draw(3, 0);
        cmd->endPass();
#else
        // ── OpenGL path (unchanged from original) ─────────────────────────
        cmd->beginPass(fb.get(), colorClear, depthClear);
        cmd->setViewport(0, 0, kWindowWidth, kWindowHeight);
        cmd->setScissor(0, 0, kWindowWidth, kWindowHeight);
        cmd->setGraphicPipeline(pso.get());
        cmd->setVertexInput(0, vbo.get(), 0);
        cmd->draw(3, 0);
        cmd->endPass();

        checkGLError(("render frame " + std::to_string(frame)).c_str());
#endif

        rhi.endFrame();

#ifndef O5M_HAS_VULKAN
        // ── GL: blit off-screen FBO → default framebuffer + readback ──────
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        auto* glPso = static_cast<GLPipeline*>(pso.get());
        glUseProgram(glPso->program());
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        GLuint vao;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        auto* glVbo = static_cast<GLBuffer*>(vbo.get());
        glVertexArrayVertexBuffer(vao, 0, glVbo->handle(), 0, 24);
        glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(vao, 0, 0);
        glEnableVertexArrayAttrib(vao, 0);
        glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, 12);
        glVertexArrayAttribBinding(vao, 1, 0);
        glEnableVertexArrayAttrib(vao, 1);

        glViewport(0, 0, kWindowWidth, kWindowHeight);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindVertexArray(0);
        glDeleteVertexArrays(1, &vao);

        if (frame == 2) {
            unsigned char pixel[4];
            glReadPixels(kWindowWidth / 2, kWindowHeight / 2, 1, 1,
                         GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            std::cout << "[RHI_VERIFY] Center pixel: R=" << (int)pixel[0]
                      << " G=" << (int)pixel[1] << " B=" << (int)pixel[2]
                      << " A=" << (int)pixel[3] << "\n";

            if (pixel[2] > 200 && pixel[0] < 100 && pixel[1] < 100) {
                std::cout << "[RHI_VERIFY] PASS — triangle rendered\n";
            } else {
                std::cerr << "[RHI_VERIFY] FAIL — expected blue center pixel\n";
                passed = false;
            }
            checkGLError("pixel readback");
        }
#endif // !O5M_HAS_VULKAN

#ifdef O5M_HAS_VULKAN
        // ── Vulkan: swapchain image readback on frame 2 ───────────────────
        //   Bypasses RHI — staging buffer + layout transition + barrier needed
        //   because the RHI has no surface/present abstraction yet. The readback
        //   validates that beginPass(nullptr) actually rendered to the swapchain.
        if (frame == 2) {
            auto* dev = rhi.device();
            VkDevice device = dev->device();
            VkQueue queue = dev->queue();
            uint32_t queueFamilyIndex = dev->queueFamilyIndex();
            VkExtent2D extent = dev->swapchainExtent();
            VkFormat format = dev->swapchainFormat();
            uint32_t imgIdx = rhi.currentImageIndex();
            VkImage srcImage = dev->swapchainImage(imgIdx);

            // Wait for all GPU work (render + present) to complete so the
            // swapchain image is safe to access.
            vkDeviceWaitIdle(device);

            // ── Staging buffer (host-visible, transfer destination) ───────
            VkDeviceSize bufferSize = extent.width * extent.height * 4;
            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

            VkBufferCreateInfo bufInfo{};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = bufferSize;
            bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            if (vkCreateBuffer(device, &bufInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
                std::cerr << "[RHI_VERIFY] Staging buffer creation failed\n";
                passed = false;
                goto readback_done;
            }

            VkMemoryRequirements memReqs;
            vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
            uint32_t memTypeIndex = dev->findMemoryType(
                memReqs.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReqs.size;
            allocInfo.memoryTypeIndex = memTypeIndex;

            if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
                std::cerr << "[RHI_VERIFY] Staging memory allocation failed\n";
                vkDestroyBuffer(device, stagingBuffer, nullptr);
                passed = false;
                goto readback_done;
            }
            vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

            // ── Temp command buffer (transient, one-shot copy) ────────────
            VkCommandPool tempPool = VK_NULL_HANDLE;
            VkCommandBuffer tempCmd = VK_NULL_HANDLE;

            {
                VkCommandPoolCreateInfo poolInfo{};
                poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                poolInfo.queueFamilyIndex = queueFamilyIndex;
                vkCreateCommandPool(device, &poolInfo, nullptr, &tempPool);

                VkCommandBufferAllocateInfo cmdInfo{};
                cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                cmdInfo.commandPool = tempPool;
                cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                cmdInfo.commandBufferCount = 1;
                vkAllocateCommandBuffers(device, &cmdInfo, &tempCmd);
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(tempCmd, &beginInfo);

            // ── Layout transition: PRESENT_SRC → TRANSFER_SRC ─────────────
            // The swapchain image is in VK_IMAGE_LAYOUT_PRESENT_SRC_KHR after
            // vkQueuePresentKHR. We need TRANSFER_SRC_OPTIMAL for vkCmdCopyImageToBuffer.
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = srcImage;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                vkCmdPipelineBarrier(tempCmd,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            // ── Copy swapchain image → staging buffer ─────────────────────
            {
                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;   // tightly packed
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = {0, 0, 0};
                region.imageExtent = {extent.width, extent.height, 1};

                vkCmdCopyImageToBuffer(tempCmd, srcImage,
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       stagingBuffer, 1, &region);
            }

            // ── Transition back to PRESENT_SRC (restore original layout) ──
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = srcImage;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

                vkCmdPipelineBarrier(tempCmd,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            vkEndCommandBuffer(tempCmd);

            // ── Submit copy commands + wait for completion ────────────────
            {
                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &tempCmd;
                vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
                vkQueueWaitIdle(queue);
            }

            // ── Map staging buffer → read center pixel ────────────────────
            void* data = nullptr;
            vkMapMemory(device, stagingMemory, 0, bufferSize, 0, &data);

            int cx = extent.width / 2;
            int cy = extent.height / 2;
            unsigned char* pixels = static_cast<unsigned char*>(data);
            unsigned char* pixel = pixels + (cy * extent.width + cx) * 4;

            // Swapchain format is typically B8G8R8A8 (BGRA byte order).
            // pixel[0]=B, pixel[1]=G, pixel[2]=R, pixel[3]=A in BGRA.
            // The triangle center is blue (0,0,1 in shader) → B≈255, G≈0, R≈0.
            bool isBGRA = (format == VK_FORMAT_B8G8R8A8_UNORM ||
                           format == VK_FORMAT_B8G8R8A8_SRGB);
            int blueIdx = isBGRA ? 0 : 2;  // B channel in BGRA, 3rd byte in RGBA
            int redIdx  = isBGRA ? 2 : 0;

            std::cout << "[RHI_VERIFY] Center pixel: R=" << (int)pixel[redIdx]
                      << " G=" << (int)pixel[1] << " B=" << (int)pixel[blueIdx]
                      << " A=" << (int)pixel[3]
                      << " (format=" << (isBGRA ? "BGRA" : "RGBA") << ")\n";

            if (pixel[blueIdx] > 200 && pixel[redIdx] < 100 && pixel[1] < 100) {
                std::cout << "[RHI_VERIFY] PASS — triangle rendered to swapchain\n";
            } else {
                std::cerr << "[RHI_VERIFY] FAIL — expected blue center pixel (got "
                          << (int)pixel[redIdx] << "," << (int)pixel[1]
                          << "," << (int)pixel[blueIdx] << ")\n";
                passed = false;
            }

            vkUnmapMemory(device, stagingMemory);

            // ── Cleanup temp resources ────────────────────────────────────
            vkDestroyBuffer(device, stagingBuffer, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);
            vkFreeCommandBuffers(device, tempPool, 1, &tempCmd);
            vkDestroyCommandPool(device, tempPool, nullptr);
        }
    readback_done:
#endif // O5M_HAS_VULKAN

        glfwPollEvents();
    }

    std::cout << "\n[RHI_VERIFY] " << (passed ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED") << "\n";

    glfwDestroyWindow(window);
    glfwTerminate();
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
