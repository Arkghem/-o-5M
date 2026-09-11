#ifndef __O5MPIPELINE__H
#define __O5MPIPELINE__H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <vector>

#include "O5MResource.h"
#include "O5MShaderResource.h"
#include "O5MDevice.h"

class O5MPipeline {
public:
    O5MPipeline(O5MDevice& device) : m_device(device) {}

    void createPipeline(const std::vector<vk::Format>& colorFormats,
                        const O5MResourceHandle<O5MShaderResource>& vsSpirv,
                        const O5MResourceHandle<O5MShaderResource>& fsSpirv,
                        const char* vsEntry = "vertMain",
                        const char* fsEntry = "fragMain");

    void begin(vk::raii::CommandBuffer& cmd, const std::vector<vk::ImageView>& views,
               vk::Extent2D extent);
    void end(vk::raii::CommandBuffer& cmd);

private:
    O5MDevice& m_device;
    vk::raii::PipelineLayout m_pipelineLayout = nullptr;
    vk::raii::Pipeline m_pipeline = nullptr;
};

#endif //__O5MPIPELINE__H
