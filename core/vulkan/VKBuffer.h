#ifndef __VKBUFFER_H__
#define __VKBUFFER_H__

#include "IBuffer.h"
#include <vulkan/vulkan.h>

// ===========================================================================
// VKBuffer — IBuffer implementation backed by VkBuffer + VkDeviceMemory
// ===========================================================================
//
// Teaching note: OpenGL binds a buffer to a "target" (GL_ARRAY_BUFFER,
// GL_UNIFORM_BUFFER, GL_ELEMENT_ARRAY_BUFFER, ...) to give it a role, and the
// role can change at any time. Vulkan flips this: the role is baked into the
// VkBuffer at creation time via VkBufferUsageFlags — a bitmask, so ONE buffer
// can serve several roles at once (e.g. vertex + storage). There is no bind
// state; usage flags are immutable for the buffer's lifetime, which matches how
// real GPUs want resources described up front.
//
// Memory: GLBuffer's STATIC/DYNAMIC/STREAM hints are driver heuristics — the
// driver guesses where to place the buffer. Vulkan makes the memory type
// explicit:
//   - STATIC        -> VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT  (GPU-only VRAM,
//                      fastest, but NOT CPU-mappable — uploads must go through
//                      a staging buffer + vkCmdCopyBuffer)
//   - DYNAMIC/STREAM -> VK_MEMORY_PROPERTY_HOST_VISIBLE |
//                      VK_MEMORY_PROPERTY_HOST_COHERENT    (CPU-mapped,
//                      persistent mapping for upload()/map())
// ===========================================================================
class VKBuffer : public IBuffer {
public:
    /// Takes raw Vulkan handles instead of a VulkanDevice singleton reference —
    /// same decoupling choice as VKMemoryAllocator. VKRhi will pass
    /// VulkanDevice::device(), queue() and queueFamilyIndex() when it creates
    /// buffers. The queue + family index are needed for the one-shot transfer
    /// that uploads into DEVICE_LOCAL memory.
    explicit VKBuffer(const BufferDesc& desc, VkDevice device,
                      VkPhysicalDevice physicalDevice, VkQueue queue,
                      uint32_t queueFamilyIndex);

    ~VKBuffer() override;   // vkDestroyBuffer + vkFreeMemory (reverse of create)

    bool create() override;
    void upload(const void* data, size_t size, size_t offset) override;

    void* map(size_t offset, size_t size) override;
    void unmap() override;

    size_t sizeInBytes() const override;
    const BufferDesc& desc() const;         // const ref — match GLBuffer pattern
    VkBuffer handle() const;                // for vkCmdBindVertexBuffers/IndexBuffer

private:
    /// One-shot transfer: temp command pool + command buffer, record the copy,
    /// submit, wait idle. Real engines reuse a persistent pool + per-frame
    /// command buffer instead of allocating one per upload.
    void submitImmediately(VkBuffer src, VkDeviceSize size, VkDeviceSize offset);

    BufferDesc m_desc;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    void* m_mappedPtr = nullptr;            // persistent mapping, host-visible only
};

#endif //__VKBUFFER_H__
