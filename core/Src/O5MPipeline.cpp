#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include "O5MPipeline.h"

void O5MPipeline::createPipeline(const std::vector<vk::Format>& colorFormats,
                                 const O5MResourceHandle<O5MShaderResource>& vsSpirv,
                                 const O5MResourceHandle<O5MShaderResource>& fsSpirv,
                                 const char* vsEntry,
                                 const char* fsEntry) {
    vk::PipelineShaderStageCreateInfo vsState {
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = vsSpirv->getShaderModule(),
        .pName = vsEntry 
    };

    vk::PipelineShaderStageCreateInfo fsState {
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = fsSpirv->getShaderModule(),
        .pName = fsEntry
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
