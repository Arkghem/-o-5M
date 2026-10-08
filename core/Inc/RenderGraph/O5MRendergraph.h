#ifndef __O5MRENDERGRAPH__H
#define __O5MRENDERGRAPH__H

#include <array>
#include <string>
#include <vector>
#include <concepts>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "RHI/O5MDevice.h"
#include "RenderGraph/O5MRendergraphType.h"
#include "RenderGraph/O5MPassBuilder.h"
#include "RenderGraph/O5MRenderContext.h"

//To delete in next commit.
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

using namespace O5MRendergraphNS;

//TODO: Input&Output pass need to be clarified in this graph.
class O5MRendergraph {
private:
    // per-handle sync state, one slot per ping-pong instance (slot 0 only for
    // non-history resources). Compile-time working memory for the two-round
    // barrier simulation -- not read at execute time.
    std::unordered_map<ResourceHandle, std::array<SyncScope, 2>> m_stateRecords;
    std::unordered_map<ResourceHandle, ResourceInfo> m_resourceInfos; //only description, no physcial resource, thus we can copy this.
    std::unordered_map<ResourceHandle, PhysicalResource> m_physicalResources; //the place we put the physcial resource in

    std::vector<PassDesc> m_passDescs;
    std::vector<uint32_t> m_executionOrder;

    std::vector<ResourceHandle> m_outputResources;

    O5MDevice& m_device;
public:
    O5MRendergraph(O5MDevice& device) : m_device(device) {};

    void addResourceInfo(ResourceInfo& info) {
        m_resourceInfos.try_emplace(info.handle, info);
    }

    void markOutput(ResourceHandle handle) {
        // Fail fast: only the handle is known here, names live in ResourceInfo
        // and are unreachable when the lookup misses.
        if (!m_resourceInfos.contains(handle))
            throw std::runtime_error("Output resource handle " + std::to_string(handle) +
                                     " not found in the graph.");
        m_outputResources.push_back(handle);
    }

    //In the future we might need to write a pass handle generator to replace the index.
    template <typename SetupFn, typename ExecuteFn>
        requires std::invocable<SetupFn, O5MPassBuilder&> &&
                 std::invocable<ExecuteFn, O5MRenderContext&, vk::raii::CommandBuffer&> 
    void addGraphicPass(const std::string& debugName, SetupFn setup, ExecuteFn execute) {
        PassDesc passDesc {
            .debugName = debugName,
            .kind = PassKind::Graphic,
            .executeFunc = execute
        };

        O5MPassBuilder builder(this, passDesc);
        setup(builder);

        m_passDescs.push_back(passDesc);
    }

    template <typename SetupFn, typename ExecuteFn>
        requires std::invocable<SetupFn, O5MPassBuilder&> &&
                 std::invocable<ExecuteFn, O5MRenderContext&, vk::raii::CommandBuffer&> 
    void addComputePass(std::string debugName, SetupFn setup, ExecuteFn execute) {
        PassDesc passDesc {
            .debugName = debugName,
            .kind = PassKind::Compute,
            .executeFunc = execute
        };

        O5MPassBuilder builder(this, passDesc);
        setup(builder);

        m_passDescs.push_back(passDesc);
    }

    //external resource import. not clear if it will goes into m_physicalResources
    ResourceHandle importTexture(
        std::string debugName,
        vk::Image image,
        vk::ImageView view,
        vk::Format format,
        vk::Extent2D extent,
        vk::ImageUsageFlags actualUsage,
        vk::ImageLayout currentLayout
    );

    ResourceHandle importBuffer(
        std::string debugName,
        vk::Buffer buffer,
        vk::DeviceSize size,
        vk::BufferUsageFlags actualUsage
    );

    void compile(void);
    // frameIndex: which frame this submit is. 0 replays the cold-start plan,
    // >= 1 replays the steady-state plan; parity picks ping-pong instances.
    void execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue,
                 vk::raii::Fence* fence = nullptr, uint32_t frameIndex = 0);

    //debug only: print out the compile result
    void dump(void) const;
};

#endif //__O5MRENDERGRAPH__H

