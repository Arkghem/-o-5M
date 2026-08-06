#include "VKTexture.h"

#include <spdlog/spdlog.h>

#include <cassert>
#include <cstring>

// ===========================================================================
// VK_CHECK — local assertion macro for Vulkan result codes
// ===========================================================================
#define VK_CHECK(result, msg)                                         \
    do {                                                              \
        VkResult _res = (result);                                     \
        if (_res != VK_SUCCESS) {                                     \
            spdlog::error("Vulkan error: {} — VkResult={}", msg,      \
                          static_cast<int>(_res));                    \
            assert(false && msg);                                     \
        }                                                             \
    } while (0)

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

VKTexture::VKTexture(const TextureDesc& desc,
                     VkDevice device,
                     VkPhysicalDevice physicalDevice)
    : m_desc(desc)
    , m_device(device)
    , m_physicalDevice(physicalDevice)
{
}

VKTexture::~VKTexture() {
    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
}

// ===========================================================================
// create() — build VkImage + VkDeviceMemory + VkImageView from TextureDesc
// ===========================================================================

bool VKTexture::create() {
    // --- Scope lock assertions (C3, C4, C5) ---
    assert(m_desc.samples <= 1 && "C3: MSAA not supported in this phase");
    assert(m_desc.miplevels == 1 && "C4: mipmaps not supported in this phase");
    assert(m_desc.layers == 1 && "C5: texture arrays not supported in this phase");

    m_vkFormat = toVkFormat(m_desc.format);

    // --- VkImage ---
    VkImageCreateInfo imageCI{};
    imageCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCI.imageType     = VK_IMAGE_TYPE_2D;
    imageCI.format        = m_vkFormat;
    imageCI.extent        = { static_cast<uint32_t>(m_desc.width),
                              static_cast<uint32_t>(m_desc.height), 1 };
    imageCI.mipLevels     = 1;
    imageCI.arrayLayers   = 1;
    imageCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage         = deriveUsage(m_desc);
    imageCI.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK(vkCreateImage(m_device, &imageCI, nullptr, &m_image),
             "vkCreateImage failed");

    // --- Memory allocation (DEVICE_LOCAL) ---
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, m_image, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    uint32_t memTypeIndex = 0;
    bool     found        = false;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memReq.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memTypeIndex = i;
            found        = true;
            break;
        }
    }
    assert(found && "No DEVICE_LOCAL memory type found for VkImage");

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory),
             "vkAllocateMemory for VkImage failed");
    VK_CHECK(vkBindImageMemory(m_device, m_image, m_memory, 0),
             "vkBindImageMemory failed");

    // --- VkImageView ---
    VkImageAspectFlags aspectMask = toAspectMask(m_desc.format);

    VkImageViewCreateInfo viewCI{};
    viewCI.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image      = m_image;
    viewCI.viewType   = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format     = m_vkFormat;
    viewCI.subresourceRange.aspectMask     = aspectMask;
    viewCI.subresourceRange.baseMipLevel   = 0;
    viewCI.subresourceRange.levelCount     = 1;
    viewCI.subresourceRange.baseArrayLayer = 0;
    viewCI.subresourceRange.layerCount     = 1;

    VK_CHECK(vkCreateImageView(m_device, &viewCI, nullptr, &m_imageView),
             "vkCreateImageView failed");

    return true;
}

// ===========================================================================
// upload() — staging buffer → vkCmdCopyBufferToImage → layout transitions
// ===========================================================================

