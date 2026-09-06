#ifndef __O5MPIPELINE__H
#define __O5MPIPELINE__H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <vector>

// color-only、fullscreen-triangle 的最小管线封装（dynamic rendering 版）。
// 不创建 vk::RenderPass / vk::Framebuffer —— attachment 信息在 beginRendering
// 时内联给出，layout 转换完全由 O5MRendergraph 的显式 barrier 负责
// （这正是 dynamic rendering 的好处：同步只有一个来源，没有隐式转换）。
//
// 前提：device 开了 VK_KHR_dynamic_rendering 扩展 + feature（Vulkan 1.3 转正，
// 1.2 需扩展；见 main/rhi_verify.cpp 的 device 创建样板）。
class O5MPipeline {
public:
    O5MPipeline(vk::raii::Device& device) : m_device(device) {}

    // 创建 fullscreen-triangle GraphicsPipeline。colorFormats 会通过
    // pNext 的 PipelineRenderingCreateInfo 告诉驱动管线兼容的 attachment 格式
    // （取代 RenderPass compatibility 的角色）。顶点在 VS 里用 gl_VertexIndex 生成。
    void createPipeline(const std::vector<vk::Format>& colorFormats,
                        const std::vector<uint32_t>& vsSpirv,
                        const std::vector<uint32_t>& fsSpirv);

    // 录命令：beginRendering（inline 传 attachment view + clear）+ bindPipeline。
    // begin/end 必须成对，rendergraph 的 pass 回调夹在中间录 draw 等命令。
    // views 与 createPipeline 的 colorFormats 一一对应。
    void begin(vk::raii::CommandBuffer& cmd, const std::vector<vk::ImageView>& views,
               vk::Extent2D extent);
    void end(vk::raii::CommandBuffer& cmd);

private:
    vk::raii::Device& m_device;
    vk::raii::PipelineLayout m_pipelineLayout = nullptr;
    vk::raii::Pipeline m_pipeline = nullptr;
};

#endif //__O5MPIPELINE__H
