#ifndef __O5MPIPELINE__H
#define __O5MPIPELINE__H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <vector>

#include "RHI/O5MDevice.h"

class O5MPipeline {
public:
    O5MPipeline(O5MDevice& device) : m_device(device) {}

    // 收 vk::ShaderModule 而非资源句柄：pipeline 是 RHI 原语，不认识资源层。
    // 入口名：GLSL 经 shaderc/glslang 编译，入口必须叫 "main"（自定义入口名
    // 只在 HLSL 前端受支持），所以默认 "main"。
    void createPipeline(const std::vector<vk::Format>& colorFormats,
                        vk::ShaderModule vs, vk::ShaderModule fs,
                        const char* vsEntry = "main",
                        const char* fsEntry = "main",
                        vk::DescriptorSetLayout setLayout = nullptr);

    void begin(vk::raii::CommandBuffer& cmd, const std::vector<vk::ImageView>& views,
               vk::Extent2D extent);
    void end(vk::raii::CommandBuffer& cmd);

    // 绑定与管线 layout 兼容的 descriptor set（layout 暂时为空 + set=0）
    void bindDescriptorSet(vk::raii::CommandBuffer& cmd, vk::DescriptorSet set);

private:
    O5MDevice& m_device;
    vk::raii::PipelineLayout m_pipelineLayout = nullptr;
    vk::raii::Pipeline m_pipeline = nullptr;
};

#endif //__O5MPIPELINE__H
