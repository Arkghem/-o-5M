#include "VKBuffer.h"

#include <spdlog/spdlog.h>

#include <cassert>
#include <cstring>

// ===========================================================================
// VK_CHECK — local assertion macro for Vulkan result codes
// ===========================================================================
// Teaching note: unlike OpenGL's polled glGetError(), Vulkan returns VkResult
// from every vkCreate*/vkAllocate*/vkMap* function and you MUST check each one.
// A missed VK_ERROR_OUT_OF_DEVICE_MEMORY surfaces as a crash many frames later
// with no clear root cause.
// ===========================================================================
#define VK_CHECK(result, msg)                                               \
    do {                                                                    \
        VkResult _res = (result);                                          \
        if (_res != VK_SUCCESS) {                                          \
            spdlog::error("Vulkan error: {} — VkResult={}", msg,            \
                          static_cast<int>(_res));                         \
            assert(false && msg);                                          \
        }                                                                  \
    } while (0)

namespace {

/// Pick a memory type satisfying typeFilter that also has all preferredProps.
/// Same query VulkanDevice::findMemoryType() performs — replicated here so
/// VKBuffer stays decoupled from the singleton (raw-handle pattern).
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                        VkMemoryPropertyFlags preferredProps) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & preferredProps) == preferredProps) {
            return i;
        }
    }

    spdlog::error("[VKBuffer] No memory type (filter=0x{:x}, props=0x{:x})",
                  typeFilter, static_cast<uint32_t>(preferredProps));
    assert(false && "No Vulkan memory type satisfies requirements + preferred properties");
    return 0;
}

/// Create a VkBuffer, allocate its memory, bind them together.
/// Returns the buffer; outMemory receives the VkDeviceMemory (caller maps/frees).
VkBuffer createBufferWithMemory(VkDevice device, VkPhysicalDevice physicalDevice,
                                VkDeviceSize size, VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags memoryProps,
                                VkDeviceMemory& outMemory) {
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    VK_CHECK(vkCreateBuffer(device, &bufInfo, nullptr, &buffer), "vkCreateBuffer");

    // Vulkan decouples the resource object from its memory: the buffer asks how
    // much memory it needs (VkMemoryRequirements), then the app allocates that
    // much and vkBindBufferMemory attaches them. GL's glBufferData hides this.
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;     // one allocation per resource (naive, like VKMemoryAllocator)
    allocInfo.memoryTypeIndex =
        findMemoryType(physicalDevice, memReq.memoryTypeBits, memoryProps);

    VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &outMemory), "vkAllocateMemory");
    VK_CHECK(vkBindBufferMemory(device, buffer, outMemory, 0), "vkBindBufferMemory");
    return buffer;
}

} // namespace

VKBuffer::VKBuffer(const BufferDesc& desc, VkDevice device,
                   VkPhysicalDevice physicalDevice, VkQueue queue,
                   uint32_t queueFamilyIndex)
    : m_desc(desc)
    , m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_queue(queue)
    , m_queueFamilyIndex(queueFamilyIndex) {}

