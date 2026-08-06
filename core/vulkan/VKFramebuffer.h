#ifndef __VKFRAMEBUFFER_H__
#define __VKFRAMEBUFFER_H__

// ===========================================================================
// VKFramebuffer — Vulkan RHI IFramebuffer implementation
// ===========================================================================
//
// Teaching note: In OpenGL, a framebuffer is a single object:
//   glCreateFramebuffers → glNamedFramebufferTexture → done.
//
// Vulkan splits this into TWO objects:
//   VkRenderPass  — describes WHAT the attachments are and HOW they are used
//                   (format, load/store ops, layout transitions). This is the
//                   "render pipeline metadata" — it tells the GPU driver how
//                   to schedule tile-based rendering and when to insert barriers.
//   VkFramebuffer — binds concrete VkImageViews to the render pass slots.
//                   Analogous to GL's framebuffer object.
//
// Why the split? Tile-based GPUs (Apple, ARM Mali, Qualcomm Adreno) render
// by dividing the screen into tiles that fit in on-chip memory. The render
// pass tells the driver which attachments stay on-chip (transient) vs. need
// write-back to main memory. OpenGL guesses based on heuristics; Vulkan
// demands you declare intent. This is why Vulkan can be faster — the driver
// does less guesswork.
//
// Render pass synthesis (create()):
//   1. Collect attachment descriptions from color + depth textures
//   2. Build single subpass (C8: no multi-subpass in this phase)
//   3. Subpass dependency: EXTERNAL → 0 with explicit pipeline barriers
//      (GL does this implicitly via glDraw*; Vulkan requires explicit sync)
//   4. vkCreateRenderPass
//   5. vkCreateFramebuffer (all attachments must have same dimensions — assert)
//
// Design mirror: GLFramebuffer (core/opengl/GLFramebuffer.h) — same
// IFramebuffer interface, different GPU API underneath.
// ===========================================================================

#include <vulkan/vulkan.h>

#include <map>

#include "IFramebuffer.h"

class VKTexture;

class VKFramebuffer : public IFramebuffer {
public:
    /// Construct with device handle (sourced from VulkanDevice::device()).
    /// No physicalDevice needed — render pass/framebuffer creation doesn't
    /// query physical device properties.
    explicit VKFramebuffer(VkDevice device);

    ~VKFramebuffer() override;

    // --- IFramebuffer interface ---

    /// Register a color attachment at `index` (0-based, maps to
    /// VK_COLOR_ATTACHMENT_BIT in subpass). Texture must have RENDERTARGET flag.
    /// mipLevel/layer: in this phase always 0 (C4/C5); Vulkan honors these
    /// in VkImageView creation, unlike GL which ignores glFramebufferTexture's
    /// level param for layered attachments.
    void attachColor(int index, ITexture* texture, int mipLevel = 0, int layer = 0) override;

    /// Register a depth/stencil attachment. Texture must have a depth format
    /// (D16_UNORM / D24_UNORM_S8_UINT / D32_SFLOAT).
    void attachDepthStencil(ITexture* texture, int mipLevel = 0, int layer = 0) override;

    /// Synthesize VkRenderPass from attachment formats → create VkFramebuffer.
    /// Returns false if no color attachments, dimension mismatch, or Vulkan error.
    bool create() override;

    // --- Vulkan-specific accessors (used by render pass executor) ---

    VkRenderPass  renderPass()  const { return m_renderPass; }
    VkFramebuffer framebuffer() const { return m_framebuffer; }
    VkExtent2D    extent()      const { return m_extent; }
    int           colorCount()  const { return static_cast<int>(m_colorAttachments.size()); }

private:
    struct Attachment {
        VKTexture* texture;
        int        mipLevel;
        int        layer;
    };

    std::map<int, Attachment> m_colorAttachments;
    Attachment                m_depthAttachment;

    VkDevice      m_device       = VK_NULL_HANDLE;
    VkRenderPass  m_renderPass   = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer  = VK_NULL_HANDLE;
    VkExtent2D    m_extent       = {0, 0};
};

#endif // __VKFRAMEBUFFER_H__
