#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "RHI/O5MDevice.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>

//delete RenderGraph
static vk::ImageAspectFlags aspectFromFormat(vk::Format fmt) {
    switch (fmt) {
        case vk::Format::eD32Sfloat:
        case vk::Format::eD16Unorm:
        case vk::Format::eX8D24UnormPack32:
            return vk::ImageAspectFlagBits::eDepth;
        case vk::Format::eS8Uint:
            return vk::ImageAspectFlagBits::eStencil;
        case vk::Format::eD16UnormS8Uint:
        case vk::Format::eD24UnormS8Uint:
        case vk::Format::eD32SfloatS8Uint:
            return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        default:
            return vk::ImageAspectFlagBits::eColor;
    }
}

// currentExtent == 0xFFFFFFFF is the spec sentinel for "the surface has no
// preferred size, the swapchain decides" -- a sentinel, not a large extent
static bool surfaceDictatesExtent(const vk::SurfaceCapabilitiesKHR& caps) {
    constexpr uint32_t kUndefinedExtent = std::numeric_limits<uint32_t>::max();
    return caps.currentExtent.width != kUndefinedExtent;
}

O5MDevice::O5MDevice(vk::raii::PhysicalDevice physicalDevice, vk::raii::Device device,
                     uint32_t queueFamilyIndex)
    : m_physicalDevice(std::move(physicalDevice)),
      m_device(std::move(device)),
      m_queueFamilyIndex(queueFamilyIndex) {
    m_memoryProperties = m_physicalDevice.getMemoryProperties();
    m_maxAllocationCount = m_physicalDevice.getProperties().limits.maxMemoryAllocationCount;

    m_queue = vk::raii::Queue(m_device, queueFamilyIndex, 0);

    // eTransient：short life-cycle but oftenly reused
    vk::CommandPoolCreateInfo poolInfo {
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = queueFamilyIndex
    };
    m_copyCommandPool = vk::raii::CommandPool(m_device, poolInfo);
}

uint32_t O5MDevice::findMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties) const {
    for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (m_memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("O5MDevice::findMemoryType: no suitable memory type found");
}

void O5MDevice::accountAllocation(void) {
    ++m_allocationCount;
    assert(m_allocationCount < m_maxAllocationCount &&
           "vkAllocateMemory approaching maxMemoryAllocationCount (~4096 on most drivers); "
           "time to sub-allocate (VMA) instead of one-allocation-per-resource");
}

std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>
O5MDevice::createBuffer(
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties
) {
    vk::BufferCreateInfo bufferInfo {
        .size = size,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };

    vk::raii::Buffer buffer(m_device, bufferInfo);

    vk::MemoryRequirements memReqs = buffer.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo {
        .allocationSize = memReqs.size,
        .memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties)
    };

    vk::raii::DeviceMemory memory(m_device, allocInfo);
    buffer.bindMemory(*memory, 0);

    accountAllocation();

    return { std::move(buffer), std::move(memory) };
}

std::pair<vk::raii::Image, vk::raii::DeviceMemory>
O5MDevice::createImage2D(vk::Format format, vk::Extent2D extent,
                         uint32_t mipLevels, vk::ImageTiling tiling,
                         vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties) {
    vk::ImageCreateInfo imageInfo {
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = { extent.width, extent.height, 1 },
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = vk::SharingMode::eExclusive,
    };

    vk::raii::Image image(m_device, imageInfo);

    vk::MemoryRequirements memReqs = image.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo {
        .allocationSize = memReqs.size,
        .memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties)
    };

    vk::raii::DeviceMemory memory(m_device, allocInfo);
    image.bindMemory(*memory, 0);

    accountAllocation();

    return { std::move(image), std::move(memory) };
}

vk::raii::ImageView O5MDevice::createImageView2D(
    const vk::raii::Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspectFlags
) {
    if (!aspectFlags) {
          aspectFlags = aspectFromFormat(format);
    }

    vk::ImageViewCreateInfo viewInfo {
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectFlags,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    return vk::raii::ImageView(m_device, viewInfo);
}

void O5MDevice::copyBuffer(const vk::raii::Buffer& src, const vk::raii::Buffer& dst,
                           vk::DeviceSize size) {
    vk::CommandBufferAllocateInfo allocInfo {
        .commandPool = *m_copyCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };

    vk::raii::CommandBuffers commandBuffers(m_device, allocInfo);
    vk::raii::CommandBuffer& cmd = commandBuffers.front();

    cmd.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    // srcOffset/dstOffset 缺省为 0，整段拷贝
    vk::BufferCopy copyRegion { .size = size };
    cmd.copyBuffer(*src, *dst, copyRegion);

    cmd.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*cmd);

    m_queue.submit(submitInfo);
    // v1 阻塞等队列空闲：简单且正确，专供加载期。
    // 升级路径：submit 带 fence → 下次 update() 收割，上传不再阻塞主线程。
    m_queue.waitIdle();
}

