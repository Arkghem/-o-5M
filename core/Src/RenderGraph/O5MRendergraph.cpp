#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "RenderGraph/O5MRendergraph.h"

#include <unordered_map>
#include <vector>

#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_format_traits.hpp>

//SHIT it really sucks, I think the whole rendergraph part might need to be reconstruct
//But anyway, let's make it just works

/*vk::ImageAspectFlags aspectFromFormat(vk::Format fmt) {
    switch (fmt) {
        case vk::Format::eD32Sfloat://high persion depth, revered-z
        case vk::Format::eD16Unorm: //low percision depth, used in moblie platform
        case vk::Format::eX8D24UnormPack32: //24 bit depths, No idea why it have to packed as 32 bit
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

vk::ImageLayout readLayoutFromUsage(vk::ImageUsageFlags usage, vk::Format fmt) {
    vk::ImageAspectFlags aspect = aspectFromFormat(fmt);
    if (aspect & vk::ImageAspectFlagBits::eDepth && aspect & vk::ImageAspectFlagBits::eStencil) {
        if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment) 
            return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    } else if (aspect & vk::ImageAspectFlagBits::eDepth) {
        if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)
            return vk::ImageLayout::eDepthReadOnlyOptimal;
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    } else {
        // 读侧优先级：sampled > transferSrc > attachment。
        // 一个资源常同时带 ColorAttachment|Sampled（先渲染后采样），读它时是采样，
        // 必须给 ShaderReadOnly；只有"只作为 attachment 用"的资源才轮到 attachment 布局。
        if (usage & vk::ImageUsageFlagBits::eSampled)
            return vk::ImageLayout::eShaderReadOnlyOptimal;
        else if (usage & vk::ImageUsageFlagBits::eTransferSrc)
            return vk::ImageLayout::eTransferSrcOptimal;
        else if (usage & vk::ImageUsageFlagBits::eColorAttachment)
            return vk::ImageLayout::eColorAttachmentOptimal;
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    }
}

vk::ImageLayout layoutFromUsage(vk::ImageUsageFlags usage, vk::Format fmt) {
    vk::ImageAspectFlags aspect = aspectFromFormat(fmt);
    bool isDepthStencil =vk::hasDepthComponent(fmt) || vk::hasStencilComponent(fmt);

    if (aspect & vk::ImageAspectFlagBits::eDepth && aspect & vk::ImageAspectFlagBits::eStencil) {
        if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment) 
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    } else if (aspect & vk::ImageAspectFlagBits::eDepth) {
        if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)
            return vk::ImageLayout::eDepthAttachmentOptimal;
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    } else {
        if (usage & vk::ImageUsageFlagBits::eColorAttachment)
            return vk::ImageLayout::eColorAttachmentOptimal;
        return vk::ImageLayout::eShaderReadOnlyOptimal;
    }
}

vk::AccessFlags accessFlagFromBufferUsage(vk::BufferUsageFlags usage, bool isRead) {
    vk::AccessFlags flags;
    if (isRead) {
        if (usage & vk::BufferUsageFlagBits::eVertexBuffer)
            flags |= vk::AccessFlagBits::eVertexAttributeRead;
        if (usage & vk::BufferUsageFlagBits::eIndexBuffer)
            flags |= vk::AccessFlagBits::eIndexRead;
        if (usage & vk::BufferUsageFlagBits::eUniformBuffer)
            flags |= vk::AccessFlagBits::eUniformRead;
        if (usage & vk::BufferUsageFlagBits::eStorageBuffer)
            flags |= vk::AccessFlagBits::eShaderRead & vk::AccessFlagBits::eShaderWrite;
        if (usage & vk::BufferUsageFlagBits::eTransferSrc)
            flags |= vk::AccessFlagBits::eTransferRead;
    } else {
        if (usage & vk::BufferUsageFlagBits::eTransferDst)
            flags |= vk::AccessFlagBits::eTransferWrite;
        if (usage & vk::BufferUsageFlagBits::eStorageBuffer)
            flags |= vk::AccessFlagBits::eShaderWrite & vk::AccessFlagBits::eShaderRead;
    }

    return flags;
}

vk::AccessFlags accessFlagsFromLayout(vk::ImageLayout layout) {
    switch (layout) {
        case vk::ImageLayout::eUndefined:
        case vk::ImageLayout::eGeneral:
        case vk::ImageLayout::ePreinitialized:
            return vk::AccessFlagBits::eNone;
        case vk::ImageLayout::eColorAttachmentOptimal:
            return vk::AccessFlagBits::eColorAttachmentWrite;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return vk::AccessFlagBits::eShaderRead;
        case vk::ImageLayout::eTransferSrcOptimal:
            return vk::AccessFlagBits::eTransferRead;
        case vk::ImageLayout::eTransferDstOptimal:
            return vk::AccessFlagBits::eTransferWrite;
        case vk::ImageLayout::ePresentSrcKHR:
            return vk::AccessFlagBits::eNone;
        default:
            return vk::AccessFlagBits::eNone;
    }
}

vk::PipelineStageFlags stageFlagsFromBufferUsage(vk::BufferUsageFlags usage) {
    vk::PipelineStageFlags flags;
    if (usage & vk::BufferUsageFlagBits::eVertexBuffer)
        flags |= vk::PipelineStageFlagBits::eVertexInput;
    if (usage & vk::BufferUsageFlagBits::eIndexBuffer)
        flags |= vk::PipelineStageFlagBits::eVertexInput;
    if (usage & vk::BufferUsageFlagBits::eUniformBuffer)
        flags |= vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
    if (usage & vk::BufferUsageFlagBits::eStorageBuffer)
        flags |= vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
    if (usage & vk::BufferUsageFlagBits::eTransferSrc)
        flags |= vk::PipelineStageFlagBits::eTransfer;
    if (usage & vk::BufferUsageFlagBits::eTransferDst)
        flags |= vk::PipelineStageFlagBits::eTransfer;
    return flags;
}

vk::PipelineStageFlags stageFlagsFromLayout(vk::ImageLayout layout) {
    switch (layout) {
        case vk::ImageLayout::eUndefined:
        case vk::ImageLayout::eGeneral:
        case vk::ImageLayout::ePreinitialized:
            return vk::PipelineStageFlagBits::eTopOfPipe;
        case vk::ImageLayout::eColorAttachmentOptimal:
            return vk::PipelineStageFlagBits::eColorAttachmentOutput;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return vk::PipelineStageFlagBits::eFragmentShader;
        case vk::ImageLayout::eTransferSrcOptimal:
            return vk::PipelineStageFlagBits::eTransfer;
        case vk::ImageLayout::eTransferDstOptimal:
            return vk::PipelineStageFlagBits::eTransfer;
        case vk::ImageLayout::ePresentSrcKHR:
            return vk::PipelineStageFlagBits::eBottomOfPipe;
        default:
            return vk::PipelineStageFlagBits::eTopOfPipe;
    }
}

void O5MRendergraph::addResource(const std::string& name, vk::Format format, vk::Extent2D extent,
                                 vk::ImageUsageFlags usage, vk::ImageLayout initialLayout,
                                 vk::ImageLayout finalLayout) {
    ResourceDesc resourceDesc;
    resourceDesc.name = name;
    resourceDesc.format = format;
    resourceDesc.extent = extent;
    resourceDesc.usage = usage;
    resourceDesc.initialLayout = initialLayout;
    resourceDesc.finalLayout = finalLayout;

    m_resources[name] = resourceDesc;
}

void O5MRendergraph::addBufferResource(const std::string& name, vk::DeviceSize size,
                                       vk::BufferUsageFlags usage,
                                       vk::MemoryPropertyFlags memoryProperties) {
    ResourceDesc desc;
    desc.name = name;
    desc.kind = ResourceKind::Buffer;
    desc.size = size;
    desc.bufferUsage = usage;
    desc.memoryProperties = memoryProperties;

    m_resources[name] = desc;
}

void O5MRendergraph::addPass(const std::string& name, const std::vector<std::string>& inputs,
                             const std::vector<std::string>& outputs,
                             std::function<void(vk::raii::CommandBuffer&)> executeFunc) {
    PassDesc pass;
    pass.name = name;
    pass.inputs = inputs;
    pass.outputs = outputs;
    pass.executeFuct = executeFunc;

    m_passDescs.push_back(pass);
}

//single writer assumption
void O5MRendergraph::compile(void) {
    //TODO: Build a dependency graph
    //TODO: Topological sort to determine execution order
    //TODO: Allocate semaphores for synchronization
    std::vector<std::vector<size_t>> dependencies(m_passDescs.size());
    std::vector<std::vector<size_t>> dependents(m_passDescs.size());

    std::unordered_map<std::string, size_t> resourceWriters;

    for (size_t i = 0; i < m_passDescs.size(); ++i) {
        const auto& pass = m_passDescs[i];

        for (const auto& input : pass.inputs) {
            auto it = resourceWriters.find(input);
            if (it != resourceWriters.end()) {
                dependents[it->second].push_back(i);
                dependencies[i].push_back(it->second);
            }
        }

        for (const auto& output : pass.outputs) {
            //single writer validation
            if (resourceWriters.find(output) != resourceWriters.end()) {
                throw std::runtime_error("Multiple writer at resource: " + output );
            }
            resourceWriters[output] = i;
        }
    }

    std::vector<bool> visited(m_passDescs.size(), false);
    for (size_t i = 0; i < m_passDescs.size(); ++i) {
        for (size_t j = 0; j < m_passDescs.size(); ++j) {
            if (!visited[j] && 
                std::none_of(dependencies[j].begin(), dependencies[j].end(), 
                    [&](size_t dep) { return !visited[dep]; })) {
                m_executionOrder.push_back(j);
                visited[j] = true;
            }
        }
    } 


    for (auto& [name, resource] : m_resources) {
        if (resource.kind == ResourceKind::Buffer) {
            std::tie(resource.buffer, resource.memory) = m_device.createBuffer(
                resource.size, resource.bufferUsage, resource.memoryProperties);

            continue;
        }

        std::tie(resource.image, resource.memory) = m_device.createImage2D(
            resource.format, resource.extent, 1, vk::ImageTiling::eOptimal,
            resource.usage, vk::MemoryPropertyFlagBits::eDeviceLocal);

        vk::ImageViewCreateInfo imageViewInfo;
        imageViewInfo.setImage(resource.image)
                     .setViewType(vk::ImageViewType::e2D)
                     .setFormat(resource.format)
                     .setSubresourceRange({aspectFromFormat(resource.format), 0, 1, 0, 1});

        resource.imageView = m_device.getDevice().createImageView(imageViewInfo);
    }
}

void O5MRendergraph::execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue,
                             vk::raii::Fence* fence) {
    std::vector<vk::CommandBuffer> commandBuffers;
    std::unordered_map<std::string, vk::ImageLayout> resourceLayoutStates;
    std::unordered_map<std::string, std::pair<vk::AccessFlags, vk::PipelineStageFlags>> bufferLastStates;

    for (auto& [name, resource] : m_resources) {
        resourceLayoutStates[name] = resource.initialLayout;
        bufferLastStates[name] = {vk::AccessFlagBits::eHostWrite, vk::PipelineStageFlagBits::eHost};
    }

    commandBuffer.begin({});
    for (auto passIdx : m_executionOrder) {
        const auto& pass = m_passDescs[passIdx];

        for (const auto& input : pass.inputs) {
            auto& resource = m_resources[input];

            if (resource.kind == ResourceKind::Buffer) {
                vk::BufferMemoryBarrier bufBarrier{
                    .srcAccessMask = bufferLastStates[resource.name].first,
                    .dstAccessMask = accessFlagFromBufferUsage(resource.bufferUsage, true),
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = *resource.buffer,
                    .size = VK_WHOLE_SIZE,
                };

                commandBuffer.pipelineBarrier(
                    bufferLastStates[resource.name].second,
                    stageFlagsFromBufferUsage(resource.bufferUsage),
                    vk::DependencyFlagBits::eByRegion,
                    {}, { bufBarrier }, {}
                );

                bufferLastStates[resource.name] = { accessFlagFromBufferUsage(resource.bufferUsage, true), stageFlagsFromBufferUsage(resource.bufferUsage)};

                continue;
            }

            const vk::ImageLayout readLayout = readLayoutFromUsage(resource.usage, resource.format);

            if (resourceLayoutStates[input] == readLayout)
                continue;

            vk::ImageMemoryBarrier barrier;
            barrier.setOldLayout(resourceLayoutStates[input])
                   .setNewLayout(readLayout)
                   .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                   .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                   .setImage(*resource.image)
                   .setSubresourceRange({aspectFromFormat(resource.format), 0, 1, 0, 1})
                   .setSrcAccessMask(accessFlagsFromLayout(resourceLayoutStates[input]))
                   .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

            commandBuffer.pipelineBarrier(
                stageFlagsFromLayout(resourceLayoutStates[input]),
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::DependencyFlagBits::eByRegion,
                {}, {}, { barrier }
            );

            resourceLayoutStates[input] = readLayout;
        }

        for (const auto& output : pass.outputs) {
            auto& resource = m_resources[output];

            if (resource.kind == ResourceKind::Buffer) {
                vk::BufferMemoryBarrier bufBarrier{
                    .srcAccessMask = bufferLastStates[resource.name].first,
                    .dstAccessMask = accessFlagFromBufferUsage(resource.bufferUsage, false),
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = *resource.buffer,
                    .size = VK_WHOLE_SIZE,
                };

                commandBuffer.pipelineBarrier(
                    bufferLastStates[resource.name].second,
                    stageFlagsFromBufferUsage(resource.bufferUsage),
                    vk::DependencyFlagBits::eByRegion,
                    {}, { bufBarrier }, {}
                );

                bufferLastStates[resource.name] = { accessFlagFromBufferUsage(resource.bufferUsage, false), stageFlagsFromBufferUsage(resource.bufferUsage)};
                continue;
            }
            auto& format = resource.format;

            vk::ImageMemoryBarrier barrier;
            barrier.setOldLayout(resourceLayoutStates[output])
                   .setNewLayout(layoutFromUsage(resource.usage, resource.format))
                   .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                   .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                   .setImage(*resource.image)
                   .setSubresourceRange({aspectFromFormat(resource.format), 0, 1, 0, 1})
                   .setSrcAccessMask(accessFlagsFromLayout(resourceLayoutStates[output]))
                   .setDstAccessMask(accessFlagsFromLayout(layoutFromUsage(resource.usage, resource.format)));

            commandBuffer.pipelineBarrier(
                stageFlagsFromLayout(resourceLayoutStates[output]),
                stageFlagsFromLayout(layoutFromUsage(resource.usage, resource.format)),
                vk::DependencyFlagBits::eByRegion,
                {}, {}, { barrier }
            );

            resourceLayoutStates[output] = layoutFromUsage(resource.usage, resource.format);
        }

        pass.executeFuct(commandBuffer);

        for (const auto& output : pass.outputs) {
            auto& resource = m_resources[output];

            if (resource.kind == ResourceKind::Buffer) {
                vk::BufferMemoryBarrier barrier; 
                barrier.setSrcAccessMask(bufferLastStates[resource.name].first)
                       .setDstAccessMask(accessFlagFromBufferUsage(resource.bufferUsage, true))
                       .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                       .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                       .setBuffer(*resource.buffer)
                       .setSize(VK_WHOLE_SIZE);                

                commandBuffer.pipelineBarrier(
                    bufferLastStates[resource.name].second,
                    stageFlagsFromBufferUsage(resource.bufferUsage),
                    vk::DependencyFlagBits::eByRegion,
                    {}, { barrier }, {}
                );

                bufferLastStates[resource.name] = { accessFlagFromBufferUsage(resource.bufferUsage, true), stageFlagsFromBufferUsage(resource.bufferUsage)};
                continue;
            }

            vk::ImageMemoryBarrier barrier;
            barrier.setOldLayout(resourceLayoutStates[output])
                   .setNewLayout(resource.finalLayout)
                   .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                   .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                   .setImage(*resource.image)
                   .setSubresourceRange({aspectFromFormat(resource.format), 0, 1, 0, 1})
                   .setSrcAccessMask(accessFlagsFromLayout(resourceLayoutStates[output]))
                   .setDstAccessMask(accessFlagsFromLayout(resource.finalLayout));

            commandBuffer.pipelineBarrier(
                stageFlagsFromLayout(resourceLayoutStates[output]),
                vk::PipelineStageFlagBits::eAllCommands,
                vk::DependencyFlagBits::eByRegion,
                {}, {}, { barrier }
            );

            resourceLayoutStates[output] = resource.finalLayout;
        }
    }

    commandBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*commandBuffer);

    //TODO Fence wait modification for cpu side
    queue.submit(submitInfo, fence ? **fence : vk::Fence{ nullptr });
}
*/

