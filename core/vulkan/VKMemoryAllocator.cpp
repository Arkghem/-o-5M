#include "VKMemoryAllocator.h"

#include <cassert>

// VKMemoryAllocator — teaching implementation of Vulkan memory allocation.
//
// Vulkan is the first mainstream API to force the app to manage GPU memory
// explicitly. OpenGL silently picks a memory type inside the driver; Vulkan
// gives you VkMemoryRequirements (size + alignment + typeFilter) and asks you
// to pick a heap. This class does the naive thing: one vkAllocateMemory per
// resource. No pooling, no sub-allocation, no recycling — each allocation is
// independent (C1 scope lock).
//
// Why real engines use VMA instead:
//   - vkAllocateMemory is expensive (driver round-trip, may map to OS-level
//     commits). VMA sub-allocates many small resources from one large block,
//     amortizing the driver calls.
//   - One-allocation-per-resource fragments memory badly: freeing one tiny
//     texture leaves a hole that a bigger allocation can't reuse. VMA packs
//     sub-allocations into blocks and can defragment.
//   - This allocator exists so you can SEE the fragmentation problem appear,
//     then appreciate what VMA does about it.

void VKMemoryAllocator::init(VkDevice device, VkPhysicalDevice physicalDevice) {
    // Both handles are needed: vkAllocateMemory needs the logical device, and
    // memory-type selection needs the physical device's memory properties.
    // VulkanDevice exposes both (device() / findMemoryType()), but we take raw
    // handles to stay decoupled from the singleton.
    m_device = device;
    m_physicalDevice = physicalDevice;
}

VKMemoryAllocator::MemoryBlock VKMemoryAllocator::allocate(
    const VkMemoryRequirements& req, VkMemoryPropertyFlags preferredProps) {
    MemoryBlock block;

    // Step 1: pick a memory type index.
    //
    // Memory types are the bridge between the app's needs and the GPU's heaps.
    // req.memoryTypeBits is a bitmask of types that can back this resource
    // (constraint from how the VkBuffer/VkImage was created). preferredProps
    // narrows it to types with the behavior we want:
    //   - DEVICE_LOCAL         — GPU-only memory, fastest for rendering
    //   - HOST_VISIBLE         — CPU can map it, needed for upload/readback
    //   - HOST_COHERENT        — no vkFlushMappedMemoryRanges needed
    // BufferDesc::STATIC → DEVICE_LOCAL; DYNAMIC/STREAM →
    // HOST_VISIBLE | HOST_COHERENT.
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    uint32_t memoryTypeIndex = 0;
    bool found = false;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & preferredProps) == preferredProps) {
            memoryTypeIndex = i;
            found = true;
            break;
        }
    }
    // A missing match is a coding bug (wrong preferredProps for the buffer's
    // usage), not a runtime condition — crash loud, per project convention.
    assert(found && "No Vulkan memory type satisfies requirements + preferred properties");

    // Step 2: ask the driver for one contiguous allocation.
    //
    // allocationSize = req.size: since this allocator does one block per
    // resource, we never carve sub-ranges, so offset is always 0. If we were
    // implementing VMA-style sub-allocation, this is where we'd request a large
    // block once and hand out aligned slices (alignUp() becomes useful there).
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkResult result = vkAllocateMemory(m_device, &allocInfo, nullptr, &block.memory);
    if (result != VK_SUCCESS) {
        // vkAllocateMemory fails with VK_ERROR_OUT_OF_DEVICE_MEMORY /
        // VK_ERROR_OUT_OF_HOST_MEMORY. Return a null block so the caller can
        // handle gracefully instead of asserting.
        return {};
    }

    block.offset = 0;
    block.size = req.size;
    return block;
}

void VKMemoryAllocator::free(MemoryBlock& block) {
    if (block.memory == VK_NULL_HANDLE) {
        return;
    }
    vkFreeMemory(m_device, block.memory, nullptr);
    block = {};  // reset to VK_NULL_HANDLE so double-free is a no-op
}

VkDeviceSize VKMemoryAllocator::alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    // Bit trick: (value + alignment - 1) & ~(alignment - 1). Works because
    // alignment is a power of two (guaranteed for all Vulkan alignments like
    // minUniformBufferOffsetAlignment). E.g. alignUp(10, 16): (10+15) & ~15 =
    // 25 & ~15 = 16. Rounds up to the next multiple of alignment.
    return (value + alignment - 1) & ~(alignment - 1);
}