void O5MDevice::copyBufferToImage(const vk::raii::Buffer& src, const vk::raii::Image& dst,
                                  vk::Format format, vk::Extent2D extent) {
    vk::CommandBufferAllocateInfo allocInfo {
        .commandPool = *m_copyCommandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };

    vk::raii::CommandBuffers commandBuffers(m_device, allocInfo);
    vk::raii::CommandBuffer& cmd = commandBuffers.front();

    vk::ImageSubresourceRange range {
        .aspectMask = aspectFromFormat(format),
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    vk::ImageMemoryBarrier before {
        .srcAccessMask = vk::AccessFlagBits::eNone,
        .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .image = dst,
        .subresourceRange = range,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
    };

    vk::ImageMemoryBarrier after{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
        // oldLayout 必须是图像当前的真实布局（before-barrier 刚把它转进
        // TRANSFER_DST_OPTIMAL）；写 UNDEFINED 等于声明"内容可以丢弃"
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .image = dst,
        .subresourceRange = range,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
    };

    cmd.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    });

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer,
        {},
        nullptr,
        nullptr,
        before
    );

    vk::BufferImageCopy copyRegion {
        .imageSubresource = {
            .aspectMask = aspectFromFormat(format),
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageExtent = { extent.width, extent.height, 1 }
    };
    cmd.copyBufferToImage(*src, *dst, vk::ImageLayout::eTransferDstOptimal, copyRegion);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eAllCommands,
        {},
        nullptr,
        nullptr,
        after
    );

    cmd.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*cmd);

    m_queue.submit(submitInfo);
    m_queue.waitIdle();
}

O5MDevice::SwapchainBundle O5MDevice::createSwapchain(
    const vk::raii::SurfaceKHR& surface,
    vk::Extent2D requestedExtent
) {
    vk::SurfaceCapabilitiesKHR caps = m_physicalDevice.getSurfaceCapabilitiesKHR(*surface);
    std::vector<vk::SurfaceFormatKHR> fmts = m_physicalDevice.getSurfaceFormatsKHR(*surface);
    std::vector<vk::PresentModeKHR> presentModes = m_physicalDevice.getSurfacePresentModesKHR(*surface);
    std::vector<vk::QueueFamilyProperties> queueProps = m_physicalDevice.getQueueFamilyProperties();
    
    if (m_physicalDevice.getSurfaceSupportKHR(m_queueFamilyIndex, *surface) != vk::True)
        throw std::runtime_error("O5MDevice: graphics queue family cannot present to surface");

    if (!(queueProps[m_queueFamilyIndex].queueFlags &  vk::QueueFlagBits::eGraphics))
        throw std::runtime_error("...: m_queueFamilyIndex is not a graphics family");

    const uint32_t maxImageCount = caps.maxImageCount == 0
        ? std::numeric_limits<uint32_t>::max()
        : caps.maxImageCount;
    const uint32_t imageCount =
        std::clamp(caps.minImageCount + 1u, caps.minImageCount, maxImageCount);
    vk::PresentModeKHR mode = vk::PresentModeKHR::eFifo;

    if (fmts.empty())
        throw std::runtime_error("O5MDevice: surface reports no supported formats");

    const auto formatIt = std::ranges::find_if(
            fmts,
            [](const auto& format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });
    vk::SurfaceFormatKHR format = formatIt == fmts.end() ? fmts[0] : *formatIt;

    vk::Extent2D extent = requestedExtent;
    if (surfaceDictatesExtent(caps)) {
        extent = caps.currentExtent;
    } else {
        extent.width = std::clamp(extent.width,
            caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(extent.height,
            caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *surface,
        .minImageCount = imageCount,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = caps.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = mode,
        .clipped = true
    };

    SwapchainBundle res = {
        .swapchain = m_device.createSwapchainKHR(swapChainCreateInfo),
        .format = format.format,
        .extent = extent,
    };

    res.images = res.swapchain.getImages();
    res.imageCount = static_cast<uint32_t>(res.images.size());

    return res;
}
