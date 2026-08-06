#ifndef __VKPIPELINE_H__
#define __VKPIPELINE_H__

#include "IGraphicsPipeline.h"
#include <vulkan/vulkan.h>
#include <vector>

// ===========================================================================
// VKPipeline — IGraphicsPipeline backed by VkPipeline + VkPipelineLayout
// ===========================================================================
//
// Teaching note: Vulkan's "pipeline" is a Pipeline State Object (PSO) — a
// monolithic, mostly-immutable bundle of ALL render state. Unlike OpenGL where
// you scatter state via glEnable/glDisable/glBlendFunc/glDepthFunc across the
// draw loop, Vulkan pre-bakes every piece into a VkGraphicsPipelineCreateInfo
// and the driver compiles it into a GPU-optimized blob. Changing ANY state
// (e.g., switching from FILL to LINE polygon mode) requires a different pipeline.
//
// This class mirrors GLPipeline's state-storage pattern (set* stubs store
// copies), then in create() maps every RHI enum to its Vk* equivalent and
// builds the full VkGraphicsPipelineCreateInfo chain.
//
// Key architectural differences from GL:
//  - create() takes VkRenderPass + VkExtent2D: Vulkan pipelines are render-pass
//    -aware; the same shader set needs a different pipeline per render pass.
//    GL has no concept of a render pass — framebuffers are swapped atomically.
//  - VKShader::link() is a no-op; this class's create() IS Vulkan's "link"
//    equivalent — VkShaderModules are bundled into VkPipelineShaderStageCreateInfo
//    stages array at pipeline creation time.
//  - Dynamic state (viewport, scissor) is declared up front via
//    VkPipelineDynamicStateCreateInfo — telling the driver "I'll set these later
//    at cmd-buffer time" — rather than implicit per-draw state in GL.
// ===========================================================================

class VKShader;

class VKPipeline : public IGraphicsPipeline {
public:
    // ── Constructor / Destructor ─────────────────────────────────────────

    /// @param device  Vulkan logical device handle (NOT VulkanDevice* — raw handle only)
    explicit VKPipeline(VkDevice device);
    ~VKPipeline() override;

    // ── Interface: IGraphicsPipeline ─────────────────────────────────────

    void setShaderStages(IShader* vs, IShader* fs, IShader* gs = nullptr) override;
    void setVertexInputLayout(const VertexInputLayout& layout) override;
    void setRasterizerState(const RasterizerState& state) override;
    void setDepthStencilState(const DepthStencilState& state) override;
    void setBlendState(int idx, const BlendState& state) override;

    /// Builds VkGraphicsPipelineCreateInfo from stored state, calls
    /// vkCreateGraphicsPipelines, stores VkPipeline + VkPipelineLayout.
    ///
    /// @param renderPass  The VkRenderPass this pipeline is compatible with.
    ///                    Provided by the command buffer / RHI at creation time.
    /// @param extent      Swapchain or framebuffer extent. Must match the
    ///                    render pass dimensions.
    /// @return true on success, false with spdlog::error logged
    ///
    /// Teaching: Unlike GL's create() (no params — GL's implicit framebuffer),
    /// Vulkan's pipeline MUST know its render pass up front. This is what makes
    /// VkPipeline truly immutable — the render pass is baked in.
    bool create(VkRenderPass renderPass, VkExtent2D extent);

    /// No-arg interface override — returns false. Vulkan pipelines require
    /// render pass + extent at creation time; use create(rp, extent) instead.
    /// VKCommandBuffer calls this lazily via create(rp, extent) when needed.
    bool create() override { return false; }

    // ── Queries ──────────────────────────────────────────────────────────

    VkPipeline       pipeline()        const { return m_pipeline; }
    VkPipelineLayout  layout()          const { return m_layout; }
    const VertexInputLayout& vertexInputLayout() const { return m_vtxLayout; }
    bool              isValid()         const { return m_pipeline != VK_NULL_HANDLE; }

    // ── Static format/enum mapping helpers ───────────────────────────────

    /// Maps VertexInputLayout::Attribute::FORMAT → VkFormat.
    /// 19 enums total: 4 float, 3 uint8-unorm, 3 uint16-unorm, 3 int8-snorm,
    /// 3 uint8, 4 int32.
    static VkFormat toVkFormat(VertexInputLayout::Attribute::FORMAT format);

    /// Maps DepthStencilState::CompareOp → VkCompareOp (6 values).
    static VkCompareOp toVkCompareOp(DepthStencilState::CompareOp op);

    /// Maps BlendState::Factor → VkBlendFactor (6 values).
    /// Teaching: src and dst factors are the SAME VkBlendFactor enum — Vulkan
    /// uses the same enum for both, unlike GL which has GL_SRC_ALPHA and
    /// GL_ONE_MINUS_SRC_ALPHA and expects you to pass them to different params.
    static VkBlendFactor toVkBlendFactor(BlendState::Factor factor);

private:
    VkDevice        m_device    = VK_NULL_HANDLE;
    VkPipeline       m_pipeline   = VK_NULL_HANDLE;
    VkPipelineLayout  m_layout     = VK_NULL_HANDLE;

    // ── State copies (set* stubs fill these, create() reads them) ────────
    VKShader*           m_vs = nullptr;
    VKShader*           m_fs = nullptr;
    VKShader*           m_gs = nullptr;
    RasterizerState     m_raster;
    DepthStencilState   m_depth;
    std::vector<BlendState> m_blends;
    VertexInputLayout   m_vtxLayout;
};

#endif // __VKPIPELINE_H__
