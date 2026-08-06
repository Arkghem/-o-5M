#ifndef __VKSHADERRESOURCEBINDINGS_H__
#define __VKSHADERRESOURCEBINDINGS_H__

// ===========================================================================
// VKShaderResourceBindings — Vulkan descriptor set implementation
// ===========================================================================
//
// Teaching note: OpenGL binds resources to shader slots via a simple
// glBindTextureUnit(GLuint unit, GLuint texture) + glBindBufferBase(GLenum
// target, GLuint index, GLuint buffer) model. The driver maps these indices
// to GPU registers internally — you never see how.
//
// Vulkan makes resource binding EXPLICIT through descriptor sets:
//
//   VkDescriptorSetLayout  — declares "binding 0 is a uniform buffer,
//                             binding 1 is a combined image sampler".
//                             This is the contract between shader and app.
//   VkDescriptorPool       — pre-allocates memory for descriptor sets so
//                             vkAllocateDescriptorSets is a cheap bump-
//                             allocator operation (like a slab allocator).
//   VkDescriptorSet        — the actual per-draw resource handles: which
//                             VkBuffer/VkImageView/VkSampler fills each
//                             binding.
//   VkPipelineLayout       — bundles descriptor set layouts + push constant
//                             ranges; passed to vkCmdBindDescriptorSets and
//                             vkCreateGraphicsPipelines.
//
// This class mirrors GLShaderResourceBindings: accumulate bindings in maps,
// then "bake" them into GPU objects in create(). Unlike GL where create() is
// a no-op, Vulkan actually allocates real GPU descriptors.
//
// Sampler/image separation: In GL, filter/wrap state lives inside the texture
// (glTexParameteri). Vulkan splits VkImageView (which pixels) from VkSampler
// (how to sample). This class uses a single default sampler (m_defaultSampler)
// for all texture bindings — per-texture sampler configurability is deferred
// to a later phase.
//
// Pipeline layout compatibility: All pipelines that use this shader resource
// bindings object must be created with the SAME VkPipelineLayout (or a
// compatible one). This class owns the layout so callers get it via
// pipelineLayout().
// ===========================================================================

#include "IShaderResourceBindings.h"

#include <vulkan/vulkan.h>

#include <map>

class VKTexture;
class VKBuffer;

class VKShaderResourceBindings : public IShaderResourceBindings {
public:
    /// @param device          VkDevice handle from VulkanDevice::device().
    /// @param defaultSampler  VkSampler from VulkanDevice::defaultSampler(),
    ///                        used for all texture bindings. In Vulkan, the
    ///                        sampler is separate from the image — the same
    ///                        VkImageView can be sampled with different
    ///                        filters by different shaders.
    VKShaderResourceBindings(VkDevice device, VkSampler defaultSampler);

    /// Destroy descriptor pool, pipeline layout, and descriptor set layout
    /// in reverse creation order. VkDescriptorSet is freed implicitly when
    /// the pool is destroyed (pool owns set memory).
    ~VKShaderResourceBindings() override;

    // --- IShaderResourceBindings interface ---

    /// Accumulate a uniform buffer binding. Mapping is binding index →
    /// (buffer pointer, offset, size). No GPU work; deferred until create().
    void bindUniformBuffer(int binding, IBuffer* buffer,
                           size_t offset = 0, size_t size = 0) override;

    /// Accumulate a texture binding. Mapping is binding index → texture pointer.
    /// No GPU work; deferred until create().
    void bindTexture(int binding, ITexture* texture) override;

    /// Build VkDescriptorSetLayout → VkPipelineLayout → VkDescriptorPool →
    /// VkDescriptorSet → vkUpdateDescriptorSets.
    /// Returns true on success; VK_CHECK + assert on any Vulkan error.
    bool create() override;

    // --- Vulkan-specific accessors ---

    /// The allocated descriptor set — passed to vkCmdBindDescriptorSets.
    VkDescriptorSet descriptorSet() const { return m_set; }

    /// The pipeline layout owning this descriptor set layout — passed to
    /// vkCmdBindDescriptorSets AND vkCreateGraphicsPipelines.
    VkPipelineLayout pipelineLayout() const { return m_pipelineLayout; }

private:
    // --- Binding data (accumulated by bind*(), consumed by create()) ---

    struct UBOBinding {
        VKBuffer* buffer = nullptr;
        size_t    offset = 0;
        size_t    size   = 0;
    };

    struct TexBinding {
        VKTexture* texture = nullptr;
    };

    std::map<int, UBOBinding> m_UBOs;
    std::map<int, TexBinding> m_textures;

    // --- Vulkan handles ---

    VkDevice   m_device         = VK_NULL_HANDLE;
    VkSampler  m_defaultSampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_setLayout      = VK_NULL_HANDLE;
    VkDescriptorPool      m_pool           = VK_NULL_HANDLE;
    VkDescriptorSet       m_set            = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
};

#endif // __VKSHADERRESOURCEBINDINGS_H__
