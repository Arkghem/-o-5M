#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "RHI/O5MDevice.h"

#include <cassert>
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

O5MDevice::O5MDevice(vk::raii::PhysicalDevice physicalDevice, vk::raii::Device device,
                     uint32_t queueFamilyIndex)
    : m_physicalDevice(std::move(physicalDevice)),
      m_device(std::move(device)) {
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
