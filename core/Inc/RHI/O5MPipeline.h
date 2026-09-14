#ifndef __O5MPIPELINE__H
#define __O5MPIPELINE__H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <vector>

#include "RHI/O5MDevice.h"

class O5MPipeline {
public:
    O5MPipeline(O5MDevice& device) : m_device(device) {}

    void createPipeline(const std::vector<vk::Format>& colorFormats,
                        vk::ShaderModule vs, vk::ShaderModule fs,
                        const char* vsEntry = "main",
                        const char* fsEntry = "main",
                        vk::DescriptorSetLayout setLayout = nullptr);

    void begin(vk::raii::CommandBuffer& cmd, const std::vector<vk::ImageView>& views,
               vk::Extent2D extent);
    void end(vk::raii::CommandBuffer& cmd);

    void bindDescriptorSet(vk::raii::CommandBuffer& cmd, vk::DescriptorSet set);

private:
    O5MDevice& m_device;
    vk::raii::PipelineLayout m_pipelineLayout = nullptr;
    vk::raii::Pipeline m_pipeline = nullptr;
};

#endif //__O5MPIPELINE__H
