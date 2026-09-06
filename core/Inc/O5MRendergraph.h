#ifndef __O5MRENDERGRAPH__H
#define __O5MRENDERGRAPH__H

#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

class O5MRendergraph {
public:
    enum class ResourceKind { Image, Buffer };

    struct ResourceDesc {
        ResourceDesc& operator=(ResourceDesc& other) {
            name = other.name;
            kind = other.kind;
            format = other.format;
            extent = other.extent;
            usage = other.usage;
            size = other.size;
            bufferUsage = other.bufferUsage;
            memoryProperties = other.memoryProperties;
            initialLayout = other.initialLayout;
            finalLayout = other.finalLayout;

            return *this;
        };

        std::string name;
        ResourceKind kind = ResourceKind::Image;

        // image 字段
        vk::Format format;
        vk::Extent2D extent;
        vk::ImageUsageFlags usage;
        vk::ImageLayout initialLayout;
        vk::ImageLayout finalLayout;

        // buffer 字段
        vk::DeviceSize size = 0;
        vk::BufferUsageFlags bufferUsage;
        vk::MemoryPropertyFlags memoryProperties;

        vk::raii::Image image = nullptr;
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::ImageView imageView = nullptr;
        vk::raii::Buffer buffer = nullptr;
    };

private:
    struct PassDesc {
        std::string name;
        std::vector<std::string> inputs;
        std::vector<std::string> outputs; //why string
        std::function<void(vk::raii::CommandBuffer&)> executeFuct;
    };
private:
    std::unordered_map<std::string, ResourceDesc> m_resources;
    std::vector<PassDesc> m_passDescs;
    std::vector<size_t> m_executionOrder;//?so this class is not just a node
    std::vector<vk::raii::Semaphore> m_semaphores;
    std::vector<std::pair<size_t,size_t>> m_semaphoreSignalWaitPairs;//signal pass, waiting pass, I thought we need two semaphore signals

    vk::raii::Device& m_device;
    vk::PhysicalDevice m_physicalDevice; // findMemoryType 需要 memoryProperties
public:
    O5MRendergraph(vk::raii::Device& device, vk::PhysicalDevice physicalDevice)
        : m_device(device), m_physicalDevice(physicalDevice) {}

    // 按属性过滤 memory type。 teaching note: memoryTypes 来自
    // vkGetPhysicalDeviceMemoryProperties，typeBits 是 bitmask，第 i 位为 1 表示该 type 可用。
    uint32_t findMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags properties) const;

    ResourceDesc* getResource(const std::string& name) {
        auto it = m_resources.find(name);
        return it != m_resources.end() ? &it->second : nullptr;
    }

    void addResource(const std::string& name, vk::Format format, vk::Extent2D extent,
                     vk::ImageUsageFlags uasge, vk::ImageLayout initialLayout,
                     vk::ImageLayout finalLayout);

    // buffer 资源：memoryProperties 决定放 HOST_VISIBLE（CPU 可 map 写入，适合 UBO）
    // 还是 DEVICE_LOCAL（需 staging，适合顶点/索引/回读中转）。
    void addBufferResource(const std::string& name, vk::DeviceSize size,
                           vk::BufferUsageFlags usage,
                           vk::MemoryPropertyFlags memoryProperties);

    void addPass(const std::string& name, const std::vector<std::string>& inputs,
                 const std::vector<std::string>& outputs,
                 std::function<void(vk::raii::CommandBuffer&)> executeFunc);

    void compile(void);

    // fence: 调用方持有 fence（frames-in-flight 的雏形），submit 时携带并等待。
    // 传 nullptr 保持旧行为（只提交不等）。
    void execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue,
                 vk::raii::Fence* fence = nullptr);
};

#endif //__O5MRENDERGRAPH__H