enum class EdgeKind { RAW, WAR, /*WAW*/ }; 
//due to the single-writer restriction, WAW is unavailable for now

struct Node {
    uint32_t index; //index into m_passDescs
    std::vector<uint32_t> RAW;   
    std::vector<uint32_t> WAR;
    std::vector<uint32_t> WAW;

    int inDegree = 0;
};

void O5MRendergraph::compile(void) {
    std::unordered_map<uint32_t, uint32_t> resourceWriters;              //resource handle -> writer pass index
    std::unordered_map<uint32_t, std::vector<uint32_t>> resourceReaders; //resource handle -> reader pass indices

    //Resource writer detect
    for (uint32_t passIdx = 0; passIdx < m_passDescs.size(); ++passIdx) {
        const auto& pass = m_passDescs[passIdx];
        for (const auto& write : pass.writes) {
            resourceWriters[write.handle] = passIdx;
        }
        for (const auto& read : pass.reads) {
            resourceReaders[read.handle].push_back(passIdx);
        }
    }

    //Pass dependencies grpah
    std::unordered_map<uint32_t, Node> nodes;
    for (uint32_t passIdx = 0; passIdx < m_passDescs.size(); ++passIdx) {
        const auto& pass = m_passDescs[passIdx];
        Node node;
        for (const auto& input : pass.reads) {
            auto writer = resourceWriters.find(input.handle);
            if (writer != resourceWriters.end()) {
                node.RAW.push_back(writer->second);
            }
        }
        for (const auto& output : pass.writes) {
            auto readers = resourceReaders.find(output.handle);
            if(readers != resourceReaders.end()){
                for (auto readerIdx : readers->second) {
                    if(readerIdx != passIdx){
                        node.WAR.push_back(readerIdx);
                    }
                }
            }
        }
        nodes[passIdx] = node;
    }

    //cycle detect
    std::queue<uint32_t> queue;
    std::vector<bool> visited(m_passDescs.size(), false);
    queue.push(0);
    while (!queue.empty()) {
        auto idx = queue.front();
        queue.pop();
        visited[idx] = true;

        for (auto dep : nodes[idx].RAW) {
            if (!visited[dep]) 
                queue.push(dep);
            else
                throw std::runtime_error("Cycle detected in render graph");
        }
        for (auto dep : nodes[idx].WAR) {
            if (!visited[dep]) 
                queue.push(dep);
            else
                throw std::runtime_error("Cycle detected in render graph");
        }
        for (auto dep : nodes[idx].WAW) {
            if (!visited[dep]) 
                queue.push(dep);
            else
                throw std::runtime_error("Cycle detected in render graph");
        }
    }

    //Kahn sort
    m_executionOrder.clear();

    //count the inDegree
    for (auto& [idx, node] : nodes) {
        for (auto dep : node.RAW) {
            node.inDegree++;
        }
        for (auto dep : node.WAR) {
            node.inDegree++;
        }
    }

    std::queue<uint32_t> readyQueue;
    for (auto& [idx, node] : nodes) {
        if (node.inDegree == 0) {
            readyQueue.push(idx);
        }
    }

    while (!readyQueue.empty()) {
        uint32_t idx = readyQueue.front();
        readyQueue.pop();
        m_executionOrder.push_back(idx);

        for (auto dep : nodes[idx].RAW) {
            nodes[dep].inDegree--;
            if (nodes[dep].inDegree == 0) {
                readyQueue.push(dep);
            }
        }
        for (auto dep : nodes[idx].WAR) {
            nodes[dep].inDegree--;
            if (nodes[dep].inDegree == 0) {
                readyQueue.push(dep);
            }
        }
    }

    //resource lifecycle management& usage convertationk
    for (size_t i = 0; i < m_executionOrder.size(); i++) {
        auto& pass = m_passDescs[m_executionOrder[i]];
        for (auto read : pass.reads) {
            //firstUse/lastUse
            auto& info = m_resourceInfos.at(read.handle);
            if (info.firstUse == UINT32_MAX)
                info.firstUse = m_executionOrder[i];
            info.lastUse = m_executionOrder[i];

            ResourceKind k = declareKind(read.use);
            switch (k) {
                case ResourceKind::Buffer: 
                    std::get<BufferInfo>(info.info).usage |= toBufferUsage(read.use);
                    break;
                case ResourceKind::Image:
                    std::get<ImageInfo>(info.info).usage |= toImageUsage(read.use);
                    break;
            }
        }

        for (auto write: pass.writes) {
            //firstUse/lastUse
            auto& info = m_resourceInfos.at(write.handle);
            if (info.firstUse == UINT32_MAX)
                info.firstUse = m_executionOrder[i];
            info.lastUse = m_executionOrder[i];

            ResourceKind k = declareKind(write.use);
            switch (k) {
                case ResourceKind::Buffer: 
                    std::get<BufferInfo>(info.info).usage |= toBufferUsage(write.use);
                    break;
                case ResourceKind::Image:
                    std::get<ImageInfo>(info.info).usage |= toImageUsage(write.use);
                    break;
            }
        }

        //if it is read&wirte, it must be a imageBuffer
        for (auto readWrite : pass.readWrites) {
            auto& info = m_resourceInfos.at(readWrite.handle);
            if (info.firstUse == UINT32_MAX)
                info.firstUse = m_executionOrder[i];
            info.lastUse = m_executionOrder[i];

            std::get<ImageInfo>(info.info).usage |= toImageUsage(readWrite.use);
        }
    }
    
    //initalize physical resources
    for (const auto& [handle, resourceInfo] : m_resourceInfos) {
        ResourceKind kind = resourceInfo.kind;
        PhysicalResource physicalResource;
        switch (kind) {
            case ResourceKind::Buffer: 
                std::tie(physicalResource.buffer, physicalResource.memory) =
                    m_device.createBuffer(std::get<BufferInfo>(resourceInfo.info).size,
                        std::get<BufferInfo>(resourceInfo.info).usage,
                        vk::MemoryPropertyFlagBits::eDeviceLocal);
                break;
            case ResourceKind::Image:
                std::tie(physicalResource.image, physicalResource.memory) =
                    m_device.createImage2D(std::get<ImageInfo>(resourceInfo.info).format,
                        std::get<ImageInfo>(resourceInfo.info).extent, 1,
                        vk::ImageTiling::eOptimal, std::get<ImageInfo>(resourceInfo.info).usage,
                        vk::MemoryPropertyFlagBits::eDeviceLocal);

                //createImageView
                physicalResource.view = m_device.createImageView2D(physicalResource.image,
                        std::get<ImageInfo>(resourceInfo.info).format);
                break;
        }
    }

    //barrier configuration
    std::unordered_map<ResourceHandle, SyncScope> stateRecords;
    for (auto& [handle, info] : m_resourceInfos) {
        stateRecords[handle] = SyncScope{
            .stages = vk::PipelineStageFlagBits::eNone,
            .access = vk::AccessFlagBits::eNone,
            .layout = vk::ImageLayout::eUndefined
        };
    }
    for (auto passIdx : m_executionOrder) {
        auto& pass = m_passDescs[passIdx];

        auto barrierConstrution = [&](const UseDecl& use) {
            SyncScope lastState = stateRecords[use.handle]; 
            SyncScope currentState = fromUseToSyncScope(pass.kind, use.use);

            pass.compiled.enterBarrier.emplace_back(use.handle, lastState, currentState);

            stateRecords[use.handle] = currentState;
        };

        auto constructBarrier = [&] (const UseDecl& use) {
            SyncScope lastState, currentState;
        };

        std::for_each(pass.reads.begin(), pass.reads.end(), barrierConstrution);
        std::for_each(pass.writes.begin(), pass.writes.end(), barrierConstrution);
        std::for_each(pass.readWrites.begin(), pass.readWrites.end(), barrierConstrution);

        //wondering what we should do with aspect&queue
    }
}
void O5MRendergraph::execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue, vk::raii::Fence* fence) {
    std::vector<vk::raii::CommandBuffer> commandBuffers;
    commandBuffer.begin({});

    for (auto passIdx : m_executionOrder) {
        const auto& pass = m_passDescs[passIdx];

        auto emitBarrier = [&](const BarrierState barrierState) {
            PhysicalResource& resource = m_physicalResources[barrierState.handle];
            switch (resource.kind) {
                case ResourceKind::Buffer: {
                    vk::BufferMemoryBarrier barrier {
                        .srcAccessMask = barrierState.from.access,
                        .dstAccessMask = barrierState.to.access,
                        .buffer = resource.buffer,
                        .size = vk::WholeSize,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    };
                    commandBuffer.pipelineBarrier(barrierState.from.stages, barrierState.to.stages, {}, {}, barrier, {});
                    break;
                }

                case ResourceKind::Image: {
                    vk::ImageMemoryBarrier ImageBarrier {
                        .srcAccessMask = barrierState.from.access,
                        .dstAccessMask = barrierState.to.access,
                        .image = resource.image,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .subresourceRange = {
                            .aspectMask = O5MDevice::aspectFromFormat(std::get<ImageInfo>(m_resourceInfos[barrierState.handle].info).format),
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1
                        }
                    };
                    commandBuffer.pipelineBarrier(barrierState.from.stages, barrierState.to.stages, {}, nullptr, nullptr, ImageBarrier);
                    break;
                }
            }
        };

        std::for_each(pass.compiled.enterBarrier.begin(), pass.compiled.enterBarrier.end(), emitBarrier);//iterator to set up barrier
        O5MRenderContext ctx(pass, m_physicalResources);                            
        pass.executeFunc(ctx, commandBuffer);
        //I think this is finished right here
        //Gosh my embedding server dead 
    }
}

