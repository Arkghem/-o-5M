#include "VKCommandBuffer.h"
#include "VKPipeline.h"
#include "VKFramebuffer.h"
#include "VKBuffer.h"
#include "VKShaderResourceBindings.h"
#include <spdlog/spdlog.h>
#include <cassert>
#include <cstring>

// ===========================================================================
// VK_CHECK — file-local assertion macro
// ===========================================================================
// Teaching note: Vulkan recording calls (vkCmd*) return void — validation
// errors from these are caught by the validation layers at submit time, not
// at record time. So we use VK_CHECK only where there IS a VkResult return
// (currently none in this file). But vkEndCommandBuffer DOES return VkResult.
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

VKCommandBuffer::VKCommandBuffer() = default;

VKCommandBuffer::~VKCommandBuffer() {
    // Teaching: nothing to destroy — VkCommandBuffer ownership lives in
    // VkCommandPool, which is owned by VKRhi. When the pool is destroyed,
    // all its command buffers are freed automatically. This is different
    // from GL where there's no command buffer concept at all.
}

// ─────────────────────────────────────────────────────────────────────────────
// beginPass — begin a render pass (swapchain or off-screen framebuffer)
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: why Vulkan needs explicit render pass begin/end vs GL's implicit FBO bind.
// =====================================================================
// OpenGL: glBindFramebuffer(GL_FRAMEBUFFER, fbo) — one call, the driver
// internally knows it's starting a new "render pass" for the next draw.
// The driver inserts barriers and layout transitions implicitly based on
// what attachments are bound and what draw commands follow.
//
// Vulkan: vkCmdBeginRenderPass — you tell the driver:
//   1. WHICH render pass (VkRenderPass — the metadata declaring attachment
//      formats, load/store ops, and subpass structure).
//   2. WHICH framebuffer (VkFramebuffer — the concrete VkImageViews).
//   3. WHAT clearance values were used (VkClearValue[]).
//
// This is NOT boilerplate. Tile-based GPUs (Apple Silicon, Mali, Adreno)
// use the render pass to decide which attachments stay in on-chip tile
// memory and which go to VRAM. Declaring intent up front lets the driver
// optimize the tile pipeline — GL's heuristic-based approach can't match
// Vulkan's explicit design for tile efficiency.

