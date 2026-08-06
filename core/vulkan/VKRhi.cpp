#include "VKRhi.h"

#include "VulkanDevice.h"
#include "VKBuffer.h"
#include "VKTexture.h"
#include "VKShader.h"
#include "VKShaderResourceBindings.h"
#include "VKPipeline.h"
#include "VKFramebuffer.h"
#include "VKCommandBuffer.h"

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

// ===========================================================================
// init(GLFWwindow* window) — Vulkan bootstrap + N-buffered frame resources
// ===========================================================================
bool VKRhi::init(GLFWwindow* window)
{
    m_window = window;

    // Tell GLFW not to create an OpenGL context — Vulkan manages its own.
    // This MUST be set before VulkanDevice::init() which creates the surface
    // via glfwCreateWindowSurface.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_device = std::make_unique<VulkanDevice>();
    if (!m_device->init(window)) {
        spdlog::error("VKRhi: VulkanDevice::init failed");
        return false;
    }

    VkDevice device = m_device->device();
    uint32_t queueFamilyIndex = m_device->queueFamilyIndex();
    uint32_t imageCount = m_device->imageCount();

    // --- Command pool ---
    // Teaching note: VkCommandPool is a memory allocator for command buffers.
    // RESET_COMMAND_BUFFER_BIT allows individual reset during beginFrame —
    // the alternative (reset the whole pool) invalidates ALL buffers at once.
    //
    // Pool = single queue family lifetime. In a multi-threaded setup, each
    // thread gets its own pool (command buffers are NOT thread-safe to record
    // from different pools).
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        spdlog::error("VKRhi: vkCreateCommandPool failed");
        return false;
    }

    // --- N-buffered command buffers ---
    // Teaching note: N = swapchain image count (typically 2-3). While the GPU
    // executes command buffer[1] for the current frame, the CPU records
    // command buffer[2] for the next frame. This is the same principle as
    // double/triple buffering — CPU and GPU work in parallel, never waiting
    // on the same resource.
    m_commandBuffers.resize(imageCount);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = imageCount;

    if (vkAllocateCommandBuffers(device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        spdlog::error("VKRhi: vkAllocateCommandBuffers failed");
        return false;
    }

    // --- Per-frame synchronization ---
    // Teaching note: Vulkan uses two synchronization primitives for frame pacing:
    //
    //   Fence (GPU → CPU): CPU waits for the GPU to finish executing a
    //     command buffer. VK_FENCE_CREATE_SIGNALED_BIT = initially signaled
    //     so the first vkWaitForFences passes immediately (no submitted work yet).
    //
    //   Semaphore (GPU → GPU): GPU-internal signal/wait between queue
    //     operations. imageAvailable = "swapchain image is ready to render to".
    //     renderFinished = "rendering is done, image can be presented".
    //     Semaphores are FASTER than fences because they don't involve CPU.
    //
    //   Why 2 semaphores? vkQueueSubmit waits on imageAvailable AND signals
    //   renderFinished. vkQueuePresentKHR waits on renderFinished. This is
    //   the classic acquire→render→present handshake.
    m_inFlightFences.resize(imageCount);
    m_imageAvailable.resize(imageCount);
    m_renderFinished.resize(imageCount);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // initially open

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < imageCount; i++) {
        if (vkCreateFence(device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semInfo, nullptr, &m_imageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semInfo, nullptr, &m_renderFinished[i]) != VK_SUCCESS) {
            spdlog::error("VKRhi: sync object creation failed at frame {}", i);
            return false;
        }
    }

    // --- Command buffer wrapper (ICommandBuffer interface) ---
    m_commandBuffer = std::make_unique<VKCommandBuffer>();

    spdlog::info("VKRhi: initialized with {} swapchain images", imageCount);
    return true;
}

// ===========================================================================
// ~VKRhi() — reverse-order teardown
// ===========================================================================
VKRhi::~VKRhi()
{
    // Wait for all GPU work to finish before destroying resources.
    // Teaching: vkDeviceWaitIdle blocks until ALL queues are idle. It's safe
    // but heavy — real engines track per-resource readiness and destroy
    // incrementally. For a learning engine, this simplicity is correct.
    if (m_device && m_device->device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device->device());
    }

    VkDevice device = m_device ? m_device->device() : VK_NULL_HANDLE;

    // Command buffer wrapper (no VkCommandBuffer to free — pool owns them)
    m_commandBuffer.reset();

    // Destroy sync primitives
    for (auto fence : m_inFlightFences) {
        if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
    }
    for (auto sem : m_imageAvailable) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device, sem, nullptr);
    }
    for (auto sem : m_renderFinished) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device, sem, nullptr);
    }

    // Destroy command pool (frees all allocated command buffers)
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, m_commandPool, nullptr);
    }

    // VulkanDevice handles instance/device/surface/swapchain teardown
    m_device.reset();
}

// ===========================================================================
// Factory methods — create Vulkan-backend resource objects
// ===========================================================================

std::unique_ptr<IBuffer> VKRhi::newBuffer(const BufferDesc& desc)
{
    return std::make_unique<VKBuffer>(desc,
                                      m_device->device(),
                                      m_device->physicalDevice(),
                                      m_device->queue(),
                                      m_device->queueFamilyIndex());
}

std::unique_ptr<ITexture> VKRhi::newTexture(const TextureDesc& desc)
{
    return std::make_unique<VKTexture>(desc,
                                       m_device->device(),
                                       m_device->physicalDevice());
}