void VKTexture::upload(const void* data, int miplevel, int layer) {
    assert(data && "upload: data must not be null");
    assert(miplevel == 0 && "C4: single mip level only");
    assert(layer == 0 && "C5: single array layer only");
    assert(m_image != VK_NULL_HANDLE && "upload: call create() first");

    uint32_t rowPitch   = static_cast<uint32_t>(m_desc.width) * pixelSize(m_desc.format);
    VkDeviceSize bufSize = static_cast<VkDeviceSize>(rowPitch) * m_desc.height;

    // --- Staging buffer (HOST_VISIBLE | HOST_COHERENT) ---
    VkBuffer       stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufCI{};
    bufCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCI.size        = bufSize;
    bufCI.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateBuffer(m_device, &bufCI, nullptr, &stagingBuffer),
             "upload: vkCreateBuffer for staging");

    VkMemoryRequirements bufMemReq;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &bufMemReq);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);

    uint32_t stagingMemIndex = 0;
    bool     stagingFound    = false;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((bufMemReq.memoryTypeBits & (1u << i)) &&
            ((memProps.memoryTypes[i].propertyFlags &
              (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            stagingMemIndex = i;
            stagingFound    = true;
            break;
        }
    }
    assert(stagingFound && "upload: no HOST_VISIBLE|HOST_COHERENT memory type");

    VkMemoryAllocateInfo stagingAlloc{};
    stagingAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAlloc.allocationSize  = bufMemReq.size;
    stagingAlloc.memoryTypeIndex = stagingMemIndex;

    VK_CHECK(vkAllocateMemory(m_device, &stagingAlloc, nullptr, &stagingMemory),
             "upload: vkAllocateMemory for staging");
    VK_CHECK(vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0),
             "upload: vkBindBufferMemory for staging");

    // --- Map → memcpy → unmap ---
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(m_device, stagingMemory, 0, bufSize, 0, &mapped),
             "upload: vkMapMemory");
    std::memcpy(mapped, data, static_cast<size_t>(bufSize));
    vkUnmapMemory(m_device, stagingMemory);

    // --- Temporary command buffer (one-time submit) ---
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = 0; // assume queue family 0 = graphics
    poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateCommandPool(m_device, &poolCI, nullptr, &cmdPool),
             "upload: vkCreateCommandPool");

    VkCommandBufferAllocateInfo cmdAI{};
    cmdAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAI.commandPool        = cmdPool;
    cmdAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAI.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAI, &cmd),
             "upload: vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo),
             "upload: vkBeginCommandBuffer");

    // --- Layout: UNDEFINED → TRANSFER_DST_OPTIMAL ---
    transitionLayout(cmd,
                     VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // --- vkCmdCopyBufferToImage ---
    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0; // tightly packed
    region.bufferImageHeight = 0; // tightly packed
    region.imageSubresource.aspectMask     = toAspectMask(m_desc.format);
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = { static_cast<uint32_t>(m_desc.width),
                           static_cast<uint32_t>(m_desc.height), 1 };

    vkCmdCopyBufferToImage(cmd,
                           stagingBuffer,
                           m_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    // --- Layout: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL ---
    transitionLayout(cmd,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VK_CHECK(vkEndCommandBuffer(cmd), "upload: vkEndCommandBuffer");

    // --- Submit and wait ---
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(m_device, 0, 0, &queue);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;

    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE),
             "upload: vkQueueSubmit");
    VK_CHECK(vkQueueWaitIdle(queue),
             "upload: vkQueueWaitIdle");

    // --- Cleanup staging + temp command resources ---
    vkFreeCommandBuffers(m_device, cmdPool, 1, &cmd);
    vkDestroyCommandPool(m_device, cmdPool, nullptr);
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);
}

// ===========================================================================
// Dimension queries (delegate to TextureDesc)
// ===========================================================================

int VKTexture::width() const     { return m_desc.width; }
int VKTexture::height() const    { return m_desc.height; }
int VKTexture::depth() const     { return m_desc.depth; }
int VKTexture::miplevels() const { return m_desc.miplevels; }
int VKTexture::layers() const    { return m_desc.layers; }
int VKTexture::samples() const   { return m_desc.samples; }

// ===========================================================================
// Vulkan-specific accessors
// ===========================================================================

const TextureDesc& VKTexture::desc() const      { return m_desc; }
VkImageView VKTexture::imageView() const         { return m_imageView; }
VkImage     VKTexture::image() const             { return m_image; }
VkFormat    VKTexture::vkFormat() const          { return m_vkFormat; }

// ===========================================================================
// Format mapping: TextureDesc::E_TEXTURE_FORMAT → VkFormat
// ===========================================================================
//
// Teaching note — format mapping table:
//   R8_UNORM          → VK_FORMAT_R8_UNORM             (single-channel, 8-bit)
//   RGBA8_UNORM       → VK_FORMAT_R8G8B8A8_UNORM       (sRGB-aware swapchain)
//   RGBA16_SFLOAT     → VK_FORMAT_R16G16B16A16_SFLOAT  (HDR intermediate)
//   RGBA32_SFLOAT     → VK_FORMAT_R32G32B32A32_SFLOAT  (compute precision)
//   D16_UNORM         → VK_FORMAT_D16_UNORM             (low-precision depth)
//   D24_UNORM_S8_UINT → VK_FORMAT_D24_UNORM_S8_UINT    (depth+stencil packed)
//   D32_SFLOAT        → VK_FORMAT_D32_SFLOAT            (high-precision depth)
//
// Unlike GL where internalFormat/format/type are three separate enums,
// Vulkan uses a single VkFormat that encodes all three together.
// ===========================================================================

