# AGENTS.md — -o-5M

## Project Identity

A **learning-oriented real-time rendering engine**. Every line of code exists to teach GPU internals and modern graphics API design. The README ("Don't ask, just render") reflects the spirit — the code should speak for itself.

- **Author**: MaYu (MIT License)
- **Goal**: Build a rendering engine from scratch, one concept per phase, always compilable.

---

## Architecture — Non-Negotiable Rules

### RHI Layer (Rendering Hardware Interface)

**All OpenGL calls live exclusively behind the RHI abstraction.** Upper layers (game logic, scene graph, render passes) never include `gl.h` or call `gl*` directly. This is not optional — it's the core architectural constraint.

The RHI layer exists for two reasons:
1. **Teaching**: Forces you to think about what the GPU is actually doing, not what OpenGL's API looks like.
2. **Vulkan readiness**: The RHI interface is designed so a future Vulkan backend can drop in without touching upper layers.

### RHI Interface Design Constraints

The RHI is not just a thin `gl*` wrapper. It must expose concepts that map naturally to both OpenGL and Vulkan:

- **Descriptors** — Resource binding model (textures, uniform buffers, samplers). Even though OpenGL doesn't use descriptor sets natively, the RHI should model them. Use `glBindTextureUnit()` + DSA to approximate.
- **Pipeline State Objects (PSO)** — Encapsulate all render state (blend, depth, rasterizer, shader stages). Avoid scattered `glEnable`/`glDisable` calls. Bundle state into a single immutable-ish object.
- **Command Buffer / Render Pass** — Frame work is structured as a sequence of passes. No ad-hoc draw calls floating in the main loop.

### Modern OpenGL Only

- **OpenGL 4.6 Core Profile** — no compatibility profile, no deprecated features.
- **DSA (Direct State Access) everywhere** — `glCreateTextures()`, `glTextureSubImage2D()`, `glNamedBufferData()`. Never `glBindTexture()` + `glTexImage2D()`.
- **No fixed-function pipeline** — no `glBegin`/`glEnd`, no matrix stack (`glMatrixMode`, `glLoadIdentity`), no built-in lighting.
- **All rendering through shaders** — a shader program must be bound for any draw call.

---

## 分阶段计划

### Phase 0: 窗口骨架
画一个清屏的彩色窗口(ESC退出)。回答：Context是什么？双缓冲原理？glClear必要性？

### Phase 1: RHI层+三角形
定义IBuffer/IShader/IVertexArray及GL实现。画三角形。回答：VAO存了什么？stride/offset如何解析顶点？和VkPipelineVertexInputStateCreateInfo的对应？

### Phase 2: Handle系统+资源管理
实现Handle<T>(index+generation)、ResourceManager(单例)、ResourceFactory。画索引矩形。回答：generation解决什么？Handle vs shared_ptr？IBO解决了什么？

### Phase 3: UBO+Camera+MVP矩阵
UBO支持、Camera(透视+LookAt)、旋转立方体。回答：六次空间变换？Z-fighting成因？UBO vs glUniform优势？

### Phase 4: 纹理+材质+Assimp
ITexture接口、stb_image加载、Material类、Assimp导入模型。回答：Mipmap原理？internalFormat vs format区别？纹理颠倒问题？

### Phase 5: 场景图+Phong光照
SceneNode(Composite)、LightNode、多光源。回答：Phong vs Blinn-Phong？法线变换为何用inverse(transpose)？Forward多光源瓶颈？

### Phase 6: RenderPass+Framebuffer
IRenderPass(Strategy)、Framebuffer封装、简化RenderGraph、后处理(灰度)。回答：离屏渲染后为何能采样该纹理？Bloom需要哪些Pass？

### Phase 7: 延迟渲染
GBuffer(MRT：Position/Normal/Albedo)、DeferredLightingPass。回答：各通道格式选择？透明物体为何不适配延迟？带宽开销估算？

### Phase 8: Shadow Mapping
ShadowMapPass、深度比较、Shadow Acne(bias)、PCF软阴影。回答：走样根源？glPolygonOffset作用？正交vs透视投影适用场景？

