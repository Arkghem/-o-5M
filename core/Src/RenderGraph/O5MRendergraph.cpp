#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "RenderGraph/O5MRendergraph.h"

#include <algorithm>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_format_traits.hpp>

ResourceHandle O5MRendergraph::importTexture(
    std::string debugName,
    vk::Image image,
    vk::ImageView view,
    vk::Format format,
    vk::Extent2D extent,
    vk::ImageUsageFlags actualUsage,
    vk::ImageLayout currentLayout
) {
    ResourceInfo info(debugName, extent, format, ResourceSource::Imported);
    std::get<ImageInfo>(info.info).usage = actualUsage;
    std::get<ImageInfo>(info.info).initalLayout = currentLayout;
    addResourceInfo(info);

    PhysicalResource physical = {
        .kind = ResourceKind::Image,
        .source = ResourceSource::Imported,
        .resource = PhysicalResource::ImportedResource {
            .image = { image, nullptr },
            .view = { view, nullptr }
        }
    };

    m_physicalResources.emplace(info.handle, std::move(physical));

    return info.handle;
}

ResourceHandle O5MRendergraph::importBuffer(
    std::string debugName,
    vk::Buffer buffer,
    vk::DeviceSize size,
    vk::BufferUsageFlags actualUsage
) {
    ResourceInfo info(debugName, size, ResourceSource::Imported);
    std::get<BufferInfo>(info.info).usage = actualUsage;
    addResourceInfo(info);

    PhysicalResource physical = {
        .kind = ResourceKind::Buffer,
        .source = ResourceSource::Imported,
        .resource = PhysicalResource::ImportedResource {
            .buffer = { buffer, nullptr }
        }
    };

    m_physicalResources.emplace(info.handle, std::move(physical));

    return info.handle;
}

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

    std::vector<uint32_t> rootPasses;

    //Resource writer detect
    for (uint32_t passIdx = 0; passIdx < m_passDescs.size(); ++passIdx) {
        const auto& pass = m_passDescs[passIdx];
        for (const auto& write : pass.writes) {
            resourceWriters[write.handle] = passIdx;

            //root passes detection
            if (std::ranges::any_of(m_outputResources.begin(), m_outputResources.end(), [&write](ResourceHandle handle) { return handle == write.handle; })) {
                rootPasses.push_back(passIdx);
            }
        }
        //readHistory member won't be included
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

    //TODO: WAW support needed here. single-writer restriction will be deprecated in phase 3.
    //Kahn sort
    m_executionOrder.clear();

    // Edge direction: node.RAW lists this pass's dependencies (writers it reads
    // from, incoming edges). node.WAR lists its dependents (readers of this
    // pass's outputs, outgoing edges). Only incoming edges feed in-degree.
    for (auto& [idx, node] : nodes) {
        node.inDegree = static_cast<int>(node.RAW.size());
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

        // scheduling idx releases its dependents (readers of its outputs)
        for (auto dependent : nodes[idx].WAR) {
            if (--nodes[dependent].inDegree == 0) {
                readyQueue.push(dependent);
            }
        }
    }

    if (m_executionOrder.size() != m_passDescs.size()) {
        // Nodes stuck with inDegree > 0 sit on or downstream of a cycle.
        // Strip sources and sinks repeatedly within the unscheduled set:
        // whatever survives has both an incoming and an outgoing edge among
        // the survivors, i.e. it lies on a cycle. Report those pass names.
        std::vector<uint32_t> remaining;
        for (uint32_t i = 0; i < m_passDescs.size(); ++i) {
            if (std::find(m_executionOrder.begin(), m_executionOrder.end(), i) ==
                m_executionOrder.end()) {
                remaining.push_back(i);
            }
        }
        bool stripped = true;
        while (stripped) {
            stripped = false;
            std::erase_if(remaining, [&](uint32_t i) {
                const auto& node = nodes[i];
                auto inRemaining = [&remaining](uint32_t other) {
                    return std::find(remaining.begin(), remaining.end(), other) !=
                           remaining.end();
                };
                const bool hasIn = std::any_of(node.RAW.begin(), node.RAW.end(), inRemaining);
                const bool hasOut = std::any_of(node.WAR.begin(), node.WAR.end(), inRemaining);
                if (!hasIn || !hasOut) {
                    stripped = true;
                    return true;
                }
                return false;
            });
        }

        std::string names;
        for (uint32_t i : remaining)
            names += (names.empty() ? "" : " -> ") + m_passDescs[i].debugName;
        throw std::runtime_error(
            "Cycle detected in render graph, passes on cycle: [" + names + "]");
    }

    //unvisited pass culling
    //BST with root passes;
    std::vector<bool> culled(m_passDescs.size(), true);
    std::queue<uint32_t> bfs;
    for (auto passIdx : rootPasses) {
        bfs.push(passIdx);
        culled[passIdx] = false;
    }
    while (!bfs.empty()) {
        auto idx = bfs.front();
        bfs.pop();
        // walk upstream only: RAW lists the writers that produce this pass's
        // inputs; WAR would walk downstream consumers, which don't contribute.
        for (auto dep : nodes[idx].RAW) {
            if (culled[dep]) {
                culled[dep] = false;
                bfs.push(dep);
            }
        }
    }

    //remove culled passes
    std::erase_if(m_executionOrder, [&](uint32_t idx) { return culled[idx];});

    //resource lifecycle management& usage convertation
    for (size_t i = 0; i < m_executionOrder.size(); i++) {
        auto& pass = m_passDescs[m_executionOrder[i]];
        for (auto read : pass.reads) {
            //firstUse/lastUse
            auto& info = m_resourceInfos.at(read.handle);
            if (info.firstUse == UINT32_MAX)
                info.firstUse = m_executionOrder[i];
            info.lastUse = m_executionOrder[i];

            ResourceKind k = declareKind(read.use);
            if(info.source == ResourceSource::Created) {
                //Add UsageBit for Created resource
                switch (k) {
                    case ResourceKind::Buffer: 
                        std::get<BufferInfo>(info.info).usage |= toBufferUsage(read.use);
                        break;
                    case ResourceKind::Image:
                        std::get<ImageInfo>(info.info).usage |= toImageUsage(read.use);
                        break;
                }
            } else if (info.source == ResourceSource::Imported) {
                //Verify UsageBit for Imported resource
                switch (k) {
                    case ResourceKind::Buffer:
                        if (!(std::get<BufferInfo>(info.info).usage & toBufferUsage(read.use)))
                            throw std::runtime_error("Imported resource '" + info.debugName +
                                "' does not support the requested usage (pass '" +
                                pass.debugName + "')");
                        break;
                    case ResourceKind::Image:
                        if (!(std::get<ImageInfo>(info.info).usage & toImageUsage(read.use)))
                            throw std::runtime_error("Imported resource '" + info.debugName +
                                "' does not support the requested usage (pass '" +
                                pass.debugName + "')");
                        break;
                }
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

        //Im not sure about all this.
        for (auto readHistory : pass.readHistorys){
            //firstUse/lastUse
            auto& info = m_resourceInfos.at(readHistory.handle);
             if (info.firstUse == UINT32_MAX)
                info.firstUse = m_executionOrder[i];
           info.lastUse = m_executionOrder[i];

           ResourceKind k = declareKind(readHistory.use);
           switch (k) {
                case ResourceKind::Buffer: 
                    std::get<BufferInfo>(info.info).usage |= toBufferUsage(readHistory.use);
                    break;
                case ResourceKind::Image:
                    std::get<ImageInfo>(info.info).usage |= toImageUsage(readHistory.use);
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
    
    // history ping-pong: any resource with a readHistory declaration gets a
    // second physical instance. Imported resources cannot be duplicated
    // (the graph does not own them), so history on imports is rejected here.
    for (const auto& pass : m_passDescs) {
        for (const auto& h : pass.readHistorys) {
            auto& info = m_resourceInfos.at(h.handle);
            if (info.source == ResourceSource::Imported)
                throw std::runtime_error(
                    "readHistory on imported resource is not supported: " + info.debugName);
            info.hasHistory = true;
        }
    }

    //initalize physical resources (created resources only; imported ones
    //already have their physical entry from importTexture/importBuffer)
    for (const auto& [handle, resourceInfo] : m_resourceInfos) {
        //continue when the resource is never used.
        if(resourceInfo.firstUse == UINT32_MAX) 
            continue;

        if (resourceInfo.source == ResourceSource::Imported)
            continue;

        PhysicalResource physicalResource;
        physicalResource.hasHistory = resourceInfo.hasHistory;
        physicalResource.kind = resourceInfo.kind;
        physicalResource.source = ResourceSource::Created;
        physicalResource.resource = PhysicalResource::OwnedResource();
        auto& owned = std::get<PhysicalResource::OwnedResource>(physicalResource.resource);

        // hasHistory -> create TWO instances (ping-pong), else one
        const uint32_t instanceCount = resourceInfo.hasHistory ? 2 : 1;
        for (uint32_t inst = 0; inst < instanceCount; ++inst) {
            switch (resourceInfo.kind) {
                case ResourceKind::Buffer:
                    std::tie(owned.buffer[inst], owned.memory[inst]) =
                        m_device.createBuffer(std::get<BufferInfo>(resourceInfo.info).size,
                            std::get<BufferInfo>(resourceInfo.info).usage,
                            vk::MemoryPropertyFlagBits::eDeviceLocal);
                    break;
                case ResourceKind::Image:
                    std::tie(owned.image[inst], owned.memory[inst]) =
                        m_device.createImage2D(std::get<ImageInfo>(resourceInfo.info).format,
                            std::get<ImageInfo>(resourceInfo.info).extent, 1,
                            vk::ImageTiling::eOptimal, std::get<ImageInfo>(resourceInfo.info).usage,
                            vk::MemoryPropertyFlagBits::eDeviceLocal);

                    //createImageView
                    owned.view[inst] = m_device.createImageView2D(owned.image[inst],
                           std::get<ImageInfo>(resourceInfo.info).format);
                    break;
            }
        }
        m_physicalResources.emplace(handle, std::move(physicalResource));
    }

    //barrier configuration: TWO-ROUND SIMULATION.
    //
    // History reads must be barriered against "the state the previous frame
    // left that ping-pong instance in" -- a cross-frame dependency that does
    // not exist at compile time. But the frame timeline is periodic: after a
    // cold start, every frame repeats the same per-instance state cycle. So
    // we simulate the schedule twice and bake two plans:
    //   round 0: starts from the initial states (eUndefined / imported layout)
    //            -> firstFrameBarrier, replayed for frameIndex == 0
    //   round 1: continues from round 0's end states
    //            -> steadyBarrier, replayed for every frameIndex >= 1
    // Instance selection inside a round r: regular uses touch instance r%2,
    // history reads touch instance (r+1)%2 (the one the "previous" round
    // produced). Non-history resources only have slot 0, so both rounds and
    // both parities resolve to the same instance.
    m_stateRecords.clear();
    for (auto& [handle, info] : m_resourceInfos) {
        // only images have a layout; buffers start with no layout state.
        // eTopOfPipe: old-style pipelineBarrier requires a non-zero srcStageMask.
        const vk::ImageLayout initialLayout =
            info.kind == ResourceKind::Image
                ? std::get<ImageInfo>(info.info).initalLayout
                : vk::ImageLayout::eUndefined;
        m_stateRecords[handle].fill(SyncScope{
            .stages = vk::PipelineStageFlagBits::eTopOfPipe,
            .access = vk::AccessFlagBits::eNone,
            .layout = initialLayout
        });
    }

    for (uint32_t round = 0; round < 2; ++round) {
        for (auto passIdx : m_executionOrder) {
            auto& pass = m_passDescs[passIdx];
            auto& plan = round == 0 ? pass.compiled.firstFrameBarrier
                                    : pass.compiled.steadyBarrier;

            auto bakeBarriers = [&](const std::vector<UseDecl>& uses, bool historySide) {
                for (const auto& use : uses) {
                    const uint32_t instance =
                        m_physicalResources.at(use.handle).slot(
                            historySide ? (round + 1) % 2 : round % 2);
                    SyncScope& state = m_stateRecords[use.handle][instance];
                    SyncScope next = fromUseToSyncScope(pass.kind, use.use);

                    plan.push_back(BarrierState{ use.handle, state, next, historySide });

                    state = next;
                }
            };

            bakeBarriers(pass.reads, false);
            bakeBarriers(pass.writes, false);
            bakeBarriers(pass.readWrites, false);
            // history reads do not join the frame-internal dependency graph,
            // but they still need barriers on the OTHER instance
            bakeBarriers(pass.readHistorys, true);
        }
    }
}

void O5MRendergraph::execute(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue,
                             vk::raii::Fence* fence, uint32_t frameIndex) {
    commandBuffer.begin({});

    for (auto passIdx : m_executionOrder) {
        const auto& pass = m_passDescs[passIdx];
        // replay: frame 0 = cold-start plan, later frames = steady plan
        const auto& plan = frameIndex == 0 ? pass.compiled.firstFrameBarrier
                                           : pass.compiled.steadyBarrier;

        auto emitBarrier = [&](const BarrierState barrierState) {
            PhysicalResource& resource = m_physicalResources[barrierState.handle];
            // history-side barriers act on the instance the previous frame
            // produced: (frameIndex+1)%2; regular ones on frameIndex%2
            const uint32_t instance = resource.slot(
                barrierState.historySide ? (frameIndex + 1) % 2 : frameIndex % 2);
            switch (resource.kind) {
                case ResourceKind::Buffer: {
                    vk::BufferMemoryBarrier barrier {
                        .srcAccessMask = barrierState.from.access,
                        .dstAccessMask = barrierState.to.access,
                        .buffer = resource.getBuffer(instance),
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
                        .oldLayout = barrierState.from.layout,
                        .newLayout = barrierState.to.layout,
                        .image = resource.getImage(instance),
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .subresourceRange = {
                            .aspectMask = O5MDevice::aspectFromFormat(std::get<ImageInfo>(m_resourceInfos.at(barrierState.handle).info).format),
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

        std::for_each(plan.begin(), plan.end(), emitBarrier);
        O5MRenderContext ctx(pass, m_physicalResources, static_cast<int>(frameIndex));
        pass.executeFunc(ctx, commandBuffer);
    }

    commandBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*commandBuffer);

    //TODO Fence wait modification for cpu side
    queue.submit(submitInfo, fence ? **fence : vk::Fence{ nullptr });
}

// below 100% generated by AI, zero-human added

// ---------------------------------------------------------------------------
// dump(): debug-only printout of the compile result (see phase1 design doc):
//   1. execution order with per-pass resource declarations,
//   2. resource lifecycle table (firstUse/lastUse + final usage),
//   3. enter barriers as recorded by compile().
// Read-only: never mutates the graph, safe to call before/after execute().
// ---------------------------------------------------------------------------

namespace {

// Human-readable name for one usage enum value (debug output only).
template <typename E>
std::string_view useEnumName(E value) {
    if constexpr (std::is_same_v<E, TexRead>) {
        switch (value) {
            case TexRead::Sampled:     return "TexRead::Sampled";
            case TexRead::Storage:     return "TexRead::Storage";
            case TexRead::TransferSrc: return "TexRead::TransferSrc";
        }
    } else if constexpr (std::is_same_v<E, TexWrite>) {
        switch (value) {
            case TexWrite::ColorClear:  return "TexWrite::ColorClear";
            case TexWrite::ColorStore:  return "TexWrite::ColorStore";
            case TexWrite::Depth:       return "TexWrite::Depth";
            case TexWrite::Storage:     return "TexWrite::Storage";
            case TexWrite::TransferDst: return "TexWrite::TransferDst";
        }
    } else if constexpr (std::is_same_v<E, TexRW>) {
        switch (value) {
            case TexRW::Storage: return "TexRW::Storage";
        }
    } else if constexpr (std::is_same_v<E, BufRead>) {
        switch (value) {
            case BufRead::Uniform:      return "BufRead::Uniform";
            case BufRead::Storage:      return "BufRead::Storage";
            case BufRead::VertexIndex:  return "BufRead::VertexIndex";
            case BufRead::Indirect:     return "BufRead::Indirect";
            case BufRead::TransferSrc:  return "BufRead::TransferSrc";
        }
    } else if constexpr (std::is_same_v<E, BufWrite>) {
        switch (value) {
            case BufWrite::Storage:     return "BufWrite::Storage";
            case BufWrite::TransferDst: return "BufWrite::TransferDst";
            case BufWrite::Uniform:     return "BufWrite::Uniform";
        }
    }
    return "unknown";
}

std::string useToString(
    const std::variant<TexRead, TexWrite, TexRW, BufRead, BufWrite>& use) {
    return std::visit(
        [](const auto& value) { return std::string{useEnumName(value)}; }, use);
}

const char* passKindToString(PassKind kind) {
    switch (kind) {
        case PassKind::Graphic: return "Graphic";
        case PassKind::Compute: return "Compute";
    }
    return "unknown";
}

} // namespace

void O5MRendergraph::dump(void) const {
    std::cout << "==== O5MRendergraph dump ====\n"
              << "passes declared: " << m_passDescs.size()
              << ", scheduled: " << m_executionOrder.size()
              << ", resources declared: " << m_resourceInfos.size()
              << ", physical resources: " << m_physicalResources.size() << "\n";

    // A pass missing from the execution order means dead pass or a
    // topological sort that could not satisfy its dependencies.
    if (m_executionOrder.size() != m_passDescs.size()) {
        std::cout << "!! " << (m_passDescs.size() - m_executionOrder.size())
                  << " pass(es) never scheduled\n";
    }

    // Pass index -> slot in the execution order (for readable lifetimes).
    std::unordered_map<uint32_t, size_t> scheduleSlot;
    for (size_t slot = 0; slot < m_executionOrder.size(); ++slot)
        scheduleSlot[m_executionOrder[slot]] = slot;

    const auto resourceName = [this](ResourceHandle handle) -> std::string {
        auto it = m_resourceInfos.find(handle);
        return it != m_resourceInfos.end()
                   ? it->second.debugName
                   : "<unknown handle " + std::to_string(handle) + ">";
    };

    // 1) execution order + declarations + barriers
    std::cout << "\n-- execution order --\n";
    for (size_t slot = 0; slot < m_executionOrder.size(); ++slot) {
        const uint32_t passIdx = m_executionOrder[slot];
        const auto& pass = m_passDescs.at(passIdx);

        std::cout << "  [" << slot << "] pass#" << passIdx << " ("
                  << passKindToString(pass.kind) << ") \"" << pass.debugName
                  << "\"\n";

        const auto printUses = [&](const char* label,
                                   const std::vector<UseDecl>& uses) {
            for (const auto& use : uses) {
                std::cout << "        " << label << " " << resourceName(use.handle)
                          << " (" << useToString(use.use) << ")\n";
            }
        };
        printUses("read :", pass.reads);
        printUses("write:", pass.writes);
        printUses("rw   :", pass.readWrites);
        printUses("hist :", pass.readHistorys);

        const auto printBarriers = [this, &resourceName](
                                       const char* label,
                                       const std::vector<BarrierState>& barriers) {
            for (const auto& barrier : barriers) {
                std::cout << "        " << label << " barrier " << resourceName(barrier.handle)
                          << (barrier.historySide ? " [history]" : "") << "\n"
                          << "            from { " << vk::to_string(barrier.from.stages)
                          << " | " << vk::to_string(barrier.from.access) << " | "
                          << vk::to_string(barrier.from.layout) << " }\n"
                          << "            to   { " << vk::to_string(barrier.to.stages)
                          << " | " << vk::to_string(barrier.to.access) << " | "
                          << vk::to_string(barrier.to.layout) << " }\n";
            }
        };
        printBarriers("first:", pass.compiled.firstFrameBarrier);
        printBarriers("steady", pass.compiled.steadyBarrier);
    }

    // 2) resource lifecycle table.
    // Note: compile() stores the PASS INDEX in firstUse/lastUse, we display
    // the schedule slot when that pass was actually scheduled.
    std::cout << "\n-- resource lifecycle --\n";
    for (const auto& [handle, info] : m_resourceInfos) {
        std::cout << "  #" << handle << " \"" << info.debugName << "\" ("
                  << (info.kind == ResourceKind::Image ? "Image" : "Buffer") << ") ";

        if (info.firstUse == UINT32_MAX) {
            std::cout << "never used (dead resource)";
        } else {
            const auto slotOf = [&](uint32_t passIdx) -> std::string {
                auto it = scheduleSlot.find(passIdx);
                return it != scheduleSlot.end()
                           ? ("slot " + std::to_string(it->second))
                           : ("pass " + std::to_string(passIdx) + " (unscheduled)");
            };
            std::cout << "firstUse " << slotOf(info.firstUse) << ", lastUse "
                      << slotOf(info.lastUse);
        }

        if (info.kind == ResourceKind::Image) {
            const auto& image = std::get<ImageInfo>(info.info);
            std::cout << " | " << vk::to_string(image.format) << " "
                      << image.extent.width << "x" << image.extent.height
                      << " usage " << vk::to_string(image.usage);
        } else {
            const auto& buffer = std::get<BufferInfo>(info.info);
            std::cout << " | size " << buffer.size << " usage "
                      << vk::to_string(buffer.usage);
        }
        std::cout << "\n";
    }

    std::cout << "==== end dump ====\n";
}
