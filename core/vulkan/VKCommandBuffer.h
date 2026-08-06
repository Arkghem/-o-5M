#ifndef __VKCOMMANDBUFFER_H__
#define __VKCOMMANDBUFFER_H__

// ===========================================================================
// VKCommandBuffer — ICommandBuffer backed by VkCommandBuffer (recorded mode)
// ===========================================================================
//
// Teaching note: GPU async execution model — Recorded vs. Immediate
// =====================================================================
//
// OpenGL is IMMEDIATE-MODE: every glDraw* call is a self-contained command
// that the driver packs, validates, and submits to the GPU right then. State
// mutations (glBindBuffer, glUseProgram, glViewport) mutate a global context
// that the next draw picks up. There is no "begin/end recording" — the driver
// builds command buffers internally and flushes when it has to.
//
// Vulkan is RECORDED-MODE: you explicitly begin a command buffer, record a
// sequence of API calls, end the buffer, then submit it to a queue. The GPU
// executes it whenever the scheduler decides — potentially frames later.
// Benefits of recording:
//
//   1. MULTI-THREADED BUILDING: you can record command buffers on worker
//      threads in parallel. GL's global context is single-threaded by design.
//
//   2. REUSE: a recorded command buffer for static geometry (e.g., a skybox)
//      can be submitted repeatedly without re-recording. GL must re-issue
//      every call per frame.
//
//   3. DRIVER OVERHEAD REDUCTION: batching 1000 draws into one submit is
//      faster than 1000 individual glDraw calls because the driver validates
//      state once per submit instead of once per draw.
//
//   4. EXPLICIT SYNCHRONIZATION: Vulkan fences/semaphores tell you exactly
//      when a command buffer finishes — no glFinish() guesswork.
//
// N-Buffered design (the "N" in "N-buffered"):
//   A real renderer keeps N command buffers in a ring (N = swapchain image
//   count, typically 2-3). While the GPU is executing buffer[1], the CPU
//   records buffer[2] for the next frame. GL's double-buffering hides this
//   implicitly; Vulkan makes you manage it. This class holds ONE VkCommandBuffer
//   at a time; VKRhi will manage the ring and call beginRecording/endRecording
//   to swap which VkCommandBuffer is active.
//
// This class mirrors GLCommandBuffer (core/opengl/GLCommandBuffer.h):
// same ICommandBuffer interface, completely different GPU execution model.
// ===========================================================================

#include <vulkan/vulkan.h>

#include <map>

#include "ICommandBuffer.h"
#include "ClearValue.h"

// Forward declarations (same pattern as GLCommandBuffer)
class VKPipeline;
class VKFramebuffer;
class VKBuffer;
class VKShaderResourceBindings;

class VKCommandBuffer : public ICommandBuffer {
public:
    // ── Constructor / Destructor ─────────────────────────────────────────

    VKCommandBuffer();
    ~VKCommandBuffer() override;

    // ── Interface: ICommandBuffer ────────────────────────────────────────

    void beginPass(IFramebuffer* fb,
                   const ClearValue& colorClear,
                   const ClearValue& depthClear) override;
    void endPass() override;

    void setGraphicPipeline(IGraphicsPipeline* pso) override;
    void setViewport(int x, int y, int w, int h) override;
    void setScissor(int x, int y, int w, int h) override;
    void setShaderResources(IShaderResourceBindings* bindings) override;

    void setVertexInput(int bindingSlot, IBuffer* buffer, size_t offset) override;
    void setIndexBuffer(IBuffer* buffer, E_INDEX_FORMAT format) override;

    void draw(int vertexCount, int firstVertex = 0) override;
    void drawIndexed(int indexCount, int firstIndex = 0, int vertexOffset = 0) override;
    void drawInstanced(int vertexCount, int instanceCount,
                       int firstVertex = 0, int firstInstance = 0) override;

    void pushDebugGroup(const char* name) override;
    void popDebugGroup() override;

    // ── Backend internal (VKRhi calls these to manage the ring buffer) ──

    /// Called by VKRhi at the start of each frame. Stores the VkCommandBuffer
    /// handle from the command pool, swapchain framebuffer/render pass for
    /// beginPass(nullptr) rendering, and extent for default viewport.
    ///
    /// Teaching: VkCommandBuffer ownership lives in VkCommandPool (owned by
    /// VKRhi). This class receives a PRE-ALLOCATED handle — it does NOT call
    /// vkAllocateCommandBuffers or vkFreeCommandBuffers. This separation
    /// mirrors how VKBuffer receives pre-allocated memory: the allocator and
    /// the user are different concerns.
    void beginRecording(VkCommandBuffer cmd,
                        VkFramebuffer swapchainFb,
                        VkRenderPass swapchainRp,
                        VkExtent2D extent);

    /// Called by VKRhi at the end of recording (before vkQueueSubmit).
    /// Calls vkEndCommandBuffer on m_cmd, sets m_recording = false.
    ///
    /// Teaching: vkEndCommandBuffer finalizes the recording — no more Vulkan
    /// commands can be recorded into this buffer until it is reset. This is
    /// why multi-threaded recording requires separate command pools per thread.
    void endRecording();

private:
    // ── Helpers ──────────────────────────────────────────────────────────

    /// Reconstruct VkVertexInputBindingDescription + VkVertexInputAttributeDescription
    /// arrays from stored vertex bindings + current pipeline's vertex layout.
    /// Returns the VkBuffers and VkDeviceSize offsets arrays for vkCmdBindVertexBuffers.
    void buildVertexInputArrays(std::vector<VkBuffer>& buffers,
                                std::vector<VkDeviceSize>& offsets) const;

    /// Convert ClearValue to VkClearValue. Layout-compatible via memcpy
    /// (ClearValue.color[4] → VkClearValue.color, depth/stencil → VkClearValue.depthStencil).
    static VkClearValue toVkClearValue(const ClearValue& cv);

    // ── Member data ─────────────────────────────────────────────────────

    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    VKPipeline* m_currentPipeline = nullptr;
    VKFramebuffer* m_currentFramebuffer = nullptr;

    std::map<int, std::pair<VKBuffer*, size_t>> m_vertexBindings;
    VKBuffer* m_indexBuffer = nullptr;
    E_INDEX_FORMAT m_indexFormat = UINT16;

    bool m_recording = false;

    // ── Swapchain rendering (used when beginPass(nullptr)) ───────────────
    // Set by VKRhi in beginRecording(). These are the per-frame swapchain
    // framebuffer and render pass — every frame presents to a different
    // swapchain image, so these change per-frame.
    VkFramebuffer m_swapchainFb  = VK_NULL_HANDLE;
    VkRenderPass  m_swapchainRp  = VK_NULL_HANDLE;
    VkExtent2D    m_swapchainExtent = {800, 600};
};

#endif // __VKCOMMANDBUFFER_H__
