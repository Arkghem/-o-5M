#pragma once

#include <cstdint>
#include <utility>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

// O5MDevice — 设备层，全引擎所有 GPU 对象创建/内存分配的唯一出口。
// 立法（对应 AGENTS.md 的 RHI 规则的 Vulkan 版）：
//   vkCreateBuffer / vkCreateImage / vkAllocateMemory（含 raii 形式）
//   只允许出现在 O5MDevice.cpp 一个文件里。
// 谁想创建 GPU 资源，持有 O5MDevice& 来调用，不许自己裸调。
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

    // 一次性拷贝（staging → device-local 的标准上传路径）：
    // 内部临时 cmd 记录 vkCmdCopyBuffer → 提交 → waitIdle。阻塞式，仅供加载期；
    // 每帧高频拷贝应录进 Rendergraph 的 pass 里，不走这里。
    void copyBuffer(const vk::raii::Buffer& src, const vk::raii::Buffer& dst,
                    vk::DeviceSize size);

    // 一次性上传（staging buffer → image，纹理上传的标准路径）：
    // 内部临时 cmd 记录 vkCmdCopyBufferToImage → 提交 → waitIdle，阻塞式，仅供加载期。
    // 拷贝 mip 0 / 单层；调用方负责用屏障把 dst 转到 eTransferDstOptimal 布局。
    void copyBufferToImage(const vk::raii::Buffer& src, const vk::raii::Image& dst,
                           vk::Format format, vk::Extent2D extent);



    // 分配账本：vkAllocateMemory 有 maxMemoryAllocationCount 天花板（多数驱动 ~4096）。
    // 现在每次分配一数，撞上限前 assert 会先叫。将来的解法是 VMA sub-allocation，
    // 到时候账本进 VMA 的回调，这行 assert 就是迁移完成的验收标准。
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
