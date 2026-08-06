#ifndef __VKTEXTURE_H__
#define __VKTEXTURE_H__

// ===========================================================================
// VKTexture — Vulkan RHI ITexture implementation
// ===========================================================================
//
// Teaching note: In OpenGL, a "texture" is a single GLuint handle that bundles
// image storage, filtering state, and mipmap chains together (glTexImage2D +
// glTexParameteri). Vulkan splits these concerns:
//
//   VkImage        — the raw pixel storage (resolution, format, mip levels).
//                     Analogous to glTexStorage2D. Does NOT know how to sample.
//   VkImageView    — a "view" into an image: which aspect (color/depth), which
//                     mip range, which array layer range. Required by shader
//                     descriptors AND framebuffer attachments.
//                     Analogous to GL's texture target + swizzle mask combined.
//   VkSampler      — filtering (linear/nearest), wrapping (repeat/clamp), LOD
//                     bias, anisotropy. Separate object because the SAME image
//                     might be sampled differently in different passes.
//
// This separation exists because real GPUs have separate hardware units for
// texture sampling vs. render-target access. Vulkan exposes reality; OpenGL
// hides it behind a monolithic texture object.
//
// Design mirror: GLTexture (core/opengl/GLTexture.h) — same ITexture interface,
// same TextureDesc config, different GPU API underneath.
// ===========================================================================

#include <vulkan/vulkan.h>

#include "ITexture.h"

class VKTexture : public ITexture {
public:
    /// Construct with descriptor + Vulkan device handles.
    /// device / physicalDevice typically come from VulkanDevice singleton.
    explicit VKTexture(const TextureDesc& desc,
                       VkDevice device,
                       VkPhysicalDevice physicalDevice);

    ~VKTexture() override;

    // --- ITexture interface ---

    /// Create VkImage + VkDeviceMemory + VkImageView from TextureDesc.
    /// Memory: DEVICE_LOCAL (GPU-only, fast access, can't be CPU-mapped).
    /// C3: samples > 1 asserts (no MSAA support at this phase).
    /// C4: miplevels always 1 (no mipmap chains).
    /// C5: layers always 1 (no texture arrays).
    bool create() override;

    /// Upload pixel data via staging buffer copy.
    /// Teaching note: unlike GL's glTextureSubImage2D which does the staging
    /// implicitly, Vulkan requires an explicit host-visible staging buffer →
    /// vkCmdCopyBufferToImage → image layout transitions.
    ///
    /// Flow: staging buffer (HOST_VISIBLE) → vkCmdCopyBufferToImage →
    ///       UNDEFINED → TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL.
    /// Upload is synchronous: submit to queue and wait for completion.
    void upload(const void* data, int miplevel = 0, int layer = 0) override;

    // --- Dimension queries ---
    int width() const override;
    int height() const override;
    int depth() const override;
    int miplevels() const override;
    int layers() const override;
    int samples() const override;

    // --- Vulkan-specific accessors ---

    /// Original descriptor used to create this texture.
    const TextureDesc& desc() const;

    /// VkImageView handle — what shader descriptors and framebuffer
    /// attachments bind to. Not the raw image.
    VkImageView imageView() const;

    /// Raw VkImage handle — rarely needed directly; prefer imageView().
    VkImage image() const;

    /// Mapped Vulkan format (e.g., VK_FORMAT_R8G8B8A8_UNORM).
    VkFormat vkFormat() const;

private:
    // --- Format helpers ---
    static VkFormat     toVkFormat(TextureDesc::E_TEXTURE_FORMAT format);
    static VkImageAspectFlags toAspectMask(TextureDesc::E_TEXTURE_FORMAT format);
    static uint32_t     pixelSize(TextureDesc::E_TEXTURE_FORMAT format);

    /// Determine VkImageUsageFlags from TextureDesc::flags.
    /// RENDERTARGET → COLOR_ATTACHMENT | SAMPLED | TRANSFER_DST.
    /// Depth formats → DEPTH_STENCIL_ATTACHMENT.
    /// Default → SAMPLED | TRANSFER_DST.
    static VkImageUsageFlags deriveUsage(const TextureDesc& desc);

    // --- Layout transition helper ---
    // Used internally by upload(). Transitions image between layouts via
    // pipeline barrier (no actual pixel work — just a GPU cache flush).
    void transitionLayout(VkCommandBuffer cmd,
                          VkImageLayout oldLayout,
                          VkImageLayout newLayout);

    TextureDesc      m_desc;
    VkImage          m_image          = VK_NULL_HANDLE;
    VkDeviceMemory   m_memory         = VK_NULL_HANDLE;
    VkImageView      m_imageView      = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkFormat         m_vkFormat       = VK_FORMAT_UNDEFINED;
};

#endif // __VKTEXTURE_H__
