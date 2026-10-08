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

