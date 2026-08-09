## 2026-08-06 — ClearValue moved to interface layer

- `struct ClearValue` extracted from `core/opengl/GLCommandBuffer.h` → new `core/rhi/ClearValue.h`.
  Interface leak fix: type was shared across backends via `ICommandBuffer::beginPass()` but defined in a GL-only header.
- GL members were `GLfloat`/`GLint`; new header uses plain `float`/`int` (same ABI since GLfloat=float, GLint=int). `main/rhi_verify.cpp` untouched — field names identical.
- `ICommandBuffer.h` keeps only the `struct ClearValue;` forward declaration — sufficient for `const ClearValue&` params, no include cycle risk.
- Include ordering in GLCommandBuffer.h: project headers after `<glad>`/`<map>`; `ClearValue.h` sits right after `ICommandBuffer.h`. Resolves via `core/rhi` in include dirs (INTERFACE lib `core_rhi`).
- Key teaching point documented in header: `(float[4], float, int)` is layout-compatible with the `VkClearValue` union (color → `VkClearColorValue`, depth/stencil → `VkClearDepthStencilValue`), so a Vulkan backend can memcpy/reinterpret without conversion. `active=false` → Vulkan loadOp=LOAD, GL skips glClearBuffer*.
- `nproc` doesn't exist on macOS (zsh) — build still parallelizes via cmake default; used `-j` alone instead.

## 2026-08-06 — CMake wiring for Vulkan RHI backend (shaderc + conditional rhi_verify)

- Added `find_package(shaderc QUIET)` with macOS fallback: `find_library(SHADERC_LIBRARY NAMES shaderc_combined)` → creates an `UNKNOWN IMPORTED` target `shaderc::shaderc_combined` with `INTERFACE_INCLUDE_DIRECTORIES` = `Vulkan_INCLUDE_DIRS`. Found at `/usr/local/lib/libshaderc_combined.a` (static archive from Vulkan SDK).
- `vulkan_rhi` now links `shaderc::shaderc_combined` (PRIVATE) alongside `Vulkan::Vulkan`.
- `rhi_verify` links conditionally: `vulkan_rhi + Vulkan::Vulkan + glfw` when `O5M_VULKAN`, else `opengl_rhi + glad + OpenGL::GL + glfw`. The `o5m` main executable stays GL-only.
- **CRITICAL ordering bug**: `add_subdirectory(core)` ran at line ~23, BEFORE `find_package(Vulkan)` (line ~37) and the shaderc block in the root CMakeLists. Imported targets (`Vulkan::Vulkan`, `shaderc::shaderc_combined`) are directory-scoped and only visible in subdirectories processed AFTER their creation — so `vulkan_rhi`'s `target_link_libraries` failed with "target was not found". Fix: moved `add_subdirectory(core)` to AFTER all dependency discovery (OpenGL/Vulkan/glfw3/shaderc). Teaching point: CMake imported targets are only visible to subdirectories added later.
- Verified: default GL build exit 0; `-DO5M_VULKAN=ON` configure shows "Vulkan SDK found" + "shaderc found (fallback)"; `vulkan_rhi` compiles and links as `libvulkan_rhi.dylib`.

## 2026-08-06 — VulkanDevice singleton (VkInstance → VkDevice → Swapchain bootstrap)

- Created `core/vulkan/VulkanDevice.h` and `core/vulkan/VulkanDevice.cpp` — the heart of the Vulkan RHI backend. Singleton that owns the full bootstrap chain: VkInstance → VkPhysicalDevice → VkDevice → VkQueue → VkSurfaceKHR → VkSwapchainKHR. All VK* resource classes will reference this for memory allocation, queue submission, and swapchain queries.

- **CMake additions**: vulkan_rhi now links `glfw` and `spdlog::spdlog` (both PRIVATE). Added `find_package(spdlog REQUIRED)` inside the O5M_VULKAN block in `core/CMakeLists.txt`. spdlog installed via `brew install spdlog` (1.17.0 at `/opt/homebrew/Cellar/spdlog/1.17.0`).

- **macOS portability**: Without `VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME` + `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` at instance creation, `vkEnumeratePhysicalDevices` returns 0 GPUs on macOS. Device-level `VK_KHR_portability_subset` must also be enabled. These are MoltenVK requirements — Vulkan-over-Metal needs explicit opt-in.

- **Validation layers**: `VK_LAYER_KHRONOS_validation` checked via `vkEnumerateInstanceLayerProperties`. On macOS with bare Vulkan SDK install (no Vulkan SDK Configurator), this layer may NOT be available → warn via spdlog and continue without blocking. Debug messenger (`VK_EXT_debug_utils`) only created when layers are enabled, with dynamic loading via `vkGetInstanceProcAddr` for `vkCreateDebugUtilsMessengerEXT` / `vkDestroyDebugUtilsMessengerEXT`.

- **Present mode**: `VK_PRESENT_MODE_MAILBOX_KHR` preferred (triple buffering, no tearing) with fallback to `VK_PRESENT_MODE_FIFO_KHR` (guaranteed vsync).

- **Swapchain format**: Prefer `VK_FORMAT_B8G8R8A8_SRGB` for gamma-correct rendering; fallback to `VK_FORMAT_B8G8R8A8_UNORM` if sRGB not advertised. Extent fixed at {800, 600} (matches rhi_verify window size, no resize per C2 lock).

- **VK_CHECK macro**: Local `assert`-based macro checking every `vkCreate*/vkAllocate*` return. Teaching point: unlike GL's polled `glGetError`, Vulkan errors are immediate returns — missing a check means a crash frames later.