VKBuffer::~VKBuffer() {
    // Persistent mapping must be released before the memory it points into.
    if (m_mappedPtr != nullptr) {
        vkUnmapMemory(m_device, m_memory);
        m_mappedPtr = nullptr;
    }
    // Buffer first, then memory — destroy order mirrors creation order.
    if (m_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

bool VKBuffer::create() {
    // --- Usage: GL's implicit target vs Vulkan's explicit bitmask ---
    // BufferDesc::usage is a bitmask (1<<0..1<<3), so multiple roles can be set
    // at once — Vulkan allows e.g. VERTEX_BUFFER_BIT | STORAGE_BUFFER_BIT in a
    // single VkBuffer, which GL's single-target model cannot express.
    VkBufferUsageFlags usage = 0;
    if (m_desc.usage & BufferDesc::VERTEXBUFFER)  usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (m_desc.usage & BufferDesc::INDEXBUFFER)   usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (m_desc.usage & BufferDesc::UNIFORMBUFFER) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (m_desc.usage & BufferDesc::STORAGEBUFFER) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    assert(usage != 0 && "VKBuffer: empty usage mask");

    // DEVICE_LOCAL memory cannot be written by the CPU, so every upload lands
    // via vkCmdCopyBuffer — which REQUIRES this flag on the destination.
    // Forgetting TRANSFER_DST_BIT is the classic "staging upload silently
    // fails validation" bug.
    usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    // --- Memory type from the memory hint ---
    // STATIC: data set once, used many times → put it in GPU-only VRAM and pay
    // a staging copy on upload. DYNAMIC/STREAM: updated per frame → keep it
    // CPU-visible and memcpy straight in.
    VkMemoryPropertyFlags memoryProps;
    if (m_desc.memory_hint == BufferDesc::STATIC) {
        memoryProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    } else {
        memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    m_buffer = createBufferWithMemory(m_device, m_physicalDevice, m_desc.byte_size,
                                      usage, memoryProps, m_memory);

    // --- Persistent mapping (fulfills IBuffer.h's "Permentally mapping" TODO) ---
    // HOST_VISIBLE memory is mapped ONCE and stays mapped for the buffer's
    // lifetime. HOST_COHERENT means every CPU write becomes visible to the GPU
    // without vkFlushMappedMemoryRanges — which is why upload() can memcpy
    // directly into m_mappedPtr.
    if (memoryProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        VK_CHECK(vkMapMemory(m_device, m_memory, 0, VK_WHOLE_SIZE, 0, &m_mappedPtr),
                 "vkMapMemory");
    }
    return true;
}

void VKBuffer::upload(const void* data, size_t size, size_t offset) {
    if (m_mappedPtr != nullptr) {
        // Host-visible (DYNAMIC/STREAM): write straight into the persistent
        // mapping. Vulkan's answer to glBufferSubData — and cheaper, because
        // there is no driver-side memory placement decision per call.
        std::memcpy(static_cast<char*>(m_mappedPtr) + offset, data, size);
        return;
    }

    // --- DEVICE_LOCAL (STATIC): transfer via a host-visible staging buffer ---
    // The CPU cannot map VRAM. Data first lands in a small CPU-visible buffer,
    // then vkCmdCopyBuffer moves it into GPU-only memory on the graphics queue.
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBuffer staging = createBufferWithMemory(
        m_device, m_physicalDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingMemory);

    void* stagingPtr = nullptr;
    VK_CHECK(vkMapMemory(m_device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &stagingPtr),
             "vkMapMemory (staging)");
    std::memcpy(stagingPtr, data, size);
    vkUnmapMemory(m_device, stagingMemory);

    submitImmediately(staging, size, offset);

    vkDestroyBuffer(m_device, staging, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);
}

void* VKBuffer::map(size_t offset, size_t /*size*/) {
    assert(m_mappedPtr != nullptr &&
           "VKBuffer::map on DEVICE_LOCAL (STATIC) memory is impossible — "
           "CPU-visible buffers only; use upload() for staging transfers");
    return static_cast<char*>(m_mappedPtr) + offset;
}

void VKBuffer::unmap() {
    // No-op: the mapping is persistent and HOST_COHERENT, so there is nothing
    // to flush or release. A HOST_VISIBLE-but-not-coherent mapping would call
    // vkFlushMappedMemoryRanges here.
}

size_t VKBuffer::sizeInBytes() const {
    return m_desc.byte_size;
}

const BufferDesc& VKBuffer::desc() const {
    return m_desc;
}

VkBuffer VKBuffer::handle() const {
    return m_buffer;
}

void VKBuffer::submitImmediately(VkBuffer src, VkDeviceSize size, VkDeviceSize offset) {
    // One-shot transfer path. Real engines keep a persistent command pool and
    // a per-frame command buffer instead of allocating one per upload — the
    // pool here is marked TRANSIENT so the driver knows it is short-lived.
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilyIndex;

    VkCommandPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &pool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "vkBeginCommandBuffer");

    VkBufferCopy region{};
    region.srcOffset = 0;
    region.dstOffset = offset;
    region.size = size;
    vkCmdCopyBuffer(cmd, src, m_buffer, 1, &region);

    VK_CHECK(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");

    // Wait so the caller can safely free the staging buffer right after.
    VK_CHECK(vkQueueWaitIdle(m_queue), "vkQueueWaitIdle");

    // Destroying the pool releases its command buffers too.
    vkDestroyCommandPool(m_device, pool, nullptr);
}
