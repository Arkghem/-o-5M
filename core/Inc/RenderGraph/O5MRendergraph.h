#ifndef __O5MRENDERGRAPH__H
#define __O5MRENDERGRAPH__H

#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "RHI/O5MDevice.h"

/*
class O5MRendergraph {
public:
    enum class ResourceKind { Image, Buffer };

    //ResourceDesc should not hold any specific vulkan resource
    //TODO clarify responsibilites of this ResourceDesc
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

        vk::Format format;
        vk::Extent2D extent;
        vk::ImageUsageFlags usage;
        vk::ImageLayout initialLayout;
        vk::ImageLayout finalLayout;

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

    O5MDevice& m_device; // GPU 对象创建/内存分配的唯一出口（见 O5MDevice 立法）
public:
    explicit O5MRendergraph(O5MDevice& device) : m_device(device) {}

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
*/

//TODO Phase1 start reconstruct the whole rendergraph.
class O5MRendergraph {
public:
    template<typename F1, typename F2>
    void addGraphicPass(std::string debugName, F1 PassBuilder, F2 Ctx) {

    }

    template<typename F1, typename F2>
    void addComputePass(std::string debugName, F1 PassBuilder, F2 Ctx) {

    }

    void compile(void);

    void execute(void);
};

#endif //__O5MRENDERGRAPH__H

