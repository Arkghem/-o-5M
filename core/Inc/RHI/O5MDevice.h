#pragma once

#include <cstdint>
#include <utility>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

class O5MDevice {
public:
    // queueFamilyIndex：copyBuffer 等"设备自执行"操作要用的 graphics 队列族，
    // 由 bootstrap（rhi_verify 的 findGraphicsQueueFamily）选定后传入。
    O5MDevice(vk::raii::PhysicalDevice physicalDevice, vk::raii::Device device,uint32_t queueFamilyIndex);

public:
    vk::raii::Device& getDevice(void) { return m_device; }
    const vk::raii::Device& getDevice(void) const { return m_device; }
    vk::raii::PhysicalDevice& getPhysicalDevice(void) { return m_physicalDevice; }
    vk::raii::Queue& getQueue(void) { return m_queue; } // graphics queue（构造时选定）

    // 按属性过滤 memory type。memoryProperties 在构造时查一次缓存，
    // 物理设备属性在进程生命周期内不变，每次调用重新 getMemoryProperties() 是浪费。
    uint32_t findMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties) const;

    // 线性资源：VBO/IBO/UBO/SSBO/Staging 全走这里。
    // 内部 = createBuffer + getMemoryRequirements + allocateMemory + bindMemory。
    std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>
    createBuffer(
        vk::DeviceSize size,
        vk::BufferUsageFlags usage,
        vk::MemoryPropertyFlags properties
    );

    std::pair<vk::raii::Image, vk::raii::DeviceMemory>
        createImage2D(vk::Format format, vk::Extent2D extent,
                      uint32_t mipLevels, vk::ImageTiling tilling,
                      vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties);

    vk::raii::ImageView createImageView2D(
        const vk::raii::Image& image,
        vk::Format format,
        vk::ImageAspectFlags aspect = {}); 

    void copyBuffer(const vk::raii::Buffer& src, const vk::raii::Buffer& dst,
                    vk::DeviceSize size);

    void copyBufferToImage(const vk::raii::Buffer& src, const vk::raii::Image& dst,
                           vk::Format format, vk::Extent2D extent);

    uint32_t getAllocationCount(void) const { return m_allocationCount; }
    uint32_t getMaxAllocationCount(void) const { return m_maxAllocationCount; }

private:
    void accountAllocation(void);
private:
    vk::raii::PhysicalDevice m_physicalDevice = nullptr;
    vk::raii::Device m_device = nullptr;
    vk::raii::Queue m_queue = nullptr;
    vk::raii::CommandPool m_copyCommandPool = nullptr;
    vk::PhysicalDeviceMemoryProperties m_memoryProperties;
    uint32_t m_maxAllocationCount = 0;
    uint32_t m_allocationCount = 0;
};
