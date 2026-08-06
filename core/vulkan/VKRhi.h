#ifndef __VKRHI_H__
#define __VKRHI_H__

#include "IRhi.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

class VulkanDevice;
class VKCommandBuffer;
class GLFWwindow;

class VKRhi : public IRhi {
public:
    bool init(GLFWwindow* window);
    ~VKRhi() override;

    // --- IRhi factory ---
    std::unique_ptr<IBuffer> newBuffer(const BufferDesc&) override;
    std::unique_ptr<ITexture> newTexture(const TextureDesc&) override;
    std::unique_ptr<IShader> newShader(E_SHADER_TYPE, const char* source) override;
    std::unique_ptr<IShaderResourceBindings> newShaderResourceBindings() override;
    std::unique_ptr<IFramebuffer> newFramebuffer() override;
    std::unique_ptr<IGraphicsPipeline> newGraphicsPipeline() override;

    ICommandBuffer* commandBuffer() override;

    void beginFrame() override;
    void endFrame() override;

    // --- Accessors (for readback / debugging) ---
    VulkanDevice* device()           const { return m_device.get(); }
    uint32_t      currentImageIndex() const { return m_currentImageIndex; }

private:
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<VKCommandBuffer> m_commandBuffer;

    // N-buffered command buffers (N = swapchain image count)
    std::vector<VkCommandBuffer> m_commandBuffers;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    // Per-frame synchronization: one fence + one semaphore pair per frame
    std::vector<VkFence> m_inFlightFences;
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;

    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;

    GLFWwindow* m_window = nullptr;
};

#endif // __VKRHI_H__
