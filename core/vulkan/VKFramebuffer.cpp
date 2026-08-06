#include "VKFramebuffer.h"

#include "VKTexture.h"

#include <spdlog/spdlog.h>

#include <cassert>
#include <vector>

// ===========================================================================
// VK_CHECK — local assertion macro for Vulkan result codes
// ===========================================================================
// Teaching note: unlike GL's polled glGetError(), Vulkan errors are immediate
// returns from vkCreate*/vkAllocate* calls. Missing a VK_CHECK means the GPU
// object is VK_NULL_HANDLE and the crash happens frames later — much harder
// to debug than a loud assert at creation time.
// ===========================================================================
#define VK_CHECK(result, msg)                                                  \
    do {                                                                       \
        VkResult _res = (result);                                              \
        if (_res != VK_SUCCESS) {                                              \
            spdlog::error("Vulkan error: {} — VkResult={}", msg,                \
                          static_cast<int>(_res));                             \
            assert(false && msg);                                              \
        }                                                                      \
    } while (0)

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

VKFramebuffer::VKFramebuffer(VkDevice device)
    : m_device(device)
{
}

VKFramebuffer::~VKFramebuffer() {
    // Destroy in reverse creation order: framebuffer → render pass.
    if (m_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

// ===========================================================================
// attachColor / attachDepthStencil — register attachments before create()
// ===========================================================================

void VKFramebuffer::attachColor(int index, ITexture* texture, int mipLevel, int layer) {
    m_colorAttachments[index] = { static_cast<VKTexture*>(texture), mipLevel, layer };
}

void VKFramebuffer::attachDepthStencil(ITexture* texture, int mipLevel, int layer) {
    m_depthAttachment = { static_cast<VKTexture*>(texture), mipLevel, layer };
}

// ===========================================================================
// create() — render pass synthesis + VkFramebuffer creation
// ===========================================================================
//
// Teaching anchor: Why does Vulkan need a VkRenderPass?
//
// Tile-based GPU optimization — GPUs like Apple Silicon, ARM Mali, and
// Qualcomm Adreno split the framebuffer into tiles that fit entirely in
// on-chip tile memory. The render pass declares:
//   - Which attachments stay on-chip (LOAD_OP_CLEAR → no read from VRAM)
//   - Which need write-back to main memory (STORE_OP_STORE)
//   - Layout transitions between passes (color → shader read)
//
// OpenGL guesses at all of this heuristically. Vulkan makes it explicit,
// which is both more verbose AND faster — the driver doesn't have to
// reverse-engineer your intent from a sequence of individual glDraw calls.
//
// Subpass dependency: EXTERNAL → 0 with explicit pipeline barriers.
// GL's driver inserts barriers implicitly after each glDraw*; Vulkan
// requires you to declare when external work (previous render pass,
// compute, transfer) must finish before this pass begins. The VkSubpassDependency
// below says: "wait for nothing from outside (TOP_OF_PIPE), and make sure
// color/depth attachment writes are visible before the fragment shader runs."
//
// Framebuffer compatibility: all attachments in a single VkFramebuffer
// must have identical dimensions. This is enforced by assert below.
// Vulkan's spec requires it; violating it is undefined behavior.
// ===========================================================================

bool VKFramebuffer::create() {
    // --- Guard: at least one color attachment required ---
    if (m_colorAttachments.empty()) {
        spdlog::error("VKFramebuffer::create: no color attachments registered");
        return false;
    }

    const bool hasDepth = (m_depthAttachment.texture != nullptr);
    const uint32_t colorCount = static_cast<uint32_t>(m_colorAttachments.size());
    const uint32_t totalAttachments = colorCount + (hasDepth ? 1u : 0u);

    std::vector<VkAttachmentDescription> attachmentDescs;
    std::vector<VkImageView>            attachmentViews;
    std::vector<VkAttachmentReference>  colorRefs;
    VkAttachmentReference               depthRef{};

    attachmentDescs.reserve(totalAttachments);
    attachmentViews.reserve(totalAttachments);
    colorRefs.reserve(colorCount);

    // --- Step 1: Collect attachment descriptions from textures ---
    //
    // Each VkAttachmentDescription describes ONE slot in the framebuffer:
    //   - format: pixel layout of the attachment (must match VkImageView)
    //   - samples: always 1 (no MSAA, C3 scope)
    //   - loadOp: CLEAR = discard previous content, start from clear color.
    //             If ClearValue::active=false (future extension), would use LOAD
    //             to preserve previous pass output (e.g., accumulation).
    //   - storeOp: STORE = write result to main memory so it can be sampled
    //              later (SHADER_READ_ONLY_OPTIMAL). Transient attachments
    //              (intermediate G-buffer) would use DONT_CARE for bandwidth.
    //   - stencilLoadOp/stencilStoreOp: only meaningful for D24S8; Vulkan
    //     ignores these fields for color-only formats.
    //   - initialLayout: UNDEFINED = GPU may discard whatever was in memory.
    //      Render pass will transition to the attachment reference layout.
    //   - finalLayout: where the image ends up after render pass ends.
    //      Color → SHADER_READ_ONLY_OPTIMAL (samplable in next pass).
    //      Depth → DEPTH_STENCIL_ATTACHMENT_OPTIMAL (not typically sampled).
    // =========================================================================

    // --- Color attachments ---
    for (auto& [index, att] : m_colorAttachments) {
        VKTexture* vkTex = att.texture;
        assert(vkTex && "VKFramebuffer: null color texture");

        VkAttachmentDescription desc{};
        desc.format         = vkTex->vkFormat();
        desc.samples        = VK_SAMPLE_COUNT_1_BIT;
        desc.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        desc.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        attachmentDescs.push_back(desc);
        attachmentViews.push_back(vkTex->imageView());

        VkAttachmentReference ref{};
        ref.attachment = static_cast<uint32_t>(attachmentDescs.size() - 1);
        ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs.push_back(ref);
    }

    // --- Depth attachment ---
    if (hasDepth) {
        VKTexture* vkTex = m_depthAttachment.texture;
        assert(vkTex && "VKFramebuffer: null depth texture");

        VkAttachmentDescription desc{};
        desc.format         = vkTex->vkFormat();
        desc.samples        = VK_SAMPLE_COUNT_1_BIT;
        desc.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        desc.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;  // stencil cleared alongside depth
        desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        // Depth stays in depth layout — not typically sampled as a color texture.
        desc.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachmentDescs.push_back(desc);
        attachmentViews.push_back(vkTex->imageView());

        depthRef.attachment = static_cast<uint32_t>(attachmentDescs.size() - 1);
        depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    // --- Step 2: VkSubpassDescription (single subpass, C8 scope) ---
    //
    // Teaching note: a subpass represents one "batch" of draw calls that share
    // the same attachments. Multiple subpasses (e.g., deferred shading: geometry
    // subpass → lighting subpass) allow the GPU to keep intermediate results in
    // tile memory without writing to VRAM. This phase uses exactly 1 subpass
    // because we're doing single-pass forward rendering.
    // =========================================================================

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = colorCount;
    subpass.pColorAttachments       = colorRefs.data();
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;
    // pResolveAttachments = nullptr (no MSAA resolve)
    // pPreserveAttachments = nullptr (no transient middle-pass attachments)

    // --- Step 3: VkSubpassDependency (EXTERNAL → subpass 0) ---
    //
    // Teaching note: this barrier tells the GPU: "before the color/depth
    // attachment stages begin in this render pass, make sure there are no
    // outstanding writes from before the render pass started."
    //
    // srcStageMask: what pipeline stages from BEFORE this render pass must complete.
    //   TOP_OF_PIPE_BIT = "nothing specific" — we don't depend on prior work.
    //   In a multi-pass setup, this would be FRAGMENT_SHADER_BIT from the
    //   previous pass (so it finishes sampling before we write to the same image).
    //
    // dstStageMask: what pipeline stages IN this render pass must wait.
    //   COLOR_ATTACHMENT_OUTPUT_BIT = color blending / writeback stage.
    //   EARLY_FRAGMENT_TESTS_BIT = depth/stencil test (before fragment shader).
    //
    // srcAccessMask / dstAccessMask: what memory access operations are on each
    //   side of the barrier. These flush GPU caches.
    //   write → COLOR_ATTACHMENT_WRITE_BIT | DEPTH_STENCIL_ATTACHMENT_WRITE_BIT.
    //
    // This is the Vulkan equivalent of GL's implicit synchronization after
    // each draw call — except Vulkan bakes it into the render pass so the
    // driver can optimize around it.
    // =========================================================================

    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags        dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    if (hasDepth) {
        dstStageMask  |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependency.dstStageMask  = dstStageMask;
    dependency.srcAccessMask = 0;  // no prior memory writes to wait for
    dependency.dstAccessMask = dstAccessMask;
    // dependencyFlags = 0 (no VK_DEPENDENCY_BY_REGION_BIT — single viewport)

    // --- Step 4: vkCreateRenderPass ---
    // =========================================================================
    // Teaching note: unlike GL where framebuffer creation is one call,
    // Vulkan requires the render pass to be created FIRST because the
    // framebuffer references it. The VkRenderPass is immutable after
    // creation — changing attachments requires a new render pass.
    // =========================================================================

    VkRenderPassCreateInfo rpCI{};
    rpCI.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCI.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
    rpCI.pAttachments    = attachmentDescs.data();
    rpCI.subpassCount    = 1;
    rpCI.pSubpasses      = &subpass;
    rpCI.dependencyCount = 1;
    rpCI.pDependencies   = &dependency;

    VK_CHECK(vkCreateRenderPass(m_device, &rpCI, nullptr, &m_renderPass),
             "vkCreateRenderPass failed");

    // --- Step 5: Determine extent + validate dimension consistency ---
    // =========================================================================
    // Teaching point — framebuffer compatibility: all attachments in a
    // single VkFramebuffer MUST have identical dimensions. This is a Vulkan
    // spec requirement (VUID-VkFramebufferCreateInfo-pAttachments-00881).
    // GL has the same rule but enforces it implicitly via "framebuffer
    // completeness" rules — Vulkan makes you assert it yourself.
    //
    // rhi_verify usage: 1 color RGBA8 (800x600) + 1 depth D24S8 (800x600).
    // =========================================================================

    auto firstColor = m_colorAttachments.begin()->second;
    int refWidth  = firstColor.texture->width();
    int refHeight = firstColor.texture->height();
    m_extent = { static_cast<uint32_t>(refWidth), static_cast<uint32_t>(refHeight) };

    for (auto& [index, att] : m_colorAttachments) {
        assert(att.texture->width()  == refWidth  && "VKFramebuffer: color attachment dimension mismatch");
        assert(att.texture->height() == refHeight && "VKFramebuffer: color attachment dimension mismatch");
    }
    if (hasDepth) {
        assert(m_depthAttachment.texture->width()  == refWidth  && "VKFramebuffer: depth attachment dimension mismatch");
        assert(m_depthAttachment.texture->height() == refHeight && "VKFramebuffer: depth attachment dimension mismatch");
    }

    // --- Step 6: vkCreateFramebuffer ---
    // =========================================================================
    // Teaching note: the VkFramebuffer binds concrete VkImageViews to the
    // render pass slots. The order of pAttachments here MUST match the order
    // of VkAttachmentDescription in the render pass (color attachments in
    // index order, then depth).
    //
    // Note on mipLevel/layer: in this phase both are always 0 (C4/C5 scope).
    // In a future phase with mipmaps or texture arrays, each attachment would
    // need a separate VkImageView created with the correct subresourceRange
    // (baseMipLevel, levelCount, baseArrayLayer, layerCount). GL ignores the
    // `level` parameter in glFramebufferTexture for layered attachments;
    // Vulkan requires you to bake it into the VkImageView at creation time.
    // This separation is actually MORE correct — different attachments in the
    // same framebuffer can come from different mip levels or array layers of
    // the same underlying VkImage.
    // =========================================================================

    VkFramebufferCreateInfo fbCI{};
    fbCI.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCI.renderPass      = m_renderPass;
    fbCI.attachmentCount = static_cast<uint32_t>(attachmentViews.size());
    fbCI.pAttachments    = attachmentViews.data();
    fbCI.width           = m_extent.width;
    fbCI.height          = m_extent.height;
    fbCI.layers          = 1;  // C5: no texture arrays

    VK_CHECK(vkCreateFramebuffer(m_device, &fbCI, nullptr, &m_framebuffer),
             "vkCreateFramebuffer failed");

    spdlog::info("VKFramebuffer: created {} color + {} depth, {}x{}",
                 colorCount, hasDepth ? "1" : "0", m_extent.width, m_extent.height);

    return true;
}