- **Build verified**: `cmake --build build-vk --target vulkan_rhi` exits 0, clean (no warnings). `libvulkan_rhi.dylib` = 277KB with all VulkanDevice methods exported. GL build (`build-gl --target opengl_rhi`) still passes. LSP errors on VulkanDevice.* are false positives — root `compile_commands.json` from GL build lacks Vulkan include paths.

## 2026-08-06 — VKMemoryAllocator (one VkDeviceMemory per resource, teaching-minimal)

- Created `core/vulkan/VKMemoryAllocator.h/.cpp` — intentionally naive allocator: each resource gets its own independent vkAllocateMemory. No pooling/recycling/sub-allocation (C1 scope lock). Purpose: make fragmentation visible so VMA's sub-allocation strategy is appreciated later.
- **API design**: `init(VkDevice, VkPhysicalDevice)` takes RAW handles instead of depending on VulkanDevice.h — decouples the allocator from the singleton, keeps it testable. `VulkanDevice::findMemoryType()` was NOT reused here because the task required self-contained memory-type iteration; allocate() does its own `vkGetPhysicalDeviceMemoryProperties` walk (typeFilter bit AND preferredProps match).
- **Error handling**: `allocate()` returns a null `MemoryBlock{}` on `vkAllocateMemory` failure (OUT_OF_DEVICE/HOST_MEMORY) rather than asserting — graceful degradation. But a *missing memory-type match* asserts (coding bug: wrong preferredProps for usage, crash-loud convention).
- **free() safety**: resets block to `{}` after vkFreeMemory → double-free becomes a no-op. Assert-free, VK_NULL_HANDLE check.
- **alignUp()**: bit trick `(value + alignment - 1) & ~(alignment - 1)` — requires power-of-two alignment, which all Vulkan alignments (minUniformBufferOffsetAlignment) guarantee. Currently unused (offset always 0); will matter when sub-allocation arrives.
- **VMA rationale documented in header**: vkAllocateMemory is driver-expensive; one-per-resource fragments heaps; VMA packs sub-allocations into blocks.
- **Build verified**: `cmake --build build-vk --target vulkan_rhi` exits 0 — GLOB in core/CMakeLists.txt auto-picked up both files, no warnings. LSP `vulkan/vulkan.h not found` errors are still the known false positives from GL-build compile_commands.json.

## 2026-08-06 — VKShader (shaderc GLSL→SPIR-V, IShader impl)

- Created `core/vulkan/VKShader.h` and `core/vulkan/VKShader.cpp` — Vulkan IShader implementation. Key architectural differences from GLShader:
  - **compile()**: Uses shaderc C++ API (`shaderc/shaderc.hpp`, `shaderc::Compiler`, `shaderc::CompileOptions`) to transpile GLSL source → SPIR-V bytecode, then creates a `VkShaderModule`. GL compiles at draw-time per-driver; Vulkan pre-compiles to SPIR-V at engine init.
  - **link()**: No-op returning true. In Vulkan, "linking" happens at pipeline creation (`VkPipelineShaderStageCreateInfo` bundles `VkShaderModule`s). No separate link step exists.
  - **Reflection**: `uniformBlocks()` and `textureBindings()` return empty vectors (C10 scope: no SPIRV-Cross). Pipeline reflection deferred to later via SPIRV-Cross or `VK_KHR_shader_reflection`.
  - **COMPUTE shaders**: Rejected with `compileLog = "compute shaders not supported"` (C9).

- **GLSL #version auto-upgrade**: Before shaderc compilation, scans source for `#version XXX`. If `< 450`, replaces with `#version 450` using `std::regex`. Reason: Vulkan's GLSL→SPIR-V compiler (glslang) requires GLSL 450 semantics for Vulkan targets. rhi_verify.cpp uses `#version 410 core` — this auto-upgrade allows the same source to work for both GL and Vulkan backends without source modification.

- **shaderc C++ API**: Used `<shaderc/shaderc.hpp>` (C++ wrapper) over the C API (`<shaderc/shaderc.h>`). Both are part of `libshaderc_combined.a`. The C++ API is RAII-friendly: `shaderc::Compiler` auto-releases via destructor, no manual `shaderc_compiler_release` needed. Options: target_env=vulkan, env_version=1.2, optimization=performance.

- **Header guard**: `__VKSHADER_H__` (double-underscore prefix+suffix, matches GLShader convention: `__GLSHADER_H__`).

- **Build verified**: `cmake --build build-vk --target vulkan_rhi` exits 0, GL build (`build-gl --target opengl_rhi`) also 0. Files auto-discovered by `file(GLOB_RECURSE)` in `core/CMakeLists.txt`. LSP diagnostics clean on both new files.

## 2026-08-06 — VKBuffer (VkBuffer + VkDeviceMemory, IBuffer implementation)