### Phase 9: PBR
Cook-Torrance(GGX+Smith+Fresnel)、IBL(辐照度+预过滤+LUT)、HDR天空盒、ToneMapping+Gamma。回答：Split-Sum Approximation？sRGB vs Linear？D/G/F各自保证什么约束？

### Phase 10+: 选做
SSAO · Bloom · 骨骼动画 · Instanced Rendering · Frustum Culling · GPU粒子 · Vulkan后端

## 输出格式（每Phase）
1. **前置知识**（图形学概念+GPU内部）
2. **架构讨论**（为什么这样设计？替代方案？）
3. **文件列表**（按依赖顺序）
4. **逐文件实现**（代码+注释）
5. **关键问题回答**
6. **踩坑预警**（2-3个常见错误及排查）

## 使用
- 「开始 Phase N」→ 按格式实现

---

## Build System

### Setup

```bash
# Prerequisites: CMake 3.20+, C++17 compiler, vcpkg (recommended)
# Dependencies (vcpkg):
#   glfw3, glad[gl-api-4.6], glm, stb, assimp, spdlog

mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg.cmake>
make -j$(nproc)
```

### Key CMake Conventions

- C++17 standard enforced at the project level (`CMAKE_CXX_STANDARD 17`).
- Target-based approach: each library/executable is a CMake target with `target_*` properties.
- Dependencies resolved via `find_package` (backed by vcpkg).

---

## Style & Conventions

### Naming
- **Directories**: lowercase with hyphens (`rhi-layer/`, `scene-graph/`)
- **Files**: PascalCase for classes (`Texture.h`, `RenderPass.h`), camelCase or snake_case otherwise
- **Classes/Structs**: PascalCase (`class RenderDevice`)
- **Functions/Methods**: camelCase (`createTexture()`, `submitCommandBuffer()`)
- **Variables**: camelCase (`vertexBuffer`, `pipelineState`)
- **Constants/Enums**: PascalCase (`enum class TextureFormat`)
- **RHI public API types**: Prefixed to avoid collision (`RhiTexture`, `RhiBuffer`, `RhiPipeline`)

### Headers
- `#pragma once` (not include guards)
- Include order: project headers → third-party → standard library

### Error Handling
- spdlog for logging (not `std::cout`/`printf`)
- Assert aggressively (`assert`/`static_assert`) during development — crash early, crash loud
- RHI methods check for GL errors in debug builds (`glGetError()` after each significant call)

---

## Workflow for AI Agents

> **🚨 最高优先级规则 —— 除非用户明确要求，否则永远不要擅自改动代码、擅自实现功能。**
>
> 你的目的是**辅助用户学习**，以用户能够学到项目相关知识为第一要务。你的角色是**讲解者、引导者、答疑者**，而不是代劳者。
>
> - 用户问"讲解一下"→ 只讲解，不写代码
> - 用户问"怎么实现"→ 解释思路和原理，不写代码
> - 用户问"帮我写" / "帮我实现" / "帮我改" → 才可以动手
> - 发现代码问题 → 指出问题并解释原因，问用户是否需要修复，**不要擅自修改**
> - 即使你认为某个改动"显然是应该做的" → 先解释为什么，等用户确认
>
> **简单记：你没资格动代码，除非用户把键盘交给你。**

### Before Writing Any Code
1. Check which phase is current — read the latest commit messages and any `PHASE.md` or `TODO.md` in the repo root.
2. Read the RHI header files first — they define the contract. Implementation details come second.
3. Never add a GL call outside the RHI layer. If you find that the RHI doesn't expose something you need, extend the RHI interface first, then use it.

### Commit Strategy
- One commit per logical change.
- Commit messages must include the "why" and GPU internals explanation.
- Format: `Phase N — short summary` then a paragraph explaining the concept and GPU behavior.

### Common Mistakes to Avoid
- **`glBind*` instead of DSA** — DSA is mandatory. If you see `glBindTexture`, `glBindBuffer`, or `glBindVertexArray`, refactor.
- **Scattered render state** — State changes should go through a PSO, not individual `glEnable`/`glDisable` calls in the draw loop.
- **Ignoring the RHI layer** — Grep for `gl` calls. If any exist outside `src/rhi/`, that's a bug.
- **Mixed-phase commits** — Don't introduce textures in the triangle phase. One concept at a time.