VkFormat VKTexture::toVkFormat(TextureDesc::E_TEXTURE_FORMAT format) {
    switch (format) {
    case TextureDesc::R8_UNORM:          return VK_FORMAT_R8_UNORM;
    case TextureDesc::RGBA8_UNORM:       return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureDesc::RGBA16_SFLOAT:     return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureDesc::RGBA32_SFLOAT:     return VK_FORMAT_R32G32B32A32_SFLOAT;
    case TextureDesc::D16_UNORM:         return VK_FORMAT_D16_UNORM;
    case TextureDesc::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
    case TextureDesc::D32_SFLOAT:        return VK_FORMAT_D32_SFLOAT;
    default:
        assert(false && "Unknown texture format");
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

// ===========================================================================
// Aspect mask mapping
// ===========================================================================
//
// Teaching note: Vulkan separates image "aspects" — a depth/stencil image
// has TWO aspects (DEPTH + STENCIL) that can be accessed independently.
// OpenGL hides this behind glReadPixels and depth-only attachment bindings.
// When creating a VkImageView, you MUST specify which aspects the view covers.
// A depth-only view cannot read stencil, and vice versa.
// ===========================================================================

VkImageAspectFlags VKTexture::toAspectMask(TextureDesc::E_TEXTURE_FORMAT format) {
    switch (format) {
    case TextureDesc::D16_UNORM:
    case TextureDesc::D32_SFLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case TextureDesc::D24_UNORM_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

// ===========================================================================
// Pixel size in bytes (for staging buffer allocation)
// ===========================================================================

uint32_t VKTexture::pixelSize(TextureDesc::E_TEXTURE_FORMAT format) {
    switch (format) {
    case TextureDesc::R8_UNORM:          return 1;
    case TextureDesc::RGBA8_UNORM:       return 4;
    case TextureDesc::RGBA16_SFLOAT:     return 8;
    case TextureDesc::RGBA32_SFLOAT:     return 16;
    case TextureDesc::D16_UNORM:         return 2;
    case TextureDesc::D24_UNORM_S8_UINT: return 4;
    case TextureDesc::D32_SFLOAT:        return 4;
    default:
        assert(false && "Unknown texture format");
        return 4;
    }
}

// ===========================================================================
// Usage flags: determine VkImageUsageFlags from TextureDesc::flags + format
// ===========================================================================

VkImageUsageFlags VKTexture::deriveUsage(const TextureDesc& desc) {
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT
                            | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (desc.flags & TextureDesc::RENDERTARGET) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    switch (desc.format) {
    case TextureDesc::D16_UNORM:
    case TextureDesc::D24_UNORM_S8_UINT:
    case TextureDesc::D32_SFLOAT:
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        break;
    default:
        break;
    }

    return usage;
}

// ===========================================================================
// Layout transition helper
// ===========================================================================
//
// Teaching note: Vulkan images have explicit "layouts" that tell the GPU how
// pixel data is organized in memory. Layout transitions are pipeline barriers
// — they don't move pixels, they flush caches and reorder memory for the next
// access pattern.
//
// Common layout transitions:
//   UNDEFINED → TRANSFER_DST_OPTIMAL      (prepare for vkCmdCopy*)
//   TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY (prepare for sampling)
//   SHADER_READ_ONLY → COLOR_ATTACHMENT     (prepare for rendering)
//   COLOR_ATTACHMENT → PRESENT_SRC          (prepare for swapchain present)
//
// OpenGL hides all of this — the driver inserts barriers implicitly.
// Vulkan's explicit model is why DSA-style APIs are faster: the driver
// does less guesswork about your intent.
// ===========================================================================

void VKTexture::transitionLayout(VkCommandBuffer cmd,
                                 VkImageLayout oldLayout,
                                 VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = m_image;
    barrier.subresourceRange.aspectMask     = toAspectMask(m_desc.format);
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    // Access masks: what operations must finish before the barrier (src)
    // and what operations must wait after the barrier (dst).
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        assert(false && "transitionLayout: unsupported layout transition");
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
}