- Completed `core/vulkan/VKBuffer.h/.cpp` — full IBuffer implementation. Constructor takes raw handles `(BufferDesc, VkDevice, VkPhysicalDevice, VkQueue, uint32_t queueFamilyIndex)` instead of the VulkanDevice singleton (same decoupling choice as VKMemoryAllocator). Task text RECOMMENDED `(desc, device, physicalDevice)` but the staging upload's vkQueueSubmit needs the queue + family index, so they're passed explicitly — VKRhi will source them from VulkanDevice::device()/queue()/queueFamilyIndex().
- **Usage bitmask teaching point**: GL's implicit bind targets vs Vulkan's immutable VkBufferUsageFlags bitmask. BufferDesc.usage is already a bitmask (1<<0..1<<3), so multiple roles (e.g. VERTEX+STORAGE) map cleanly to OR'd VK flags — something GL's single-target model can't express.
- **TRANSFER_DST_BIT gotcha (classic)**: every buffer gets `VK_BUFFER_USAGE_TRANSFER_DST_BIT` added, because DEVICE_LOCAL buffers are CPU-unwritable and all uploads go through vkCmdCopyBuffer — which validates the flag on the destination. Forgetting it = silent validation failure. Staging buffers get TRANSFER_SRC_BIT.
- **Memory hint mapping**: STATIC → DEVICE_LOCAL (VRAM, staging copy per upload); DYNAMIC/STREAM → HOST_VISIBLE | HOST_COHERENT. Persistent mapping: vkMapMemory once in create() with VK_WHOLE_SIZE, stored in m_mappedPtr — fulfills IBuffer.h's "Permentally mapping" TODO. HOST_COHERENT ⇒ no vkFlushMappedMemoryRanges, so upload() = plain memcpy and unmap() = no-op.
- **map() on DEVICE_LOCAL asserts** (crash-loud convention): Vulkan cannot map VRAM, unlike GL where glMapBuffer always works via driver copy. Teaching comment documents the difference.
- **submitImmediately()**: one-shot transfer path — transient command pool + primary command buffer, ONE_TIME_SUBMIT begin, vkCmdCopyBuffer (region srcOffset=0, dstOffset=offset), vkQueueSubmit, vkQueueWaitIdle, then vkDestroyCommandPool (frees its command buffers). Comment notes real engines reuse a persistent pool + per-frame cmd buffer. This deliberately does NOT use VKRhi's command buffer (not created yet, C-scope).
- Refactored buffer creation into file-local `createBufferWithMemory()` (anonymous namespace) shared by create() and the staging path — vkCreateBuffer → vkGetBufferMemoryRequirements → vkAllocateMemory → vkBindBufferMemory, with local `findMemoryType()` replicating VulkanDevice::findMemoryType() to stay singleton-free. VK_CHECK on every vkCreate*/vkAllocate*/vkMap*/vkQueue* call (spdlog + assert pattern copied from VulkanDevice.cpp).
- Build verified: `cmake --build build-vk --target vulkan_rhi` exits 0, no warnings (GLOB auto-picked both files). LSP `vulkan/vulkan.h not found` errors remain known false positives from GL-build compile_commands.json.

## 2026-08-06 — VKTexture (VkImage + VkDeviceMemory + VkImageView, ITexture implementation)

- Created `core/vulkan/VKTexture.h` and `core/vulkan/VKTexture.cpp` — Vulkan ITexture implementation. Mirrors GLTexture pattern: same ITexture interface, same TextureDesc config, different GPU API.

