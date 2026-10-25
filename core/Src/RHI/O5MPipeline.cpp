#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS

#include "RHI/O5MPipeline.h"

void O5MPipeline::createPipeline(vk::ShaderModule vs, vk::ShaderModule fs,
                                 const char* vsEntry, const char* fsEntry) {
    vk::PipelineShaderStageCreateInfo vsState {
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = vs,
        .pName = vsEntry
    };

    vk::PipelineShaderStageCreateInfo fsState {
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = fs,
        .pName = fsEntry
    };

    vk::PipelineShaderStageCreateInfo shaderStages[] = { vsState, fsState };

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
        .vertexBindingDescriptionCount = static_cast<uint32_t>(m_spec.vertexBindings.size()),
        .pVertexBindingDescriptions = m_spec.vertexBindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(m_spec.vertexAttributes.size()),
        .pVertexAttributeDescriptions = m_spec.vertexAttributes.data(),
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly {
        .topology =  m_spec.topology, 
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
        .cullMode = m_spec.cullMode,
        .frontFace = m_spec.frontFace,
        .lineWidth = 1
    };

    vk::PipelineMultisampleStateCreateInfo multisampling{.rasterizationSamples = vk::SampleCountFlagBits::e1};

    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachmentStates;
    for (auto colorformat : m_spec.colorFormats) {
        vk::PipelineColorBlendAttachmentState colorBlendAttachmentState{
            .blendEnable = vk::False,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
        };

        colorBlendAttachmentStates.push_back(colorBlendAttachmentState);
    }

    //TODO: stencil default state is unclear
    vk::PipelineDepthStencilStateCreateInfo depthStencilInfo {
        .depthTestEnable = m_spec.depthTestEnable,
        .depthWriteEnable = m_spec.depthWriteEnable,
        .depthCompareOp = m_spec.depthCompareOp,
        .stencilTestEnable = m_spec.stencilTestEnable,
        .front = m_spec.frontStencil,
        .back = m_spec.backStencil
    };

    vk::PipelineColorBlendStateCreateInfo colorBlendingInfo{
        .attachmentCount = static_cast<uint32_t>(colorBlendAttachmentStates.size()),
        .pAttachments = colorBlendAttachmentStates.data()
    };

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .setLayoutCount = static_cast<uint32_t>(m_spec.setLayouts.size()),
        .pSetLayouts = m_spec.setLayouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(m_spec.pushConstants.size()),
        .pPushConstantRanges = m_spec.pushConstants.data()
    };

    m_pipelineLayout = m_device.getDevice().createPipelineLayout(pipelineLayoutCreateInfo);

    vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1,
        .scissorCount = 1
    };

    const vk::Format depthFmt = m_spec.depthStencilFormat.value_or(vk::Format::eUndefined);
    const bool hasStencil = m_spec.depthStencilFormat.has_value() && 
        static_cast<bool>(O5MDevice::aspectFromFormat(*m_spec.depthStencilFormat) & vk::ImageAspectFlagBits::eStencil);

    vk::PipelineRenderingCreateInfo renderingInfo {
        .colorAttachmentCount = static_cast<uint32_t>(m_spec.colorFormats.size()),
        .pColorAttachmentFormats = m_spec.colorFormats.data(),
        .depthAttachmentFormat = depthFmt,
        .stencilAttachmentFormat = hasStencil ? depthFmt : vk::Format::eUndefined
    };

    vk::GraphicsPipelineCreateInfo pipelineInfo {
        .pNext = &renderingInfo,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &raterizationInfo,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencilInfo,
        .pColorBlendState = &colorBlendingInfo,
        .pDynamicState = &dynamicStateInfo,
        .layout = *m_pipelineLayout,
    };

    m_pipeline = m_device.getDevice().createGraphicsPipeline(nullptr, pipelineInfo);
}

void O5MPipeline::begin(vk::raii::CommandBuffer& cmd, const O5MBeginInfo& beginInfo) {
    std::vector<vk::RenderingAttachmentInfo> colorAttachments;
    colorAttachments.reserve(beginInfo.colorViews.size());
    for (const auto& view : beginInfo.colorViews) {
        vk::RenderingAttachmentInfo color{};
        color.setImageView(view)
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal) 
            .setLoadOp(beginInfo.colorLoadOp)
            .setStoreOp(beginInfo.colorStoreOp)
            .setClearValue(beginInfo.colorClear);
        colorAttachments.push_back(color);
    }

    vk::RenderingAttachmentInfo depthAttachment{};
    if (beginInfo.depthView != nullptr) {
        depthAttachment.setImageView(beginInfo.depthView)
            .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setLoadOp(beginInfo.depthLoadOp)
            .setStoreOp(beginInfo.depthStoreOp)
            .setClearValue(beginInfo.depthClear);
    }

    vk::RenderingAttachmentInfo stencilAttachment{};
    if (beginInfo.stencilView != nullptr) {
        stencilAttachment.setImageView(beginInfo.stencilView)
            .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setLoadOp(beginInfo.stencilLoadOp)
            .setStoreOp(beginInfo.stencilStoreOp)
            .setClearValue(beginInfo.depthClear);
    }

    vk::RenderingInfo renderingInfo{};
    renderingInfo.setRenderArea({ { 0, 0 }, beginInfo.extent})
        .setLayerCount(1)
        .setColorAttachments(colorAttachments)
        .setPDepthAttachment(beginInfo.depthView != nullptr ? &depthAttachment : nullptr)
        .setPStencilAttachment(beginInfo.stencilView != nullptr ? &stencilAttachment : nullptr);

    cmd.beginRendering(renderingInfo);

    cmd.setViewport(0, vk::Viewport(0.f, 0.f,
                                    static_cast<float>(beginInfo.extent.width),
                                    static_cast<float>(beginInfo.extent.height), 0.f, 1.f));
    cmd.setScissor(0, vk::Rect2D({ 0, 0 }, beginInfo.extent));
}

void O5MPipeline::end(vk::raii::CommandBuffer& cmd) {
    cmd.endRendering();
}

void O5MPipeline::pushConstant(vk::raii::CommandBuffer &cmd, vk::ShaderStageFlags stages, uint32_t offset, uint32_t size, const void *data) const {
    assert(offset % 4 ==0 && "push constant offset must be a multiple of 4");
    const bool covered = std::any_of(
        m_spec.pushConstants.begin(), m_spec.pushConstants.end(),
        [&](const vk::PushConstantRange& r) {
            return (r.stageFlags & stages) == stages && r.offset <= offset && offset + size <= r.offset + r.size;
        });

    assert(covered && "pushed range is not declared in O5MPipeline::pushConstants");
    cmd.pushConstants(*m_pipelineLayout, stages, offset, size, data);
}

void O5MPipeline::bindPipeline(vk::raii::CommandBuffer& cmd) {
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
}

void O5MPipeline::bindDescriptorSet(vk::raii::CommandBuffer& cmd, vk::DescriptorSet set) {
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout, 0, set, nullptr);
}

void O5MPipeline::bindDescriptorSets(vk::raii::CommandBuffer& cmd, uint32_t firstSet,
                                     vk::ArrayProxy<const vk::DescriptorSet> sets) {
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipelineLayout, firstSet, sets, nullptr);
}