void VKCommandBuffer::beginPass(IFramebuffer* fb,
                                 const ClearValue& colorClear,
                                 const ClearValue& depthClear) {
    if (!m_recording) {
        spdlog::warn("beginPass called outside of beginRecording/endRecording block");
        return;
    }

    VkRenderPass  renderPass  = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkExtent2D    extent      = {800, 600};
    int           colorCount  = 0;
    bool          hasDepth    = false;

    if (fb != nullptr) {
        // ── Off-screen framebuffer (e.g., shadow map, GBuffer, post-process) ──
        auto* vkFb = static_cast<VKFramebuffer*>(fb);
        renderPass  = vkFb->renderPass();
        framebuffer = vkFb->framebuffer();
        extent      = vkFb->extent();
        colorCount  = vkFb->colorCount();
        hasDepth    = depthClear.active;  // depth attachment exists if depth clear is active
        m_currentFramebuffer = vkFb;
    } else {
        // ── Swapchain rendering (present to screen) ──
        // Teaching: beginPass(nullptr) is the "draw to screen" convention.
        // The swapchain framebuffer/render pass are per-frame (each swapchain
        // image needs its own VkFramebuffer), set by VKRhi in beginRecording().
        renderPass  = m_swapchainRp;
        framebuffer = m_swapchainFb;
        extent      = m_swapchainExtent;
        colorCount  = 1;    // swapchain always has 1 color attachment
        hasDepth    = false; // no depth on swapchain (depthless presentation)
        m_currentFramebuffer = nullptr;
    }

    assert(renderPass != VK_NULL_HANDLE && "No render pass available");
    assert(framebuffer != VK_NULL_HANDLE && "No framebuffer available");

    // ── Build clear values array ──────────────────────────────────────────
    // Teaching: VkClearValue matches ClearValue layout (see ClearValue.h
    // comments). color[4] → VkClearColorValue, depth/stencil → VkClearDepthStencilValue.
    // We memcpy for zero-cost conversion without per-field copying.
    std::vector<VkClearValue> clearValues;
    clearValues.reserve(colorCount + (hasDepth ? 1 : 0));

    for (int i = 0; i < colorCount; i++) {
        clearValues.push_back(toVkClearValue(colorClear));
    }
    if (hasDepth) {
        clearValues.push_back(toVkClearValue(depthClear));
    }

    // ── Build render pass begin info ──────────────────────────────────────
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass      = renderPass;
    renderPassInfo.framebuffer     = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues    = clearValues.data();

    // Teaching: VK_SUBPASS_CONTENTS_INLINE means all subsequent drawing
    // commands are recorded directly into this command buffer (not via
    // secondary command buffers). This is the simplest mode — equivalent
    // to how GL always works.
    vkCmdBeginRenderPass(m_cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

// ─────────────────────────────────────────────────────────────────────────────
// endPass
// ─────────────────────────────────────────────────────────────────────────────

void VKCommandBuffer::endPass() {
    // Teaching: vkCmdEndRenderPass transitions all attachment layouts to
    // their finalLayout (declared in VkAttachmentDescription) and ends the
    // subpass. After this, no more draw calls until a new vkCmdBeginRenderPass.
    // GL's glBindFramebuffer(0) is the analog — but it's a state change,
    // not an explicit "end".
    vkCmdEndRenderPass(m_cmd);
}

// ─────────────────────────────────────────────────────────────────────────────
// setGraphicPipeline — bind a PSO, lazy-create if needed
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: Vulkan pipelines are render-pass-aware. The SAME shaders with the
// SAME rasterizer/depth/blend state need a DIFFERENT VkPipeline per render pass
// because the render pass declares attachment formats and load/store ops.
// GL has no concept of a render pass, so a single glProgram can render to any
// FBO. This is Vulkan's "double-edged sword": more control + better driver
// optimization, but more pipeline objects to manage.
//
// Lazy creation: if the pipeline hasn't been built yet (isValid() == false),
// we create it now using the CURRENT render pass and extent. This matches
// how GL works — state is set just before the draw — but in a real engine,
// pipelines would be pre-baked during asset loading for performance.

void VKCommandBuffer::setGraphicPipeline(IGraphicsPipeline* pso) {
    auto* vkp = static_cast<VKPipeline*>(pso);

    // ── Lazy pipeline creation ────────────────────────────────────────────
    // If the pipeline hasn't been built yet, create it now using the current
    // render pass + extent. This requires a framebuffer to be bound (swapchain
    // or off-screen).
    if (!vkp->isValid()) {
        VkRenderPass rp;
        VkExtent2D   ext;

        if (m_currentFramebuffer) {
            rp  = m_currentFramebuffer->renderPass();
            ext = m_currentFramebuffer->extent();
        } else {
            // Using swapchain render pass (beginPass(nullptr) was called)
            rp  = m_swapchainRp;
            ext = m_swapchainExtent;
        }

        if (rp == VK_NULL_HANDLE) {
            spdlog::error("Cannot create pipeline: no render pass available");
            return;
        }

        spdlog::info("Lazy-creating VkPipeline (first use)");
        if (!vkp->create(rp, ext)) {
            spdlog::error("Failed to create VkPipeline");
            return;
        }
    }

    m_currentPipeline = vkp;

    // Teaching: vkCmdBindPipeline bundles all render state — blend, depth,
    // rasterizer, shader stages — into ONE call. GL scatters this across
    // glUseProgram + glEnable(GL_DEPTH_TEST) + glBlendFunc + glCullFace + ...
    // The GPU pipeline state is a single monolithic blob; Vulkan reflects this.
    vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkp->pipeline());
}

// ─────────────────────────────────────────────────────────────────────────────
// setViewport — Y-axis flip for GL→Vulkan NDC convention
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: NEGATIVE HEIGHT TRICK to flip Y-axis.
// =====================================================================
// GL viewport origin: BOTTOM-LEFT corner.  (0,0) = bottom-left.
// Vulkan viewport origin: TOP-LEFT corner.  (0,0) = top-left.
//
// The vertex shader outputs clip-space coordinates in a convention determined
// by the shader, not the API. If shaders were authored for GL (Y-up NDC,
// where +Y goes UP the screen), they produce clip-space Y that assumes the
// viewport maps Y+1 (top) to the top of the window.
//
// Vulkan's native viewport mapping:
//   x_screen = viewport.x + (NDC.x + 1) * viewport.width  / 2
//   y_screen = viewport.y + (NDC.y + 1) * viewport.height / 2
// BUT Vulkan's NDC has Y=-1 at the TOP (upper-left origin).
//
// Without the flip, a GL-authored shader's +Y (which it thinks is "up")
// would point DOWN on screen. The negative height trick inverts this:
//
//   VkViewport{ x, y + h, w, -h, 0, 1 }
//
//   With negative height:
//     y_screen = (y + h) + (NDC.y + 1) * (-h) / 2
//              = y + h - (NDC.y + 1) * h / 2
//   At NDC.y = +1 (GL "top"):   y_screen = y + h - h = y         (bottom → actually top after flip)
//   At NDC.y = -1 (GL "bottom"): y_screen = y + h - 0 = y + h    (top → actually bottom after flip)
//
// Wait — that seems backwards. Let's re-derive.
//
// In Vulkan's viewport mapping, the viewport origin (viewport.y) is the
// TOP of the image. The extent (viewport.height, including negative) determines
// the direction.
//
// With height = -h:
//   Pixel Y = viewport.y + (1 - (NDC.y + 1)/2) * (-h)   [Vulkan spec: y = viewport.y + viewport.height * (1 - (ndc.y+1)/2)]
// Wait, the spec says: y_f = viewport.y + viewport.height * (T - t)
// where T = 1/2 and t = NDC.y / 2... Actually the spec (25.5) says:
//   x_f = viewport.x + viewport.width  * (p_x + 1) / 2
//   y_f = viewport.y + viewport.height * (p_y + 1) / 2
// where (p_x, p_y) are the normalized device coordinates.
//
// So with height = -h, viewport.y = y + h:
//   y_f = (y + h) + (-h) * (NDC.y + 1) / 2
//       = y + h - h*(NDC.y + 1)/2
// For NDC.y = +1: y_f = y + h - h = y        (maps to physical y, which is TOP in Vulkan)
// For NDC.y = -1: y_f = y + h - 0 = y + h    (maps to physical y+h, which is BOTTOM in Vulkan)
//
// But with a standard positive viewport (height = h, viewport.y = y):
//   y_f = y + h * (NDC.y + 1) / 2
// For NDC.y = -1: y_f = y                     (maps to physical y, which is TOP in Vulkan → bottom-left in GL)
// For NDC.y = +1: y_f = y + h                 (maps to physical y+h, which is BOTTOM in Vulkan → top-left in GL)
//
// So with the negative height trick:
// GL NDC +1 (top) → Vulkan physical y (top, since Vulkan viewport.y IS the top edge)
// GL NDC -1 (bottom) → Vulkan physical y+h (bottom)
//
// This makes GL-authored shaders work WITHOUT modifying the projection matrix.
// The alternative is to flip Y in the projection matrix, which changes ALL
// downstream math (winding order, culling, stencil). The negative height trick
// is a viewport-only transform that leaves clip-space untouched.
//
//   VkViewport{x, y+h, w, -h, minDepth=0, maxDepth=1}
//   ────────────────────────────────────────────
//   Teaching: GL viewport origin bottom-left; Vulkan top-left.
//   Negative height mirrors GL's NDC convention so shaders port without
//   matrix changes.

void VKCommandBuffer::setViewport(int x, int y, int w, int h) {
    // ── GL bottom-left → Vulkan top-left via negative height ─────────────
    VkViewport viewport{};
    viewport.x        = static_cast<float>(x);
    viewport.y        = static_cast<float>(y + h);  // offset to GL's "top"
    viewport.width    = static_cast<float>(w);
    viewport.height   = static_cast<float>(-h);     // NEGATIVE: flip Y direction
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(m_cmd, 0, 1, &viewport);
}

// ─────────────────────────────────────────────────────────────────────────────
// setScissor
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: scissor uses absolute pixel coordinates (no NDC mapping), so NO
// Y-flip is needed. The scissor rectangle (x, y, w, h) means the same region
// in both GL and Vulkan: pixels from (x, y) to (x+w, y+h). The viewport's
// Y-flip doesn't affect scissor because scissor clips in framebuffer space,
// not NDC space.

void VKCommandBuffer::setScissor(int x, int y, int w, int h) {
    VkRect2D scissor{};
    scissor.offset.x      = x;
    scissor.offset.y      = y;
    scissor.extent.width  = static_cast<uint32_t>(w);
    scissor.extent.height = static_cast<uint32_t>(h);

    vkCmdSetScissor(m_cmd, 0, 1, &scissor);
}

// ─────────────────────────────────────────────────────────────────────────────
// setShaderResources — bind descriptor set
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: GL binds resources one-at-a-time (glBindBufferRange per UBO,
// glBindTextureUnit per texture — scattered across the draw preamble).
// Vulkan's vkCmdBindDescriptorSets binds an ENTIRE set at once — all UBOs,
// all textures, all samplers in one call. This is more efficient because
// the driver validates the complete binding table once instead of per-slot.
// It also matches how GPUs work: descriptors are stored in a contiguous table
// in GPU-visible memory.

void VKCommandBuffer::setShaderResources(IShaderResourceBindings* bindings) {
    auto* vkBind = static_cast<VKShaderResourceBindings*>(bindings);

    if (!m_currentPipeline) {
        spdlog::warn("setShaderResources called without a bound pipeline");
        return;
    }

    VkDescriptorSet set = vkBind->descriptorSet();
    if (set == VK_NULL_HANDLE) {
        // Empty bindings — pipeline layout has no descriptor sets. This is
        // valid for shaders that use only in/out varyings (no uniforms).
        return;
    }

    // Teaching: pipelineLayout MUST match the layout used when the pipeline
    // was created (VkGraphicsPipelineCreateInfo::layout). In a future phase,
    // this will be enforced by making pipeline creation take a
    // VKShaderResourceBindings* and using its pipelineLayout().
    vkCmdBindDescriptorSets(m_cmd,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_currentPipeline->layout(),  // pipeline layout
                            0,                             // first set index
                            1,                             // descriptor set count
                            &set,                          // descriptor sets
                            0,                             // dynamic offset count
                            nullptr);                      // dynamic offsets
}

// ─────────────────────────────────────────────────────────────────────────────
// setVertexInput — accumulate binding (deferred to draw)
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: same pattern as GLCommandBuffer — accumulate bindings in a map,
// reconstruct VAO-equivalent arrays at draw time. Vulkan doesn't have VAOs;
// vkCmdBindVertexBuffers takes arrays of VkBuffer + VkDeviceSize directly.
// This is actually SIMPLER than GL — no vertex-array-object indirection.

void VKCommandBuffer::setVertexInput(int bindingSlot, IBuffer* buffer, size_t offset) {
    auto* vkBuffer = static_cast<VKBuffer*>(buffer);
    m_vertexBindings[bindingSlot] = {vkBuffer, offset};
}

// ─────────────────────────────────────────────────────────────────────────────
// setIndexBuffer
// ─────────────────────────────────────────────────────────────────────────────

void VKCommandBuffer::setIndexBuffer(IBuffer* buffer, E_INDEX_FORMAT format) {
    m_indexBuffer = static_cast<VKBuffer*>(buffer);
    m_indexFormat = format;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildVertexInputArrays — helper for draw* methods
// ─────────────────────────────────────────────────────────────────────────────

void VKCommandBuffer::buildVertexInputArrays(std::vector<VkBuffer>& buffers,
                                              std::vector<VkDeviceSize>& offsets) const {
    if (!m_currentPipeline || m_vertexBindings.empty()) {
        return;
    }

    const auto& vtxLayout = m_currentPipeline->vertexInputLayout();

    // Build VkBuffer + VkDeviceSize arrays indexed by binding slot.
    // The pipeline's vertex layout declares which binding slots exist;
    // our stored m_vertexBindings map connects slots to concrete buffers.
    size_t maxBinding = 0;
    for (const auto& [slot, _] : m_vertexBindings) {
        if (static_cast<size_t>(slot) >= maxBinding) {
            maxBinding = static_cast<size_t>(slot) + 1;
        }
    }

    buffers.resize(maxBinding);
    offsets.resize(maxBinding);

    for (int i = 0; i < static_cast<int>(maxBinding); i++) {
        auto it = m_vertexBindings.find(i);
        if (it != m_vertexBindings.end()) {
            buffers[i] = it->second.first->handle();
            offsets[i] = static_cast<VkDeviceSize>(it->second.second);
        } else {
            // Unused binding slot — Vulkan requires either valid handle or
            // we could set to VK_NULL_HANDLE and skip. For simplicity, set
            // to null; vkCmdBindVertexBuffers tolerates null handles if the
            // pipeline doesn't reference the slot.
            buffers[i] = VK_NULL_HANDLE;
            offsets[i] = 0;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// draw — indexed by vertex buffer (no index buffer)
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: vkCmdBindVertexBuffers + vkCmdDraw = Vulkan's equivalent of
// GL's glBindVertexArray + glDrawArrays. But Vulkan has NO VAO indirection —
// you pass VkBuffer handles directly. This is simpler at the call site but
// requires the application to manage buffer bindings explicitly per draw.
//
// GL's VAO stores all vertex buffer bindings in a GPU-side table so that
// glBindVertexArray(vao) is a single state-switch. Vulkan's approach is
// equivalent to GL's direct state access — no hidden state.

void VKCommandBuffer::draw(int vertexCount, int firstVertex) {
    // ── Defensive guard ───────────────────────────────────────────────────
    // Same pattern as GLCommandBuffer: no-op draw if pipeline or framebuffer
    // not set. Prevents crash from misordered calls during development.
    if (!m_currentPipeline || !m_currentFramebuffer) {
        if (!m_currentFramebuffer) {
            // Might be a swapchain render pass (beginPass(nullptr) → swapchain).
            // In that case, allow the draw — the framebuffer is set by the swapchain.
            // For now, guard conservatively: only allow if swapchain is set.
            if (m_swapchainFb == VK_NULL_HANDLE) {
                spdlog::warn("draw() skipped: no framebuffer bound");
                return;
            }
        } else {
            // We have a framebuffer but no pipeline. Skip.
            if (!m_currentPipeline) {
                spdlog::warn("draw() skipped: no pipeline bound");
                return;
            }
        }
    }

    // ── Bind vertex buffers ───────────────────────────────────────────────
    const auto& vtxLayout = m_currentPipeline->vertexInputLayout();
    std::vector<VkBuffer>     vkBuffers;
    std::vector<VkDeviceSize> vkOffsets;
    buildVertexInputArrays(vkBuffers, vkOffsets);

    if (!vkBuffers.empty()) {
        vkCmdBindVertexBuffers(m_cmd,
                               0,                       // first binding
                               static_cast<uint32_t>(vkBuffers.size()),
                               vkBuffers.data(),
                               vkOffsets.data());
    }

    // ── Draw ──────────────────────────────────────────────────────────────
    // Teaching: vkCmdDraw takes vertexCount + instanceCount — instanced and
    // non-instanced use the SAME function. GL splits them into glDrawArrays
    // + glDrawArraysInstanced. Vulkan unifies them because every draw is
    // technically an instanced draw with instanceCount=1 by default.
    vkCmdDraw(m_cmd,
              static_cast<uint32_t>(vertexCount),
              1,                                 // instanceCount=1 (non-instanced)
              static_cast<uint32_t>(firstVertex),
              0);                                // firstInstance
}

// ─────────────────────────────────────────────────────────────────────────────
// drawIndexed — indexed draw with element buffer
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: Vulkan's index type is embedded in the draw command call itself
// (VK_INDEX_TYPE_UINT16 vs VK_INDEX_TYPE_UINT32), NOT in the buffer bind.
// GL uses glDrawElements(..., GL_UNSIGNED_SHORT, ...) — same concept, different
// API surface. The index buffer is bound separately via vkCmdBindIndexBuffer.

void VKCommandBuffer::drawIndexed(int indexCount, int firstIndex, int vertexOffset) {
    if (!m_currentPipeline) {
        spdlog::warn("drawIndexed() skipped: no pipeline bound");
        return;
    }

    // ── Bind vertex buffers ───────────────────────────────────────────────
    std::vector<VkBuffer>     vkBuffers;
    std::vector<VkDeviceSize> vkOffsets;
    buildVertexInputArrays(vkBuffers, vkOffsets);

    if (!vkBuffers.empty()) {
        vkCmdBindVertexBuffers(m_cmd, 0,
                               static_cast<uint32_t>(vkBuffers.size()),
                               vkBuffers.data(), vkOffsets.data());
    }

    // ── Bind index buffer ─────────────────────────────────────────────────
    // Teaching: unlike GL where the index buffer is part of the VAO state
    // (glVertexArrayElementBuffer), Vulkan binds it separately per draw.
    // This allows using the same vertex data with different index buffers
    // without recreating a VAO — natural in Vulkan, awkward in GL.
    if (m_indexBuffer) {
        VkIndexType indexType = (m_indexFormat == UINT32)
                                    ? VK_INDEX_TYPE_UINT32
                                    : VK_INDEX_TYPE_UINT16;
        vkCmdBindIndexBuffer(m_cmd, m_indexBuffer->handle(), 0, indexType);
    }

    // ── Indexed draw ──────────────────────────────────────────────────────
    // Teaching: firstIndex * sizeof(index) is handled by Vulkan internally
    // (unlike GL where you pass a byte-offset pointer). The indexCount
    // parameter is the number of indices, not vertices.
    vkCmdDrawIndexed(m_cmd,
                     static_cast<uint32_t>(indexCount),
                     1,                                 // instanceCount
                     static_cast<uint32_t>(firstIndex),
                     static_cast<int32_t>(vertexOffset),
                     0);                                // firstInstance
}

// ─────────────────────────────────────────────────────────────────────────────
// drawInstanced
// ─────────────────────────────────────────────────────────────────────────────

void VKCommandBuffer::drawInstanced(int vertexCount, int instanceCount,
                                     int firstVertex, int firstInstance) {
    if (!m_currentPipeline) {
        spdlog::warn("drawInstanced() skipped: no pipeline bound");
        return;
    }
    if (!m_currentFramebuffer && m_swapchainFb == VK_NULL_HANDLE) {
        spdlog::warn("drawInstanced() skipped: no framebuffer bound");
        return;
    }

    // ── Bind vertex buffers ───────────────────────────────────────────────
    std::vector<VkBuffer>     vkBuffers;
    std::vector<VkDeviceSize> vkOffsets;
    buildVertexInputArrays(vkBuffers, vkOffsets);

    if (!vkBuffers.empty()) {
        vkCmdBindVertexBuffers(m_cmd, 0,
                               static_cast<uint32_t>(vkBuffers.size()),
                               vkBuffers.data(), vkOffsets.data());
    }

    // ── Instanced draw ────────────────────────────────────────────────────
    // Teaching: Same vkCmdDraw function as non-instanced — instanceCount > 1
    // activates hardware instancing. The vertex shader receives gl_InstanceIndex
    // (or gl_InstanceID in GLSL) to differentiate instances. GL needs a separate
    // glDrawArraysInstanced entry point; Vulkan unifies them.
    vkCmdDraw(m_cmd,
              static_cast<uint32_t>(vertexCount),
              static_cast<uint32_t>(instanceCount),
              static_cast<uint32_t>(firstVertex),
              static_cast<uint32_t>(firstInstance));
}

// ─────────────────────────────────────────────────────────────────────────────
// pushDebugGroup / popDebugGroup
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: Vulkan debug labels require the VK_EXT_debug_utils extension.
// The function pointers vkCmdBeginDebugUtilsLabelEXT / vkCmdEndDebugUtilsLabelEXT
// are loaded dynamically via vkGetInstanceProcAddr from VulkanDevice. Since
// this class doesn't depend on VulkanDevice (decoupled by design), debug
// group support is stubbed out here. A future integration point would pass
// the function pointers from VKRhi during beginRecording().
//
// GL: glPushDebugGroup / glPopDebugGroup — always available in debug context.
// Vulkan: requires extension + dynamic loading — more complex but more explicit.

void VKCommandBuffer::pushDebugGroup(const char* name) {
    // STUB: Debug utils extension not wired yet.
    // Future: VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr, name, {1,0,0,1} };
    //         vkCmdBeginDebugUtilsLabelEXT(m_cmd, &label);
    (void)name;
}

void VKCommandBuffer::popDebugGroup() {
    // STUB: Debug utils extension not wired yet.
    // Future: vkCmdEndDebugUtilsLabelEXT(m_cmd);
}

// ─────────────────────────────────────────────────────────────────────────────
// beginRecording — called by VKRhi at frame start
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: This is the moment the CPU starts recording GPU commands for the
// current frame. In a real engine, VKRhi would reset the command buffer
// (vkResetCommandBuffer or vkBeginCommandBuffer from a fresh pool allocation),
// then call this method. The swapchain framebuffer/render pass change every
// frame because each frame acquires a different swapchain image.

void VKCommandBuffer::beginRecording(VkCommandBuffer cmd,
                                      VkFramebuffer swapchainFb,
                                      VkRenderPass swapchainRp,
                                      VkExtent2D extent) {
    m_cmd            = cmd;
    m_swapchainFb    = swapchainFb;
    m_swapchainRp    = swapchainRp;
    m_swapchainExtent = extent;

    m_currentPipeline    = nullptr;
    m_currentFramebuffer = nullptr;
    m_vertexBindings.clear();
    m_indexBuffer  = nullptr;
    m_indexFormat  = UINT16;
    m_recording    = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// endRecording — finalize command buffer (before vkQueueSubmit)
// ─────────────────────────────────────────────────────────────────────────────
//
// Teaching: vkEndCommandBuffer transitions the command buffer from recording
// state to executable state. After this call, the buffer can be submitted
// to a queue. Any attempt to record more commands will fail with a validation
// error. This is why multi-threaded recording requires separate command pools:
// each thread needs its own pool so resetting doesn't race.

void VKCommandBuffer::endRecording() {
    assert(m_recording && "endRecording() called on non-recording command buffer");
    VK_CHECK(vkEndCommandBuffer(m_cmd), "Failed to end command buffer recording");
    m_recording = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// toVkClearValue — layout-compatible conversion
// ─────────────────────────────────────────────────────────────────────────────

VkClearValue VKCommandBuffer::toVkClearValue(const ClearValue& cv) {
    VkClearValue vkCv{};
    // Teaching: ClearValue is designed to be layout-compatible with VkClearValue.
    // color[4] → VkClearColorValue (same 4-float layout), depth/stencil → VkClearDepthStencilValue.
    // We could memcpy the whole struct (sizeof is 28 bytes: 4 + 16 + 4 + 4),
    // but explicit field assignments are clearer for teaching.
    std::memcpy(vkCv.color.float32, cv.color, sizeof(cv.color));
    vkCv.depthStencil.depth   = cv.depth;
    vkCv.depthStencil.stencil = static_cast<uint32_t>(cv.stencil);
    return vkCv;
}
