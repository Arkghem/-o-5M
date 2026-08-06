#include "VKShaderResourceBindings.h"

#include "VKBuffer.h"
#include "VKTexture.h"

#include <spdlog/spdlog.h>

#include <cassert>
#include <vector>

// ===========================================================================
// VK_CHECK — local assertion macro for Vulkan result codes
// ===========================================================================
// Teaching note: unlike OpenGL's polled glGetError(), Vulkan returns VkResult
// from every vkCreate*/vkAllocate* function and you MUST check each one.
// A missed VK_ERROR_OUT_OF_DEVICE_MEMORY surfaces as a crash many frames later
// with no clear root cause.
// ===========================================================================
#define VK_CHECK(result, msg)                                               \
    do {                                                                    \
        VkResult _res = (result);                                           \
        if (_res != VK_SUCCESS) {                                           \
            spdlog::error("Vulkan error: {} — VkResult={}", msg,             \
                          static_cast<int>(_res));                          \
            assert(false && msg);                                           \
        }                                                                   \
    } while (0)

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

VKShaderResourceBindings::VKShaderResourceBindings(VkDevice device,
                                                   VkSampler defaultSampler)
    : m_device(device)
    , m_defaultSampler(defaultSampler)
{
}

VKShaderResourceBindings::~VKShaderResourceBindings()
{
    // Teaching note: destroy in REVERSE order of creation.
    // VkDescriptorSet is freed implicitly when the pool is destroyed
    // (pool owns set memory — no separate vkFreeDescriptorSets call).
    // Pipeline layout → pool → set layout.
    if (m_pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    if (m_setLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
}

// ===========================================================================
// bindUniformBuffer / bindTexture — accumulate bindings (no GPU work)
// ===========================================================================
//
// Pattern mirrors GLShaderResourceBindings exactly: store in maps, defer all
// GPU allocation to create(). The difference: GL's create() is a no-op because
// glBindBufferRange/glBindTextureUnit are called at draw time. Vulkan's
// descriptor model requires binding information to be pre-baked into a
// VkDescriptorSet via vkUpdateDescriptorSets BEFORE the draw call.

void VKShaderResourceBindings::bindUniformBuffer(int binding, IBuffer* buffer,
                                                  size_t offset, size_t size)
{
    // Cast through IBuffer* — caller guarantees the runtime type is VKBuffer.
    // Same pattern as GLShaderResourceBindings: static_cast<GLBuffer*>(buffer).
    m_UBOs[binding] = { static_cast<VKBuffer*>(buffer), offset, size };
}

void VKShaderResourceBindings::bindTexture(int binding, ITexture* texture)
{
    m_textures[binding] = { static_cast<VKTexture*>(texture) };
}

// ===========================================================================
// create() — build and allocate the descriptor set
// ===========================================================================
//
// Teaching note: this is where Vulkan diverges hard from OpenGL. GL's resource
// binding is dynamic: glBindBufferRange + glBindTextureUnit at draw time,
// driver resolves the slot→handle mapping internally. Vulkan requires you to:
//
//   1. DECLARE the binding contract: VkDescriptorSetLayoutBinding says
//      "binding 0 expects a UNIFORM_BUFFER, binding 1 expects a
//      COMBINED_IMAGE_SAMPLER".
//
//   2. ALLOCATE descriptor memory from a pool: VkDescriptorPool pre-allocates
//      backing memory so vkAllocateDescriptorSets is cheap (like a slab
//      allocator — O(1) bump, no heap fragmentation).
//
//   3. WRITE the actual resource handles: vkUpdateDescriptorSets copies
//      VkBuffer/VkImageView/VkSampler handles INTO the descriptor set.
//      This is a CPU-side memory copy into pool-backed storage.
//
//   4. CREATE a pipeline layout: bundles descriptor set layouts + push
//      constant ranges. Pipelines are validated against their layout at
//      vkCreateGraphicsPipelines time — the driver can statically know
//      exactly which shader stage uses which binding.
//
// After create(), the descriptor set is immutable for this phase (C1: no
// recycling). Dynamic descriptors (set at draw-time with offsets) are a
// later feature.
// ===========================================================================

bool VKShaderResourceBindings::create()
{
    // ------------------------------------------------------------------
    // Step 1: Build VkDescriptorSetLayoutBinding array from accumulated bindings
    // ------------------------------------------------------------------
    // Each binding in the shader must have a corresponding layout binding
    // that declares its descriptor type and the shader stage(s) that access it.
    // For this phase we include ALL stages (vertex + fragment) since we don't
    // have SPIRV-Cross reflection to know which stage uses which.
    // Teaching: VK_SHADER_STAGE_ALL_BITS is a bitmask — the driver uses it
    // to validate that a pipeline referencing this layout actually has shaders
    // that consume these bindings at the expected stages.

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    layoutBindings.reserve(m_UBOs.size() + m_textures.size());

    for (const auto& [binding, ubo] : m_UBOs) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding            = static_cast<uint32_t>(binding);
        layoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        layoutBinding.descriptorCount    = 1;
        layoutBinding.stageFlags         = VK_SHADER_STAGE_ALL;
        layoutBinding.pImmutableSamplers = nullptr;
        layoutBindings.push_back(layoutBinding);
    }

    for (const auto& [binding, tex] : m_textures) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding            = static_cast<uint32_t>(binding);
        layoutBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        layoutBinding.descriptorCount    = 1;
        layoutBinding.stageFlags         = VK_SHADER_STAGE_ALL;
        layoutBinding.pImmutableSamplers = nullptr;
        layoutBindings.push_back(layoutBinding);
    }

    // ------------------------------------------------------------------
    // Step 2: vkCreateDescriptorSetLayout
    // ------------------------------------------------------------------
    // The layout is the "schema" for the descriptor set — downstream
    // descriptors and pipelines must match it exactly. OpenGL has no
    // equivalent; the driver infers schema from glBindBufferRange calls.

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings    = layoutBindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr,
                                         &m_setLayout),
             "vkCreateDescriptorSetLayout");

    // ------------------------------------------------------------------
    // Step 3: vkCreatePipelineLayout
    // ------------------------------------------------------------------
    // Pipeline layout bundles descriptor set layouts + push constant ranges.
    // No push constants in this phase. A pipeline created with this layout
    // must be bound with vkCmdBindDescriptorSets using the compatible
    // descriptor set.

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = (layoutBindings.empty() ? 0u : 1u);
    pipelineLayoutInfo.pSetLayouts    = (layoutBindings.empty() ? nullptr : &m_setLayout);
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    VK_CHECK(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr,
                                    &m_pipelineLayout),
             "vkCreatePipelineLayout");

    // If no bindings were registered, there's no descriptor set to allocate.
    // This is valid (shader that only uses push constants or in/out varyings).
    if (layoutBindings.empty()) {
        spdlog::info("VKShaderResourceBindings::create() — no bindings, "
                     "descriptor set skipped (layout + pipeline layout only)");
        return true;
    }

    // ------------------------------------------------------------------
    // Step 4: vkCreateDescriptorPool
    // ------------------------------------------------------------------
    // The pool pre-allocates memory for descriptor sets. Each pool size entry
    // declares how many descriptors of each TYPE the pool can allocate.
    // maxSets=1 (single descriptor set in this phase — no multi-set support).
    //
    // Teaching: Vulkan pools are NOT global. Each pool has a fixed allocation
    // budget. Exceeding it = VK_ERROR_OUT_OF_POOL_MEMORY at
    // vkAllocateDescriptorSets time. Production engines size pools carefully
    // or recycle sets after frames.

    std::vector<VkDescriptorPoolSize> poolSizes;

    if (!m_UBOs.empty()) {
        VkDescriptorPoolSize uboSize{};
        uboSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboSize.descriptorCount = static_cast<uint32_t>(m_UBOs.size());
        poolSizes.push_back(uboSize);
    }
    if (!m_textures.empty()) {
        VkDescriptorPoolSize texSize{};
        texSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texSize.descriptorCount = static_cast<uint32_t>(m_textures.size());
        poolSizes.push_back(texSize);
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes    = poolSizes.data();
    // No VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT — we free the
    // whole pool at destruction, not individual sets (C1: no recycling).

    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool),
             "vkCreateDescriptorPool");

    // ------------------------------------------------------------------
    // Step 5: vkAllocateDescriptorSets
    // ------------------------------------------------------------------
    // Allocates one set from the pool. The set layout must be compatible
    // with the pool's declared sizes.

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &m_setLayout;

    VK_CHECK(vkAllocateDescriptorSets(m_device, &allocInfo, &m_set),
             "vkAllocateDescriptorSets");

    // ------------------------------------------------------------------
    // Step 6: vkUpdateDescriptorSets — write actual resource handles
    // ------------------------------------------------------------------
    // This is the Vulkan equivalent of GL's glBindBufferRange +
    // glBindTextureUnit, but done ONCE at resource-creation time instead of
    // every draw call. The descriptor set now holds GPU-visible handles.
    //
    // Teaching: VkWriteDescriptorSet uses a DST binding index (which slot in
    // the descriptor set) AND a SRC array element index (for array-of-
    // descriptors). In this phase, each binding has descriptorCount=1,
    // so dstArrayElement is always 0.

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(m_UBOs.size() + m_textures.size());

    // We need to keep VkDescriptorBufferInfo / VkDescriptorImageInfo alive
    // until vkUpdateDescriptorSets returns — they're read by the driver during
    // the call (not referenced afterward).
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo>  imageInfos;
    bufferInfos.reserve(m_UBOs.size());
    imageInfos.reserve(m_textures.size());

    // --- Uniform buffer writes ---
    for (const auto& [binding, ubo] : m_UBOs) {
        // If size == 0, use the entire buffer (VK_WHOLE_SIZE).
        // Same semantics as GL's glBindBufferRange with size=0 → glBindBufferBase.
        VkDeviceSize bindSize = (ubo.size == 0)
            ? VK_WHOLE_SIZE
            : static_cast<VkDeviceSize>(ubo.size);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = ubo.buffer->handle();
        bufferInfo.offset = static_cast<VkDeviceSize>(ubo.offset);
        bufferInfo.range  = bindSize;
        bufferInfos.push_back(bufferInfo);

        VkWriteDescriptorSet write{};
        write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet           = m_set;
        write.dstBinding       = static_cast<uint32_t>(binding);
        write.dstArrayElement  = 0;
        write.descriptorCount  = 1;
        write.descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo      = &bufferInfos.back();
        write.pImageInfo       = nullptr;
        write.pTexelBufferView = nullptr;
        writes.push_back(write);
    }

    // --- Texture writes (combined image sampler) ---
    //
    // Teaching: VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER bundles image AND
    // sampler into one descriptor slot — the shader declares
    // `layout(binding=N) uniform sampler2D tex;` in GLSL and the SPIR-V
    // compiler maps it to a combined image sampler. Vulkan supports separate
    // image+sampler descriptors too (VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE +
    // VK_DESCRIPTOR_TYPE_SAMPLER), but combined is the common path matching
    // GL's mental model.
    //
    // The image MUST be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL before
    // the draw call. VKTexture's upload() transitions to this layout, and
    // we assume the texture has been uploaded before create() is called.
    for (const auto& [binding, tex] : m_textures) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler     = m_defaultSampler;
        imageInfo.imageView   = tex.texture->imageView();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos.push_back(imageInfo);

        VkWriteDescriptorSet write{};
        write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet           = m_set;
        write.dstBinding       = static_cast<uint32_t>(binding);
        write.dstArrayElement  = 0;
        write.descriptorCount  = 1;
        write.descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pBufferInfo      = nullptr;
        write.pImageInfo       = &imageInfos.back();
        write.pTexelBufferView = nullptr;
        writes.push_back(write);
    }

    // All writes in one call — vkUpdateDescriptorSets validates that every
    // binding in the layout is written exactly once.
    vkUpdateDescriptorSets(m_device,
                           static_cast<uint32_t>(writes.size()),
                           writes.data(),
                           0, nullptr);  // no copy operations

    spdlog::info("VKShaderResourceBindings::create() — {} UBOs, {} textures, "
                 "set={}, layout={}",
                 m_UBOs.size(), m_textures.size(),
                 static_cast<const void*>(m_set),
                 static_cast<const void*>(m_setLayout));

    return true;
}
