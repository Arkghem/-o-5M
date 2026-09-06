#include "O5MPipeline.h"

// ---------------------------------------------------------------------------
// createPipeline —— TODO(you)
// ---------------------------------------------------------------------------
// 思路（参考 vkguide.dev "dynamic rendering" 章节的数据流）：
//   1. ShaderModule：vk::ShaderModuleCreateInfo { pCode = spirv.data(),
//      codeSize = 字节数(spirv.size() * sizeof(uint32_t)) }，
//      每阶段 PipelineShaderStageCreateInfo { stage, *module, "main" }。
//   2. VertexInputState 全默认（无顶点绑定/属性，fullscreen triangle 的顶点
//      在 VS 里由 gl_VertexIndex 展开：(-1,-1) (3,-1) (-1,3)）。
//   3. InputAssembly：eTriangleList。
//   4. Viewport/Scissor：创建时给 dummy（数量 1），配合 DynamicState
//      { eViewport, eScissor }，begin() 里动态设置——extent 变化不用重建管线。
//   5. Rasterization：eFill / cull eNone（绕过绕序坑）/ frontFace 任意 / lineWidth 1。
//   6. Multisample：e1。
//   7. ColorBlend：每个 colorFormat 一个 blendEnable=false 的 attachment state。
//   8. PipelineLayout 先建空的（{}），descriptor 后续阶段再加。
//   9. dynamic rendering 的关键差异：GraphicsPipelineCreateInfo 里没有 renderPass
//      （填默认 {}），改为 pNext 挂 vk::PipelineRenderingCreateInfo：
//          renderingInfo.setColorAttachmentFormats(colorFormats);
//      注意 renderingInfo 的生命周期必须覆盖 createGraphicsPipeline 调用。
void O5MPipeline::createPipeline(const std::vector<vk::Format>& colorFormats,
                                 const std::vector<uint32_t>& vsSpirv,
                                 const std::vector<uint32_t>& fsSpirv) {

}

// begin/end 我直接给——样板没有教学点，注意 RenderingAttachmentInfo 的
// imageView/layout/resolveMode 与 clearValue 一一对应即可。
void O5MPipeline::begin(vk::raii::CommandBuffer& cmd, const std::vector<vk::ImageView>& views,
                        vk::Extent2D extent) {
    std::vector<vk::RenderingAttachmentInfo> colorAttachments;
    colorAttachments.reserve(views.size());
    for (const auto& view : views) {
        vk::RenderingAttachmentInfo color;
        color.setImageView(view)
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal) // graph 的 barrier 已保证
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(vk::ClearValue(
                vk::ClearColorValue(std::array<float, 4>{ 0.f, 0.f, 0.f, 1.f })));
        colorAttachments.push_back(color);
    }

    vk::RenderingInfo renderingInfo;
    renderingInfo.setRenderArea({ { 0, 0 }, extent })
        .setLayerCount(1)
        .setColorAttachments(colorAttachments);

    cmd.beginRendering(renderingInfo);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);

    // viewport/scissor 是 dynamic state，每次 begin 时设置
    cmd.setViewport(0, vk::Viewport(0.f, 0.f,
                                    static_cast<float>(extent.width),
                                    static_cast<float>(extent.height), 0.f, 1.f));
    cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
}

void O5MPipeline::end(vk::raii::CommandBuffer& cmd) {
    cmd.endRendering();
}
