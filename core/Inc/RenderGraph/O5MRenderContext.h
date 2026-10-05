#ifndef __O5MRENDERCONTEXT__H
#define __O5MRENDERCONTEXT__H

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include <vector>

#include "RenderGraph/O5MRendergraphType.h"

using namespace O5MRendergraphNS;

//Context pass the reference of the vulkan resource.
class O5MRenderContext {
private:
    std::vector<std::pair<uint32_t, PhysicalResource&>> physicalResources; 
    const PassDesc& pass;
    const int frameIndex; //readHistory support

    PhysicalResource& get(uint32_t handle) const {
        for (auto [h, res] : physicalResources) {
            if (h == handle) 
                return res;
        }
    }
public:
    O5MRenderContext(const PassDesc& pass, std::unordered_map<ResourceHandle, PhysicalResource>& ref, int frameIndex = INT_MAX) :
        frameIndex(frameIndex),
        pass(pass) {
        for (auto read : pass.reads) 
            physicalResources.push_back({read.handle, ref.at(read.handle)});
        for (auto write : pass.writes) 
            physicalResources.push_back({write.handle, ref.at(write.handle)});
        for (auto readWrite : pass.readWrites) //buffer doesn't has this.
            physicalResources.push_back({readWrite.handle, ref.at(readWrite.handle)});
    }

    //no idea about wtf is these.
    //TODO: fixthis
    //TODO: readHistory support
    vk::Buffer getBuffer(uint32_t handle) { return get(handle).getBuffer(); }
    vk::Image      getImage(uint32_t handle) { return get(handle).getImage(); }
    vk::ImageView  getImageView(uint32_t handle) { return get(handle).getView(); }

    vk::ImageView  getHistory(uint32_t handle) const; //get the imageView from last frame

    vk::Viewport   getViewport(void) const; // don't know what to do with this two 
    vk::Rect2D     getScissor(void)  const;

    std::string getDebugName(void) const { return pass.debugName; }
    int getFrameIndex(void) const { return frameIndex; }
};

#endif //!__O5MRENDERCONTEXT__H
