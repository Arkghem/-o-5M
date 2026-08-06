#ifndef __VKMEMORYALLOCATOR_H__
#define __VKMEMORYALLOCATOR_H__

#include <vulkan/vulkan.h>

/// Simplest Vulkan memory allocator — one VkDeviceMemory per resource.
/// Teaching note: this is intentionally minimal. Real engines use VMA
/// (VulkanMemoryAllocator) which sub-allocates from large blocks, reducing
/// vkAllocateMemory calls (driver-limited) and preventing fragmentation.
/// This allocator does ONE allocation per resource — good for learning,
/// bad for production.
class VKMemoryAllocator {
public:
    struct MemoryBlock {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize   offset = 0;
        VkDeviceSize   size   = 0;
    };

    /// The allocator needs a VkDevice + physical device memory properties.
    /// VulkanDevice provides both via device() + findMemoryType().
    void init(VkDevice device, VkPhysicalDevice physicalDevice);

    /// Allocate memory satisfying the requirements with preferred properties.
    /// Returns a MemoryBlock with .offset=0 (whole allocation).
    MemoryBlock allocate(const VkMemoryRequirements& req,
                         VkMemoryPropertyFlags preferredProps);

    /// Free a previously allocated block.
    void free(MemoryBlock& block);

    /// Align a value upward to alignment boundary.
    /// Teaching note: GPUs require aligned memory (e.g. uniform buffers at
    /// minUniformBufferOffsetAlignment). OpenGL hides this; Vulkan exposes it
    /// in VkPhysicalDeviceLimits. Failing to align causes VK_ERROR_*.
    static VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment);

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
};
#endif