- **VkImage vs VkImageView vs VkSampler separation (key teaching point)**: In OpenGL, a "texture" is a monolithic GLuint handle bundling storage + filtering + mip chains. Vulkan splits these: VkImage = raw pixel storage (like glTexStorage2D), VkImageView = a view into which aspect/mip/array range to access (like GL's texture target + swizzle), VkSampler = filtering/wrapping/anisotropy. This separation mirrors real GPU hardware units.

- **Format mapping**: 7 formats mapped — R8_UNORM→VK_FORMAT_R8_UNORM, RGBA8_UNORM→R8G8B8A8_UNORM, RGBA16_SFLOAT→R16G16B16A16_SFLOAT, RGBA32_SFLOAT→R32G32B32A32_SFLOAT, D16_UNORM, D24_UNORM_S8_UINT, D32_SFLOAT. Unlike GL's three-enum split (internalFormat/format/type), Vulkan uses a single VkFormat that encodes all three.

- **Aspect mask mapping**: Depth-only formats (D16, D32) use VK_IMAGE_ASPECT_DEPTH_BIT; D24_UNORM_S8_UINT uses DEPTH|STENCIL; all others use COLOR. Teaching point: Vulkan's aspect masks allow independently accessing depth and stencil planes — something GL hides behind implicit attachment bindings.

- **Usage flags**: RENDERTARGET → COLOR_ATTACHMENT_BIT|SAMPLED_BIT|TRANSFER_DST_BIT; depth formats → DEPTH_STENCIL_ATTACHMENT_BIT; default → SAMPLED_BIT|TRANSFER_DST_BIT. TRANSFER_DST_BIT always included because upload() uses vkCmdCopyBufferToImage which requires it on the destination.

- **Memory**: DEVICE_LOCAL (VRAM, fast GPU access, not CPU-mappable). Inline memory type selection via vkGetPhysicalDeviceMemoryProperties walk — independent of VKMemoryAllocator (choice: self-contained texture, knowledge duplication acceptable for teaching).

- **upload() — staging buffer flow**: Host-visible staging buffer (TRANSFER_SRC_BIT) → memcpy pixel data → vkCmdCopyBufferToImage → layout transitions (UNDEFINED→TRANSFER_DST_OPTIMAL→SHADER_READ_ONLY_OPTIMAL) → vkQueueSubmit + vkQueueWaitIdle (synchronous, init-time operation). Temp command pool/command buffer created and destroyed per upload.

- **Layout transitions (teaching point)**: Vulkan images have explicit memory layouts that tell the GPU how pixels are organized. Pipeline barriers (vkCmdPipelineBarrier) transition between layouts — they flush caches and reorder memory, not move pixels. OpenGL hides all barriers; Vulkan exposes them because the driver has to guess less about intent.

- **Scope locks**: C3 (samples>1 asserts — no MSAA), C4 (miplevels always 1 — no mip chains), C5 (layers always 1 — no arrays). No cubemap support (CUBEMAP flag ignored, viewType always VK_IMAGE_VIEW_TYPE_2D).

- **Constructor takes raw handles** (VkDevice, VkPhysicalDevice) not VulkanDevice singleton — same decoupling pattern as VKBuffer and VKMemoryAllocator. upload() gets queue via vkGetDeviceQueue(m_device, 0, 0) assuming family 0 = graphics (matches VulkanDevice setup).

- **Build verified**: `cmake --build build-vk --target vulkan_rhi` exits 0, clean (no warnings). Files auto-discovered by `file(GLOB_RECURSE)`. LSP diagnostics clean on both new files (VKTexture.h, VKTexture.cpp).

## 2026-08-06 — VKShaderResourceBindings (VkDescriptorSetLayout + Pool + Set + PipelineLayout)

- Created `core/vulkan/VKShaderResourceBindings.h` and `core/vulkan/VKShaderResourceBindings.cpp` — Vulkan descriptor set implementation mirroring `GLShaderResourceBindings`. Key architectural divergence: GL's `create()` is a no-op (bindings resolved at draw-time via `glBindBufferRange`/`glBindTextureUnit`); Vulkan's `create()` builds a full 6-step GPU object chain.

- **Constructor**: `(VkDevice, VkSampler)` — 2 params sourced from `VulkanDevice::device()` and `VulkanDevice::defaultSampler()`. The sampler is required because Vulkan separates image (VkImageView) from sampling (VkSampler) — unlike GL where filter state is part of the texture object. Single default sampler for all texture bindings (per-texture sampler configurability deferred).

- **create() pipeline** (6 steps):
  1. Build `VkDescriptorSetLayoutBinding` array from accumulated `m_UBOs`/`m_textures` maps — `UNIFORM_BUFFER` for UBOs, `COMBINED_IMAGE_SAMPLER` for textures, `VK_SHADER_STAGE_ALL` (no SPIRV-Cross reflection yet)
  2. `vkCreateDescriptorSetLayout` — declares the shader→resource contract
  3. `vkCreatePipelineLayout` — bundles descriptor set layouts + push constant ranges; `setLayoutCount=0` when no bindings (valid for shaders using only in/out varyings)
  4. `vkCreateDescriptorPool` — pool sizes sized per descriptor type count, `maxSets=1`, no `FREE_DESCRIPTOR_SET_BIT` (whole-pool destruction only, C1: no recycling)
  5. `vkAllocateDescriptorSets` — single set from pool
  6. `vkUpdateDescriptorSets` — writes `VkDescriptorBufferInfo` (buffer handle + offset + range) and `VkDescriptorImageInfo` (imageView + sampler + `SHADER_READ_ONLY_OPTIMAL` layout) into the set. UBO `size=0` → `VK_WHOLE_SIZE` (mirrors `glBindBufferBase` semantics)

- **Empty bindings case**: When both maps are empty, `create()` skips pool+set allocation but still creates the pipeline layout — valid for shaders with no resource bindings.

- **Destructor**: Reverse-creation teardown: `vkDestroyPipelineLayout` → `vkDestroyDescriptorPool` (implicitly frees descriptor sets) → `vkDestroyDescriptorSetLayout`.

- **Teaching comments cover**: GL's dynamic binding vs Vulkan's pre-baked descriptors, sampler/image separation, pipeline layout compatibility (all pipelines using this bindings object must be created with the same layout), combined image sampler matching GL's `sampler2D` mental model, pool as slab allocator, and the GL→Vulkan analogy: `vkUpdateDescriptorSets` ≈ batch `glBindBufferRange` + `glBindTextureUnit` at init time.

- **VK_CHECK macro**: File-local (same pattern as VKBuffer.cpp) — `spdlog::error` + `assert(false)` on every `vkCreate*`/`vkAllocate*` call.

- **Build verified**: `cmake --build build-vk --target vulkan_rhi` exits 0, no warnings. Files auto-discovered by `file(GLOB_RECURSE)` in `core/CMakeLists.txt`.

## 2026-08-06 — VKPipeline (VkGraphicsPipelineCreateInfo builder, IGraphicsPipeline impl)

- Created `core/vulkan/VKPipeline.h` and `core/vulkan/VKPipeline.cpp` — the most mapping-heavy class. Full IGraphicsPipeline implementation that stores state copies and builds a VkGraphicsPipelineCreateInfo from them.

- **Constructor pattern**: Takes raw `VkDevice` handle (same decoupling choice as VKBuffer, VKTexture, VKMemoryAllocator — no VulkanDevice* dependency). Destructor destroys VkPipeline then VkPipelineLayout (reverse-creation order).

- **create(VkRenderPass, VkExtent2D) signature (key design decision)**: Unlike GL's `create()` (no params — GL has an implicit framebuffer), Vulkan pipelines are render-pass-aware. The render pass and extent are provided by VKCommandBuffer or VKRhi at creation time — explicit, decoupled, follows the task spec's "Final decision". No `VulkanDevice::defaultRenderPass()` method needed; render pass responsibility stays with the caller.

- **20 vertex format mappings** (task said 19 but enum has 20): FLOAT32→R32_SFLOAT, FLOAT32X2→R32G32_SFLOAT, FLOAT32X3→R32G32B32_SFLOAT, FLOAT32X4→R32G32B32A32_SFLOAT, 3× UINT8_UNORM (R8/R8G8/R8G8B8A8), 3× UINT16_UNORM (R16/R16G16/R16G16B16A16), 3× INT8_SNORM (R8/R8G8/R8G8B8A8), 3× UINT8 (R8/R8G8/R8G8B8A8_UINT), 4× INT32 (R32/R32G32/R32G32B32/R32G32B32A32_SINT). Teaching point: Vulkan uses a single VkFormat for vertex attributes AND textures — unlike GL which uses separate glVertexAttribPointer(type, size, normalized) params.

- **perInstance → VK_VERTEX_INPUT_RATE_INSTANCE mapping**: Stored in VkVertexInputBindingDescription::inputRate. GL ignores per-instance rate by default (divisor=0 ⇒ per-vertex); Vulkan honors it explicitly. Teaching comment documents the GL-Vulkan divergence.

- **RasterizerState mapping**: polygonMode FILL→VK_POLYGON_MODE_FILL, LINE→VK_POLYGON_MODE_LINE. cullMode NONE/FRONT/BACK → VK_CULL_MODE_NONE/VK_CULL_MODE_FRONT_BIT/VK_CULL_MODE_BACK_BIT. depthClamp→depthClampEnable.

- **Front-face compensation**: `VK_FRONT_FACE_CLOCKWISE`. Vulkan's clip space has Y-down (upper-left origin), GL has Y-up. To preserve the same winding order for models authored in GL convention, the front face must be inverted. Without this, back-face culling selects the wrong faces.

- **DepthStencilState**: 6 CompareOp values map 1:1 (VK_COMPARE_OP_NEVER/LESS/EQUAL/LESS_OR_EQUAL/GREATER/ALWAYS). Stencil disabled (zeroed front/back: KEEP+NEVER+0 masks — C-scope). depthBoundsTestEnable=false.

- **BlendState**: 6 Factor enum values map 1:1 to VkBlendFactor (ZERO/ONE/SRC_ALPHA/DST_ALPHA/ONE_MINUS_SRC_ALPHA/ONE_MINUS_DST_ALPHA). Alpha factors hardcoded to ONE/ZERO (matching GL impl). Per-attachment state in a vector; empty blend vector → single disabled attachment (passthrough). colorBlendOp=ADD, colorWriteMask=RGBA.

- **Dynamic state**: `VK_DYNAMIC_STATE_VIEWPORT + VK_DYNAMIC_STATE_SCISSOR` declared as dynamic — the command buffer sets actual values at beginPass time via vkCmdSetViewport/VkCmdSetScissor. Teaching: unlike GL where ALL state is implicitly dynamic, Vulkan requires explicit opt-in.

- **Pipeline layout**: Empty — zero descriptor set layouts, zero push constant ranges (C10: no SPIRV-Cross reflection, no VKShaderResourceBindings integration yet). Later: when bindings system wires up, pipeline layout will come from VKShaderResourceBindings.

- **Geometry shader optional**: Only pushed into shaderStages array if non-null. Teaching comment: GL impl crashes on null gs pointer; Vulkan handles it explicitly by skipping the stage.

- **link() equivalence documented**: VKShader::link() is a no-op. VKPipeline::create() IS Vulkan's "link" equivalent — VkShaderModules are bundled into VkPipelineShaderStageCreateInfo at pipeline creation time. Commented in header and create().

- **No pipeline cache**: `basePipelineHandle=VK_NULL_HANDLE`, `basePipelineIndex=-1`, `vkCreateGraphicsPipelines(..., VK_NULL_HANDLE, ...)` — C6 scope lock. Pipeline caches serialize driver compilation to disk; skipped for teaching clarity.

- **Input assembly**: Fixed `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`, `primitiveRestartEnable=false`. No glBegin(GL_TRIANGLES) equivalent — Vulkan requires topology up front because GPUs optimize for it.

- **VK_CHECK macro**: File-local (same pattern as VKBuffer.cpp / VulkanDevice.cpp). Applied to the single vkCreateGraphicsPipelines call. vkCreatePipelineLayout has its own result check (non-fatal — just returns false on failure so the caller can handle).

- **Build verified**: `cmake --build build-vk --target vulkan_rhi` exits 0, no warnings. GL build (`build-gl --target opengl_rhi`) also 0. Files auto-discovered by `file(GLOB_RECURSE)`. LSP diagnostics clean on both new files.

## 2026-08-06 — VKFramebuffer (VkRenderPass + VkFramebuffer, IFramebuffer impl)

- Created `core/vulkan/VKFramebuffer.h` and `core/vulkan/VKFramebuffer.cpp` — Vulkan IFramebuffer implementation. The key architectural divergence from GLFramebuffer: Vulkan splits the framebuffer concept into TWO objects — VkRenderPass (metadata: attachment formats, load/store ops, layout transitions) and VkFramebuffer (concrete VkImageViews bound to render pass slots). GL does both in a single `glCreateFramebuffers` + `glNamedFramebufferTexture` call.

- **Why the split?** Tile-based GPU optimization. GPUs like Apple Silicon, ARM Mali, and Qualcomm Adreno divide the framebuffer into tiles that fit in on-chip memory. The render pass declares which attachments stay on-chip (LOAD_OP_CLEAR → no VRAM read) vs. need write-back (STORE_OP_STORE). Vulkan makes this explicit so the driver doesn't have to reverse-engineer intent from a sequence of glDraw calls.

- **create() — 6-step synthesis**:
  1. Collect `VkAttachmentDescription` from color + depth textures (format→vkFormat(), loadOp=CLEAR, storeOp=STORE, initialLayout=UNDEFINED, finalLayout=SHADER_READ_ONLY_OPTIMAL for color / DEPTH_STENCIL_ATTACHMENT_OPTIMAL for depth). Fixed CLEAR per scope (teaching comment: loadOp=LOAD for active=false).
  2. Build `VkAttachmentReference` array — color refs point to color descriptions (layout=COLOR_ATTACHMENT_OPTIMAL), depth ref points to depth description (layout=DEPTH_STENCIL_ATTACHMENT_OPTIMAL).
  3. `VkSubpassDescription`: 1 subpass (C8 scope), `pipelineBindPoint=GRAPHICS`, color + depth refs. No resolve/preserve attachments (no MSAA, no transient pass intermediates).
  4. `VkSubpassDependency`: EXTERNAL→0, srcStageMask=TOP_OF_PIPE, dstStageMask=COLOR_ATTACHMENT_OUTPUT (+ EARLY_FRAGMENT_TESTS if depth), srcAccessMask=0, dstAccessMask=COLOR_ATTACHMENT_WRITE (+ DEPTH_STENCIL_ATTACHMENT_WRITE if depth). Teaching point: this is Vulkan's equivalent of GL's implicit per-draw barriers — baked into the render pass so the driver can optimize around it.
  5. `vkCreateRenderPass` — immutable after creation (changing attachments requires a new render pass).
  6. `vkCreateFramebuffer` — binds concrete VkImageViews to render pass slots. Extent from first color attachment; assert all attachments have same dimensions (Vulkan spec requirement, documented as teaching point: framebuffer compatibility = dimensions + formats).

- **Dimension consistency assert**: All attachments must have identical dimensions — enforced with assert in debug builds. GL has the same rule ("framebuffer completeness") but validates it with glCheckFramebufferStatus; Vulkan makes you assert it yourself at creation time.

- **mipLevel/layer limitation documented**: In this phase both are always 0 (C4/C5 scope). Teaching comment explains that in future phases with mipmaps/texture arrays, each attachment would need a separate VkImageView created with correct `subresourceRange` (baseMipLevel, levelCount, baseArrayLayer, layerCount). The existing VKTexture::imageView() always uses baseMipLevel=0, levelCount=1, baseArrayLayer=0, layerCount=1.

- **VK_CHECK macro**: File-local (same pattern as VKTexture/VKBuffer/VKShader/VKShaderResourceBindings) — `spdlog::error` + `assert(false)` on vkCreateRenderPass and vkCreateFramebuffer.

- **Destructor**: Reverse-creation teardown: vkDestroyFramebuffer → vkDestroyRenderPass. Both guarded by `!= VK_NULL_HANDLE`.

- **Constructor**: Takes raw `VkDevice` handle only (simpler than VKTexture/VKBuffer which also need VkPhysicalDevice). Render pass and framebuffer creation don't query physical device properties.

- **Build verified**: `cmake --build build-vk --target vulkan_rhi` exits 0, no warnings. Files auto-discovered by `file(GLOB_RECURSE)`. LSP diagnostics clean on both new files.

## 2026-08-06 — VKCommandBuffer (recorded N-buffered command buffer)

- Created `core/vulkan/VKCommandBuffer.h` and `core/vulkan/VKCommandBuffer.cpp` — the most semantically different class from GL. While GLCommandBuffer wraps immediate-mode GL calls, VKCommandBuffer records into a pre-allocated VkCommandBuffer for later submission.

- **GPU async execution model (core teaching)**:
  - GL = IMMEDIATE-MODE: each glDraw* is self-contained; the driver submits internally. No "begin/end recording".
  - Vulkan = RECORDED-MODE: explicitly begin recording → record commands → end recording → submit to queue. The GPU executes whenever the scheduler decides.
  - Benefits: multi-threaded recording (separate pools per thread), reuse of static command buffers, reduced driver overhead (batch validation), explicit sync via fences/semaphores.
  - N-Buffered design: VKRhi manages a ring of N command buffers (N = swapchain image count). While GPU executes buffer[1], CPU records buffer[2]. This class holds ONE buffer at a time; VKRhi calls beginRecording/endRecording per frame.

- **Viewport Y-flip (negative height trick)**:
  - GL viewport origin: BOTTOM-LEFT. Vulkan viewport origin: TOP-LEFT.
  - `VkViewport{x, float(y+h), w, float(-h), 0.0f, 1.0f}` — setting viewport.y = y+h (GL "top") and height = -h inverts Y in the viewport transform, so GL-authored shaders (Y-up NDC convention) render correctly without matrix changes.
  - Alternative: flip Y in projection matrix — but that changes winding order, culling, and all downstream math. The viewport-only flip is cleaner.
  - Scissor uses absolute framebuffer coords: NO flip needed. VkRect2D{x, y, w, h} means the same pixels in both APIs.

- **beginPass(IFramebuffer*)**:
  - fb != nullptr: uses VKFramebuffer's renderPass()/framebuffer()/extent() for off-screen rendering (shadow maps, GBuffer, post-process).
  - fb == nullptr: uses swapchain m_swapchainFb/m_swapchainRp set by VKRhi in beginRecording(). This is the "draw to screen" convention.
  - Builds VkRenderPassBeginInfo with ClearValue→VkClearValue conversion (memcpy color[4], explicit depth/stencil).
  - VK_SUBPASS_CONTENTS_INLINE: direct recording (no secondary command buffers) — equivalent to GL's implicit mode.

- **Lazy pipeline creation**: In setGraphicPipeline(), if VKPipeline::isValid() is false, call create() with the CURRENT render pass + extent. This matches GL's "set state just before draw" pattern but is suboptimal; real engines pre-bake pipelines at asset load. Teaching comment documents this.

- **Defensive guards**: draw()/drawIndexed()/drawInstanced() check for nullptr pipeline and nullptr framebuffer — same pattern as GLCommandBuffer. Silently skip the draw with spdlog::warn rather than crash.

- **Vertex input reconstruction**: buildVertexInputArrays() iterates m_vertexBindings map → builds VkBuffer[] + VkDeviceSize[] arrays. No VAO indirection — Vulkan's vkCmdBindVertexBuffers takes arrays directly. Actually simpler than GL per-draw VAO creation/destruction.

- **Shader resources**: vkCmdBindDescriptorSets binds the ENTIRE descriptor set (all UBOs + textures) at once. GL scatters glBindBufferRange/glBindTextureUnit per slot. Vulkan's approach matches GPU hardware: descriptors live in a contiguous table.

- **Debug groups**: Stubbed out — VK_EXT_debug_utils extension not wired (requires dynamic function pointer loading via vkGetInstanceProcAddr, decoupled from this class).

- **Destructor**: Nothing to destroy — VkCommandBuffer ownership lives in VkCommandPool (owned by VKRhi). vkDestroyCommandPool frees all its buffers automatically.

- **Build verified**: `cmake --build build-vk --target vulkan_rhi` exits 0, no warnings. GL build (`build-gl --target opengl_rhi`) also 0. LSP diagnostics clean on both new files. Files auto-discovered by `file(GLOB_RECURSE)`.

## 2026-08-06 — VKRhi (Vulkan RHI backend entry point, IRhi implementation)

- Created `core/vulkan/VKRhi.h` and `core/vulkan/VKRhi.cpp` — the Vulkan backend entry point. Mirrors GLRhi's `init/factory/beginFrame/endFrame` pattern. Owns VulkanDevice, manages N-buffered command buffers, and orchestrates the per-frame acquire→record→submit→present loop.

- **VulkanDevice extensions needed**: Added `physicalDevice()` getter (VKBuffer/VKTexture constructors need VkPhysicalDevice for memory type queries), plus swapchain render pass + per-image framebuffers (`createSwapchainRenderPass()`, `m_swapchainRenderPass`, `m_swapchainFramebuffers`, getters). VKRhi's `init()` calls `createSwapchainRenderPass()` between `createSwapchain()` and `createDefaultSampler()`.

- **N-buffered frame loop (core teaching)**:
  - N = `VulkanDevice::imageCount()` (typically 2-3). Ring buffer: `m_currentFrame = (m_currentFrame + 1) % N`.
  - `beginFrame()`: vkWaitForFences(currentFrame) → vkResetFences → vkAcquireNextImageKHR → vkResetCommandBuffer → vkBeginCommandBuffer → beginRecording (wires swapchain framebuffer/render pass).
  - `endFrame()`: endRecording → vkQueueSubmit (wait imageAvailable, signal renderFinished) → vkQueuePresentKHR (wait renderFinished).
  - **Fence vs Semaphore**: fence = GPU→CPU (CPU waits for GPU completion). Semaphore = GPU→GPU (pipeline stage synchronization). `VK_FENCE_CREATE_SIGNALED_BIT` for initial state — first vkWaitForFences passes immediately.
  - **Swapchain out-of-date**: VK_ERROR_OUT_OF_DATE_KHR handled with log only (C2: no resize).

- **Factory methods**: Each returns a `unique_ptr<VK*>` passing the necessary VulkanDevice handles. VKBuffer gets device+physicalDevice+queue+queueFamilyIndex (needs queue for staging upload's vkQueueSubmit). VKTexture gets device+physicalDevice. VKShader gets device. VKShaderResourceBindings gets device+defaultSampler. VKFramebuffer/VKPipeline get device only.

- **VKPipeline fix**: VKPipeline's `create(VkRenderPass, VkExtent2D)` overload hid the base class `create()` pure virtual — making VKPipeline abstract. Previously unnoticed because no code ever instantiated VKPipeline via `make_unique`. Added `bool create() override { return false; }` (no-op stub — Vulkan pipelines MUST have render pass info).

- **GLFW_CLIENT_API hint**: Must call `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)` before `VulkanDevice::init()` which calls `glfwCreateWindowSurface`. Without it, GLFW may create an OpenGL context that conflicts with Vulkan surface creation.

- **Destructor**: vkDeviceWaitIdle → destroy VKCommandBuffer wrapper → destroy semaphores/fences → destroy command pool → VulkanDevice::shutdown() (handles instance/device cleanup). Pool destruction implicitly frees all allocated command buffers.

- **Build verified**: `cmake --build build-vk --target vulkan_rhi` exits 0, clean (4 files recompiled: VKCommandBuffer, VKPipeline, VKRhi, VulkanDevice). LSP diagnostics clean on all 5 touched files (VKRhi.h, VKRhi.cpp, VulkanDevice.h, VulkanDevice.cpp, VKPipeline.h).

## 2026-08-06 — rhi_verify.cpp dual-backend (#ifdef O5M_HAS_VULKAN) with swapchain readback

- Rewrote `main/rhi_verify.cpp` to support both GLRhi and VKRhi via `#ifdef O5M_HAS_VULKAN`. GL path is ZERO changes — all existing GL code wrapped in `#ifndef`/`#else`, preserved byte-for-byte. VK path renders twice per frame: off-screen FBO (validates VKFramebuffer synthesis) + swapchain via `beginPass(nullptr)`.

- **Added VulkanDevice::swapchainImage()** getter returning raw `VkImage` (needed for `vkCmdCopyImageToBuffer` during readback — `swapchainImageView()` returns `VkImageView`, not `VkImage`). Also added `VKRhi::device()` and `VKRhi::currentImageIndex()` accessors.

- **ClearValue include fix**: Previously `ClearValue` was transitively included via `GLCommandBuffer.h` → `ClearValue.h`. With `#ifdef` splitting, the VK path lost this transitive include. Added explicit `#include "ClearValue.h"` in the common header section.

- **unique_ptr incomplete type trap**: `VKRhi.h` forward-declares `VulkanDevice` and `VKCommandBuffer` but uses `std::unique_ptr<VulkanDevice>` / `std::unique_ptr<VKCommandBuffer>` as members. The `unique_ptr` destructor is inline and calls `delete`, which requires the complete type. When `rhi_verify.cpp` instantiates `VKRhi rhi;`, the compiler generates destruction code for the members after `~VKRhi()` body — needs complete types even though `~VKRhi()` is defined out-of-line in VKRhi.cpp. Fix: `#include "VulkanDevice.h"` and `#include "VKCommandBuffer.h"` in rhi_verify.cpp's VK `#ifdef` block. Teaching point: out-of-line destructor declaration doesn't prevent the compiler from needing complete types for inline member destructors.

- **VK readback design**: After endFrame() on frame 2, `vkDeviceWaitIdle` → create staging buffer (HOST_VISIBLE | HOST_COHERENT, TRANSFER_DST) → temp command buffer (TRANSIENT pool) → layout transition `PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL` → `vkCmdCopyImageToBuffer` → transition back → `vkQueueSubmit` + `vkQueueWaitIdle` → `vkMapMemory` → read center pixel → assert blue-ish color. Swapchain format detection: B8G8R8A8 vs RGBA8 — check `pixel[0]` for BGRA, `pixel[2]` for RGBA.

- **Both builds verified**: `cmake --build build-gl --target rhi_verify` exits 0 (100KB binary), `cmake --build build-vk --target rhi_verify` exits 0 (101KB binary). LSP errors on VK headers are known false positives (GL compile_commands.json lacks Vulkan include paths). Files changed: `core/vulkan/VulkanDevice.h` (+1 getter), `core/vulkan/VKRhi.h` (+2 accessors), `main/rhi_verify.cpp` (major rewrite).


## 2026-08-06 — PHASE-VULKAN.md teaching document (T14)

- Created `/PHASE-VULKAN.md` (384 lines), the educational capstone following AGENTS.md's 6-section output format.
- Sources of truth used: plan decisions (rhi-vulkan-backend.md), real implementation notes (learnings.md), and actual source headers/comments. No fictional content.
- Section 4 (逐文件实现) intentionally does NOT reproduce code — 21 implementation files exist. Instead: one entry per file with the key teaching points + reading guidance (which function/comment to read). Task explicitly waived full code.
- 10 key questions written (task required 7+): instance/device explicitness, queue family, swapchain vs default FBO, why VkRenderPass, hand-written allocator (device-local vs host-visible), staging+fence readback chain, GL 4.1/4.6 DSA absence in Vulkan, VkImage/View/Sampler split, recorded vs immediate mode, immutable PSO.
- 6 real pitfalls (task required 2+): MoltenVK portability enumeration (0 GPUs), shaderc #version 410→450 regex upgrade, viewport negative-height Y-flip + CLOCKWISE front-face, fence must start SIGNALED, VkPipeline needs VkRenderPass (lazy create in setGraphicPipeline), validation layers may be absent on macOS.
- Style constraint enforced: zero em/en dashes (grep verified 0 occurrences). Used full-width colon  ： as list separator in the file-list code block instead. Chinese headers + mixed Chinese/English technical prose per AGENTS.md.
- Note for future docs: the anti-dash writing rule also applies to Chinese prose; a quick `grep -n '—\|–'` post-write check catches violations.

## F1 Audit (2026-08-06) — findings from compliance audit
- VKShader uses shaderc **C++ API** (`shaderc::Compiler`, `CompileGlslToSpv`, `GetCompilationStatus`) — the plan's QA grep pattern (`shaderc_compile_into_spv` etc., C API names) matches 0, but real shaderc compilation is present and linked via `shaderc::shaderc_combined`.
- 2 `assert(false)` exist in VulkanDevice.cpp (L201 pickPhysicalDevice "no GPU", L615 findMemoryType "no suitable memory type") — fatal-error paths after spdlog::error, NOT interface stubs. Intent of G3 satisfied; literal "0" grep not.
- All 69 `gl[A-Z]` matches in core/vulkan are teaching comments comparing GL vs Vulkan (0 non-comment matches). grep -v comment-strip confirmed.
- `VK_ERROR_OUT_OF_DATE_KHR` handled as log + skip (acquire: return; present: log) per C2 — no swapchain recreation (single vkCreateSwapchainKHR at init).
- pushConstantRangeCount=0, subpassCount=1 in both sites — guardrail compliant.
- rhi_verify.cpp vk calls (L349-525) all inside `#ifdef O5M_HAS_VULKAN` (L332-528); main.cpp has zero Vk references.
