#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

class O5MDevice {
public:
    O5MDevice(vk::raii::PhysicalDevice physicalDevice, vk::raii::Device device,uint32_t queueFamilyIndex);

    struct SwapchainBundle {
        vk::raii::SwapchainKHR swapchain = nullptr;
        vk::Format format = vk::Format::eUndefined;
        vk::Extent2D extent;
        uint32_t imageCount = 0;
        std::vector<vk::Image> images;              // owned by the swapchain
        std::vector<vk::raii::ImageView> views;     // owned by us
    };

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

public:
    vk::raii::Device& getDevice(void) { return m_device; }
    const vk::raii::Device& getDevice(void) const { return m_device; }
    vk::raii::PhysicalDevice& getPhysicalDevice(void) { return m_physicalDevice; }
    vk::raii::Queue& getQueue(void) { return m_queue; }
                                                        
    uint32_t findMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties) const;

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

   SwapchainBundle createSwapchain(const vk::raii::SurfaceKHR& surface,
                                    vk::Extent2D requestedExtent);

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
    uint32_t m_queueFamilyIndex = 0;
    uint32_t m_maxAllocationCount = 0;
    uint32_t m_allocationCount = 0;
};
