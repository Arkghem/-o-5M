#include "VKPipeline.h"
#include "VKShader.h"

#include <spdlog/spdlog.h>
#include <cassert>

// ===========================================================================
// VK_CHECK — file-local assertion macro for Vulkan result codes
// ===========================================================================
// Teaching note: unlike OpenGL where errors are polled per-call via
// glGetError(), Vulkan returns VkResult from every vkCreate*/vkAllocate*
// function. You MUST check every return value — a missed VK_ERROR_OUT_OF_HOST_MEMORY
// leads to a crash many frames later with no clear root cause.
// ===========================================================================
#define VK_CHECK(result, msg)                                               \
    do {                                                                    \
        VkResult _res = (result);                                          \
        if (_res != VK_SUCCESS) {                                          \
            spdlog::error("Vulkan error: {} — VkResult={}", msg,            \
                          static_cast<int>(_res));                         \
            assert(false && msg);                                          \
        }                                                                  \
    } while (0)

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

VKPipeline::VKPipeline(VkDevice device)
    : m_device(device)
{
}

VKPipeline::~VKPipeline()
{
    // Teaching: Vulkan objects are destroyed in reverse-creation order.
    // Pipeline must be destroyed before its layout (layout is a dependency).
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// set* stubs — store state copies (mirrors GLPipeline pattern)
// ─────────────────────────────────────────────────────────────────────────────

void VKPipeline::setShaderStages(IShader* vs, IShader* fs, IShader* gs)
{
    m_vs = static_cast<VKShader*>(vs);
    m_fs = static_cast<VKShader*>(fs);
    m_gs = static_cast<VKShader*>(gs);
}

void VKPipeline::setVertexInputLayout(const VertexInputLayout& layout)
{
    m_vtxLayout = layout;
}

void VKPipeline::setRasterizerState(const RasterizerState& state)
{
    m_raster = state;
}

void VKPipeline::setDepthStencilState(const DepthStencilState& state)
{
    m_depth = state;
}

void VKPipeline::setBlendState(int idx, const BlendState& state)
{
    if (m_blends.size() <= static_cast<size_t>(idx)) {
        m_blends.resize(idx + 1);
    }
    m_blends[idx] = state;
}

// ─────────────────────────────────────────────────────────────────────────────
// create() — build VkGraphicsPipelineCreateInfo from stored state
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching note: This method is Vulkan's equivalent of GLPipeline::create() +
// GLShader::link() combined. In GL, shader linking produces a glProgramObject
// and state is applied per-draw via individual glEnable/glDepthFunc calls.
// In Vulkan, ALL state — shaders, vertex format, rasterizer, depth/stencil,
// blend, render pass, dynamic state — is baked into a single immutable
// VkPipeline object. Changing any of these (e.g. toggling wireframe mode)
// requires creating a new pipeline.
//
// The renderPass parameter is required because Vulkan pipelines are
// render-pass-aware: the pipeline's output format(s) must match the render
// pass's attachment formats. GL has no equivalent — it just writes to the
// currently-bound FBO.
// =============================================================================

bool VKPipeline::create(VkRenderPass renderPass, VkExtent2D extent)
{
    if (!m_vs || !m_fs) {
        spdlog::error("VKPipeline::create: vertex and fragment shader required");
        return false;
    }

    // Ensure shaders are compiled (link() is no-op for VKShader, but call for
    // interface consistency — compile() should have been called already by VKRhi)
    if (!m_vs->compile() || !m_fs->compile()) {
        spdlog::error("VKPipeline::create: shader compilation failed");
        return false;
    }

    // ── 1. Shader stages ─────────────────────────────────────────────────
    //
    // Teaching: Vulkan separates shader stages from the pipeline — each
    // VkPipelineShaderStageCreateInfo bundles a VkShaderModule + entry point
    // name ("main" by default in GLSL). Multiple stages (VS, FS, GS) go into
    // one array. GL implicitly links all attached shaders at glLinkProgram time;
    // Vulkan makes the set-of-stages explicit at pipeline creation time.

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    {
        VkPipelineShaderStageCreateInfo stageInfo = {};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.pName = "main";  // GLSL entry point

        // Vertex shader (required)
        stageInfo.stage  = m_vs->stageFlag();
        stageInfo.module = m_vs->module();
        shaderStages.push_back(stageInfo);

        // Fragment shader (required)
        stageInfo.stage  = m_fs->stageFlag();
        stageInfo.module = m_fs->module();
        shaderStages.push_back(stageInfo);

        // Geometry shader (optional)
        // Teaching: GL impl crashes on null gs; Vulkan handles it explicitly
        // by only adding the stage when the shader is present.
        if (m_gs) {
            stageInfo.stage  = m_gs->stageFlag();
            stageInfo.module = m_gs->module();
            shaderStages.push_back(stageInfo);
        }
    }

    // ── 2. Vertex input ──────────────────────────────────────────────────
    //
    // Teaching: Vulkan uses TWO arrays to describe vertex input:
    //   - bindings[]: describe the per-buffer layout (stride, per-vertex vs per-instance rate)
    //   - attributes[]: describe each shader input location (format, offset, which binding)
    // GL merges these: glVertexArrayAttribFormat(loc, size, type, normalized, offset)
    // + glVertexArrayAttribBinding(loc, bindingIndex) + glVertexArrayBindingDivisor(binding, divisor).
    // The Vulkan two-array approach mirrors VkPipelineVertexInputStateCreateInfo more directly.
    //
    // perInstance mapping: GL's glVertexBindingDivisor(binding, 1) vs Vulkan's
    // VK_VERTEX_INPUT_RATE_INSTANCE. GL ignores per-instance rate by default
    // (divisor=0 ⇒ per-vertex); Vulkan honors it explicitly.

    std::vector<VkVertexInputBindingDescription>   bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;

    for (size_t b = 0; b < m_vtxLayout.bindings.size(); ++b) {
        VkVertexInputBindingDescription binding = {};
        binding.binding   = static_cast<uint32_t>(b);
        binding.stride    = m_vtxLayout.bindings[b].stride;
        binding.inputRate = m_vtxLayout.bindings[b].perInstance
                            ? VK_VERTEX_INPUT_RATE_INSTANCE
                            : VK_VERTEX_INPUT_RATE_VERTEX;
        bindings.push_back(binding);
    }

    for (const auto& attr : m_vtxLayout.attributes) {
        VkVertexInputAttributeDescription desc = {};
        desc.location = attr.location;
        desc.binding  = attr.binding;
        desc.format   = toVkFormat(attr.format);
        desc.offset   = attr.offset;
        attributes.push_back(desc);
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount   = static_cast<uint32_t>(bindings.size());
    vertexInputInfo.pVertexBindingDescriptions      = bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions    = attributes.data();

    // ── 3. Input assembly ────────────────────────────────────────────────
    //
    // Teaching: Fixed primitive topology at pipeline creation time — no
    // glBegin(GL_TRIANGLES) equivalent. Vulkan requires the topology up front
    // because GPUs optimize for it. primitiveRestartEnable=false: no strip-cut
    // indices (we don't need them for the indexed cube).

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // ── 4. Viewport state (dynamic) ──────────────────────────────────────
    //
    // Teaching: Vulkan requires declaring viewport + scissor counts at pipeline
    // creation time even when they're dynamic state. This is the "shape" of the
    // pipeline's viewport array — the actual values are set at command-buffer
    // time via vkCmdSetViewport / vkCmdSetScissor.
    // GL equivalent: glViewport() + glScissor() at draw time (always dynamic).

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    // ── 5. Rasterizer ────────────────────────────────────────────────────
    //
    // Teaching: Vulkan's rasterizer struct is analogous to GL's combined
    // glPolygonMode + glCullFace + glEnable(GL_DEPTH_CLAMP), but all in one
    // immutable struct. No scattered state changes at draw time.
    //
    // frontFace=VK_FRONT_FACE_CLOCKWISE: Vulkan's clip space has Y-down
    // (upper-left origin), GL has Y-up. To preserve the same winding order for
    // models authored in GL convention, we invert the front face. Without this,
    // back-face culling would cull the WRONG faces.

    VkPolygonMode vkPolygonMode = (m_raster.polygonMode == RasterizerState::LINE)
                                  ? VK_POLYGON_MODE_LINE
                                  : VK_POLYGON_MODE_FILL;

    VkCullModeFlags vkCullMode = VK_CULL_MODE_NONE;
    switch (m_raster.cullMode) {
        case RasterizerState::FRONT: vkCullMode = VK_CULL_MODE_FRONT_BIT; break;
        case RasterizerState::BACK:  vkCullMode = VK_CULL_MODE_BACK_BIT;  break;
        case RasterizerState::NONE:  vkCullMode = VK_CULL_MODE_NONE;      break;
    }

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = vkPolygonMode;
    rasterizer.cullMode    = vkCullMode;
    rasterizer.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthClampEnable     = m_raster.depthClamp ? VK_TRUE : VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;
    // depthBias* are left at zero (no shadow mapping yet — Phase 8)

    // ── 6. Multisample ───────────────────────────────────────────────────
    //
    // Teaching: rasterizationSamples=1 means no MSAA. Vulkan requires this
    // struct even when not multisampling — it declares the sample count for
    // rasterization. GL doesn't need an explicit multisample state struct
    // (glEnable(GL_MULTISAMPLE) suffices).

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 0.0f;

    // ── 7. Depth / Stencil ───────────────────────────────────────────────
    //
    // Teaching: Vulkan's depth-stencil state bundles depth test, depth write,
    // depth compare, stencil test, stencil ops, and depth bounds into one struct.
    // GL spreads these across glEnable(GL_DEPTH_TEST), glDepthMask, glDepthFunc,
    // glStencilFunc, glStencilOp, etc. — any can change independently at draw time.
    //
    // Stencil is disabled (C-scope: no stencil testing in current phases).
    // depthBoundsTestEnable=false: an optional per-draw depth clamp range
    // that GL doesn't have a direct equivalent for.

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable       = m_depth.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable      = m_depth.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp        = toVkCompareOp(m_depth.depthCompareOp);
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable     = VK_FALSE;
    // front/back stencil state left at zero (KEEP, NEVER, 0 masks)

    // ── 8. Color blend ───────────────────────────────────────────────────
    //
    // Teaching: Vulkan's color blend state is per-attachment — each render
    // target configured independently (matches GL's glBlendFuncSeparatei
    // / glBlendEquationSeparatei). If no blend states are set, we create a
    // single disabled attachment (passthrough).
    //
    // Alpha blend factors are hardcoded to ONE / ZERO (same as GL impl).
    // colorWriteMask = RGBA all channels (no mask).

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    if (m_blends.empty()) {
        VkPipelineColorBlendAttachmentState disabled = {};
        disabled.blendEnable         = VK_FALSE;
        disabled.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT
                                     | VK_COLOR_COMPONENT_G_BIT
                                     | VK_COLOR_COMPONENT_B_BIT
                                     | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments.push_back(disabled);
    } else {
        for (const auto& blend : m_blends) {
            VkPipelineColorBlendAttachmentState attach = {};
            attach.blendEnable         = blend.enable ? VK_TRUE : VK_FALSE;
            attach.srcColorBlendFactor = toVkBlendFactor(blend.srcColorBlendFactor);
            attach.dstColorBlendFactor = toVkBlendFactor(blend.dstColorBlendFactor);
            attach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;  // same as GL impl
            attach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // same as GL impl
            attach.colorBlendOp        = VK_BLEND_OP_ADD;
            attach.alphaBlendOp        = VK_BLEND_OP_ADD;
            attach.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT
                                       | VK_COLOR_COMPONENT_G_BIT
                                       | VK_COLOR_COMPONENT_B_BIT
                                       | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachments.push_back(attach);
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable     = VK_FALSE;
    colorBlending.attachmentCount   = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments      = colorBlendAttachments.data();
    float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // ── 9. Dynamic state ─────────────────────────────────────────────────
    //
    // Teaching: Vulkan requires declaring which parts of the pipeline are
    // "dynamic" — settable at command-buffer time without recreating the
    // pipeline. We declare VIEWPORT + SCISSOR as dynamic so the command
    // buffer can set them at beginPass time (via vkCmdSetViewport/VkCmdSetScissor).
    // Other dynamic states (line width, depth bias, stencil refs) are left static
    // for now.
    //
    // GL equivalent: EVERY state is implicitly dynamic — glViewport/glScissor
    // can be called at any point between draw calls. Vulkan requires you to
    // opt in to the ones you intend to change.

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynamicStates;

    // ── 10. Pipeline layout ──────────────────────────────────────────────
    //
    // Teaching: The pipeline layout declares the resource binding contract —
    // which descriptor set layouts and push constant ranges the shader stages
    // expect. With empty shader reflection (no SPIRV-Cross yet), we create a
    // layout with zero descriptor sets and zero push constants.
    //
    // Later: when VKShaderResourceBindings integration lands, the pipeline
    // layout will come from the bindings object (vkCmdBindDescriptorSets
    // requires the same layout compatibility).
    //
    // GL equivalent: glUseProgram implicitly knows about uniform locations
    // and texture units — there's no separate "layout" object.

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts    = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges    = nullptr;

    VkResult layoutResult = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_layout);
    if (layoutResult != VK_SUCCESS) {
        spdlog::error("VKPipeline::create: vkCreatePipelineLayout failed — VkResult={}",
                      static_cast<int>(layoutResult));
        return false;
    }

    // ── 11. Assemble & create ────────────────────────────────────────────
    //
    // Teaching: VkGraphicsPipelineCreateInfo is the "everything struct" —
    // it bundles all 10+ sub-structs into a single giant creation call.
    // The driver compiles this into a GPU-specific binary (similar to how
    // a C++ compiler turns source → object code). This is why PSO creation
    // is expensive — do it at load time, not per frame.
    //
    // GL equivalent: glLinkProgram + configuring all state individually at
    // first draw (driver does compilation lazily on the first draw call).

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages             = shaderStages.data();
    pipelineInfo.pVertexInputState   = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = m_layout;
    pipelineInfo.renderPass          = renderPass;
    pipelineInfo.subpass             = 0;       // first (only) subpass
    pipelineInfo.basePipelineHandle  = VK_NULL_HANDLE;  // no pipeline derivation (C6)
    pipelineInfo.basePipelineIndex   = -1;       // no pipeline cache (C6)

    // Teaching: vkCreateGraphicsPipelines creates 1 pipeline with no pipeline
    // cache (VK_NULL_HANDLE). Pipeline caches serialize driver compilation
    // results to disk — a production optimization we skip for teaching clarity.
    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                       &pipelineInfo, nullptr, &m_pipeline),
             "vkCreateGraphicsPipelines");

    spdlog::info("VKPipeline::create: pipeline created successfully "
                 "(stages={}, bindings={}, attributes={}, blend-attachments={})",
                 shaderStages.size(), bindings.size(), attributes.size(),
                 colorBlendAttachments.size());

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Static helpers: format / enum mapping
// ─────────────────────────────────────────────────────────────────────────────

VkFormat VKPipeline::toVkFormat(VertexInputLayout::Attribute::FORMAT format)
{
    // Teaching: Vulkan uses a single VkFormat for both vertex attributes AND
    // texture formats — unlike GL which uses a three-tuple (internalFormat,
    // format, type) for textures and separate glVertexAttribPointer params
    // (size, type, normalized) for vertex attributes. VkFormat encodes
    // component count, component type, and normalization in one enum value.

    using F = VertexInputLayout::Attribute;
    switch (format) {
        // Float formats
        case F::FLOAT32:     return VK_FORMAT_R32_SFLOAT;
        case F::FLOAT32X2:   return VK_FORMAT_R32G32_SFLOAT;
        case F::FLOAT32X3:   return VK_FORMAT_R32G32B32_SFLOAT;
        case F::FLOAT32X4:   return VK_FORMAT_R32G32B32A32_SFLOAT;

        // Unsigned normalized (0.0–1.0 from integer)
        case F::UINT8_UNORM:   return VK_FORMAT_R8_UNORM;
        case F::UINT8X2_UNORM: return VK_FORMAT_R8G8_UNORM;
        case F::UINT8X4_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case F::UINT16_UNORM:  return VK_FORMAT_R16_UNORM;
        case F::UINT16X2_UNORM: return VK_FORMAT_R16G16_UNORM;
        case F::UINT16X4_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;

        // Signed normalized (-1.0–1.0 from signed integer)
        case F::INT8_SNorm:   return VK_FORMAT_R8_SNORM;
        case F::INT8X2_SNORM: return VK_FORMAT_R8G8_SNORM;
        case F::INT8X4_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;

        // Raw unsigned integer (shader sees int value directly)
        case F::UINT8:   return VK_FORMAT_R8_UINT;
        case F::UINT8X2: return VK_FORMAT_R8G8_UINT;
        case F::UINT8X4: return VK_FORMAT_R8G8B8A8_UINT;

        // Raw signed integer (shader sees int value directly)
        case F::INT32:   return VK_FORMAT_R32_SINT;
        case F::INT32X2: return VK_FORMAT_R32G32_SINT;
        case F::INT32X3: return VK_FORMAT_R32G32B32_SINT;
        case F::INT32X4: return VK_FORMAT_R32G32B32A32_SINT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkCompareOp VKPipeline::toVkCompareOp(DepthStencilState::CompareOp op)
{
    // Teaching: Depth/stencil compare operations map 1:1 between GL and Vulkan.
    // GL: GL_NEVER / GL_LESS / GL_EQUAL / GL_LEQUAL / GL_GREATER / GL_ALWAYS
    // Vk: VK_COMPARE_OP_NEVER / LESS / EQUAL / LESS_OR_EQUAL / GREATER / ALWAYS

    switch (op) {
        case DepthStencilState::CompareOp::NEVER:         return VK_COMPARE_OP_NEVER;
        case DepthStencilState::CompareOp::LESS:          return VK_COMPARE_OP_LESS;
        case DepthStencilState::CompareOp::EQUAL:         return VK_COMPARE_OP_EQUAL;
        case DepthStencilState::CompareOp::LESS_OR_EQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case DepthStencilState::CompareOp::GREATER:       return VK_COMPARE_OP_GREATER;
        case DepthStencilState::CompareOp::ALWAYS:        return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_NEVER;
}

VkBlendFactor VKPipeline::toVkBlendFactor(BlendState::Factor factor)
{
    // Teaching: Vulkan uses the SAME VkBlendFactor enum for both src and dst
    // blend factors (unlike GL which has separate tokens like GL_SRC_ALPHA and
    // GL_ONE_MINUS_SRC_ALPHA passed to different glBlendFuncSeparate params).
    // The mapping is 1:1 for all 6 factors.

    switch (factor) {
        case BlendState::Factor::ZERO:                      return VK_BLEND_FACTOR_ZERO;
        case BlendState::Factor::ONE:                       return VK_BLEND_FACTOR_ONE;
        case BlendState::Factor::SRC_ALPHA:                 return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendState::Factor::DST_ALPHA:                 return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendState::Factor::ONE_MINUS_SRC_ALPHA:       return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendState::Factor::ONE_MINUS_DST_ALPHA:       return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ZERO;
}