std::unique_ptr<IShader> VKRhi::newShader(E_SHADER_TYPE type, const char* source)
{
    return std::make_unique<VKShader>(type, source, m_device->device());
}

std::unique_ptr<IShaderResourceBindings> VKRhi::newShaderResourceBindings()
{
    return std::make_unique<VKShaderResourceBindings>(m_device->device(),
                                                       m_device->defaultSampler());
}

std::unique_ptr<IFramebuffer> VKRhi::newFramebuffer()
{
    return std::make_unique<VKFramebuffer>(m_device->device());
}

std::unique_ptr<IGraphicsPipeline> VKRhi::newGraphicsPipeline()
{
    return std::make_unique<VKPipeline>(m_device->device());
}

ICommandBuffer* VKRhi::commandBuffer()
{
    return m_commandBuffer.get();
}

// ===========================================================================
// beginFrame() — acquire swapchain image + start recording
// ===========================================================================
void VKRhi::beginFrame()
{
    VkDevice device = m_device->device();
    uint32_t N = m_device->imageCount();

    // --- Wait for the GPU to finish with this frame's resources ---
    // Teaching: fence = "is the GPU done with the command buffer from N frames ago?"
    // Fence is signaled when vkQueueSubmit completes. Without this wait, we'd
    // overwrite a command buffer that the GPU is still reading.
    vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]);

    // --- Acquire next swapchain image ---
    // Teaching: vkAcquireNextImageKHR returns the INDEX of the next available
    // swapchain image. The application does NOT know which image it gets
    // ahead of time — the OS/present engine decides. This is why per-image
    // framebuffers are pre-created: we just index into the array.
    //
    // m_imageAvailable[currentFrame] is signaled when the image becomes
    // available. UINT64_MAX timeout = wait forever (vsync-bound).
    VkResult result = vkAcquireNextImageKHR(
        device,
        m_device->swapchain(),
        UINT64_MAX,
        m_imageAvailable[m_currentFrame],
        VK_NULL_HANDLE,
        &m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain is out of date (e.g., window resize). C2 scope: log only,
        // no resize handling.
        spdlog::warn("VKRhi: swapchain out of date (VK_ERROR_OUT_OF_DATE_KHR)");
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        spdlog::error("VKRhi: vkAcquireNextImageKHR failed (VkResult={})",
                      static_cast<int>(result));
        return;
    }

    // --- Reset and begin recording ---
    // Teaching: vkResetCommandBuffer with flags=0 performs a "light" reset —
    // it clears the internal command list but keeps the pool allocation.
    // vkBeginCommandBuffer with ONE_TIME_SUBMIT tells the driver each record
    // is submitted exactly once, enabling driver optimizations (the driver
    // can skip caching compiled commands).
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo);

    // Wire the active VkCommandBuffer + swapchain framebuffer into the
    // ICommandBuffer wrapper. When upper code calls beginPass(nullptr),
    // VKCommandBuffer will use this swapchain framebuffer + render pass.
    m_commandBuffer->beginRecording(
        m_commandBuffers[m_currentFrame],
        m_device->swapchainFramebuffer(m_currentImageIndex),
        m_device->swapchainRenderPass(),
        m_device->swapchainExtent());
}

// ===========================================================================
// endFrame() — submit + present
// ===========================================================================
void VKRhi::endFrame()
{
    VkDevice device = m_device->device();
    VkQueue queue = m_device->queue();
    uint32_t N = m_device->imageCount();

    // --- End recording ---
    // Teaching: vkEndCommandBuffer finalizes the recorded command stream.
    // After this call, the buffer is immutable until reset — no more
    // Vulkan drawing commands can be added. This is what makes multi-threaded
    // recording possible: each thread builds a complete buffer, then the main
    // thread submits them all.
    m_commandBuffer->endRecording();

    // --- Submit to graphics queue ---
    // Teaching: the submit is where GPU execution actually starts. The two
    // semaphores create a dependency chain:
    //
    //   acquire → [wait: imageAvailable] → RECORDED COMMANDS →
    //            [signal: renderFinished] → present
    //
    // The pipeline stage mask tells the driver WHICH pipeline stage should
    // wait for the semaphore. COLOR_ATTACHMENT_OUTPUT = wait until the image
    // is ready before starting to write color attachments (vs. waiting until
    // the vertex shader starts, which would be wasteful).
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { m_imageAvailable[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[] = { m_renderFinished[m_currentFrame] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(queue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
        spdlog::error("VKRhi: vkQueueSubmit failed");
    }

    // --- Present ---
    // Teaching: vkQueuePresentKHR hands the rendered image to the presentation
    // engine (the OS compositor). It waits for renderFinished to ensure the
    // GPU has finished drawing before the compositor reads the image.
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = { m_device->swapchain() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(queue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        spdlog::warn("VKRhi: swapchain out of date during present (VK_ERROR_OUT_OF_DATE_KHR)");
    } else if (result != VK_SUCCESS) {
        spdlog::error("VKRhi: vkQueuePresentKHR failed (VkResult={})",
                      static_cast<int>(result));
    }

    // --- Advance frame index ---
    // Teaching: modulo wraps back to 0 after N frames, reusing the oldest
    // resources (which are guaranteed idle by the fence wait at the top of
    // beginFrame). This is the ring-buffer pattern: index = (index+1) % N.
    m_currentFrame = (m_currentFrame + 1) % N;
}
