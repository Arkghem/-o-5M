#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include "O5MPipeline.h"
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
                                 const O5MResourceHandle<O5MShaderResource>& vsSpirv,
                                 const O5MResourceHandle<O5MShaderResource>& fsSpirv) {
    vk::PipelineShaderStageCreateInfo vsState {
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = vsSpirv->getShaderModule(),
        .pName = "vertMain",
    };

    vk::PipelineShaderStageCreateInfo fsState {
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = fsSpirv->getShaderModule(),
        .pName = "fragMain",
    };

    vk::PipelineShaderStageCreateInfo shaderStages[] = { vsState, fsState };

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo {

    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly {
        .topology = vk::PrimitiveTopology::eTriangleList  
    };

    std::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    vk::PipelineRasterizationStateCreateInfo raterizationInfo {
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1
    };

    vk::PipelineMultisampleStateCreateInfo multisampling{.rasterizationSamples = vk::SampleCountFlagBits::e1};

    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachmentStates;
    for (auto colorformat : colorFormats) {
        vk::PipelineColorBlendAttachmentState colorBlendAttachmentState{
            .blendEnable = vk::False
        };

        colorBlendAttachmentStates.push_back(colorBlendAttachmentState);
    }

    vk::PipelineColorBlendStateCreateInfo colorBlendingInfo{
        .attachmentCount = static_cast<uint32_t>(colorBlendAttachmentStates.size()),
        .pAttachments = colorBlendAttachmentStates.data()
    };

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{

    };

    m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutCreateInfo);

    vk::PipelineRenderingCreateInfo renderingInfo {
        .colorAttachmentCount = static_cast<uint32_t>(colorFormats.size()),
        .pColorAttachmentFormats = colorFormats.data()
    };

    vk::GraphicsPipelineCreateInfo pipelineInfo {
        .pNext = &renderingInfo,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = nullptr,
        .pRasterizationState = &raterizationInfo,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &colorBlendingInfo,
        .pDynamicState = &dynamicStateInfo,
        .layout = *m_pipelineLayout,
    };

    m_pipeline = m_device.createGraphicsPipeline(nullptr, pipelineInfo);
}

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

    cmd.setViewport(0, vk::Viewport(0.f, 0.f,
                                    static_cast<float>(extent.width),
                                    static_cast<float>(extent.height), 0.f, 1.f));
    cmd.setScissor(0, vk::Rect2D({ 0, 0 }, extent));
}

void O5MPipeline::end(vk::raii::CommandBuffer& cmd) {
    cmd.endRendering();
}
