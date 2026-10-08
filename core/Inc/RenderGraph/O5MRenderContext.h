#ifndef __O5MRENDERCONTEXT__H
#define __O5MRENDERCONTEXT__H

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <vector>

#include "RenderGraph/O5MRendergraphType.h"

using namespace O5MRendergraphNS;

//Context pass the reference of the vulkan resource.
//Lightweight per-pass view built at record time by O5MRendergraph::execute():
//resolves handles to physical resources, picks the ping-pong instance for
//this frame, owns nothing.
class O5MRenderContext {
private:
    std::vector<std::pair<uint32_t, PhysicalResource&>> physicalResources;
    const PassDesc& pass;
    const int frameIndex; //readHistory / ping-pong parity support

    PhysicalResource& get(uint32_t handle) const {
        for (auto [h, res] : physicalResources) {
            if (h == handle)
                return res;
        }
        throw std::runtime_error("RenderContext: unknown resource handle");
    }

    // ping-pong parity of THIS frame; PhysicalResource::slot() maps it to 0
    // for resources without history, so callers never branch
    uint32_t currentInstance(void)  const { return static_cast<uint32_t>(frameIndex) % 2; }
    uint32_t historyInstance(void) const { return (currentInstance() + 1) % 2; }
public:
    O5MRenderContext(const PassDesc& pass, std::unordered_map<ResourceHandle, PhysicalResource>& ref, int frameIndex = 0) :
        frameIndex(frameIndex),
        pass(pass) {
        for (auto read : pass.reads)
            physicalResources.push_back({read.handle, ref.at(read.handle)});
        for (auto write : pass.writes)
            physicalResources.push_back({write.handle, ref.at(write.handle)});
        for (auto readWrite : pass.readWrites)
            physicalResources.push_back({readWrite.handle, ref.at(readWrite.handle)});
        // history-only resources still need to resolve here for getHistory*()
        for (auto readHistory : pass.readHistorys)
            physicalResources.push_back({readHistory.handle, ref.at(readHistory.handle)});
    }

    // current-frame instance (the one this frame's writes target)
    vk::Buffer     getBuffer(uint32_t handle) { return get(handle).getBuffer(currentInstance()); }
    vk::Image      getImage(uint32_t handle) { return get(handle).getImage(currentInstance()); }
    vk::ImageView  getImageView(uint32_t handle) { return get(handle).getView(currentInstance()); }

    // previous-frame instance of a ping-pong resource. NOTE: on frame 0 the
    // other instance was never written -- contents are UNDEFINED; shaders
    // should guard with getFrameIndex() == 0.
    vk::Buffer     getHistoryBuffer(uint32_t handle) { return get(handle).getBuffer(historyInstance()); }
    vk::Image      getHistoryImage(uint32_t handle) { return get(handle).getImage(historyInstance()); }
    vk::ImageView  getHistoryImageView(uint32_t handle) { return get(handle).getView(historyInstance()); }

    vk::Viewport   getViewport(void) const; // don't know what to do with this two
    vk::Rect2D     getScissor(void)  const;

    std::string getDebugName(void) const { return pass.debugName; }
    int getFrameIndex(void) const { return frameIndex; }
};

#endif //!__O5MRENDERCONTEXT__H
