# RHI Vulkan 后端实现（最小可用）

## TL;DR

> **Quick Summary**: 在 `core/vulkan/` 实现与 OpenGL 后端平行的最小可用 Vulkan 后端（`vulkan_rhi` 共享库），覆盖全部 8 个 RHI 接口，通过编译时 `#ifdef O5M_HAS_VULKAN` 与 GL 后端共存。运行时 shaderc 编译 GLSL→SPIR-V，手写 VkDeviceMemory 分配器，内部合成 VkRenderPass，`beginFrame/endFrame` 内完成 acquire/submit/present。
>
> **Deliverables**:
> - `core/rhi/ClearValue.h`（接口类型修复，从 GL 后端头文件迁出）
> - `core/vulkan/VulkanDevice.h/.cpp`（VkInstance/Device/Queue/Swapchain 单例）
> - `core/vulkan/VKMemoryAllocator.h/.cpp`（手写内存分配器）
> - `core/vulkan/VKBuffer.h/.cpp`（重写现有空壳）
> - `core/vulkan/VKTexture.h/.cpp`
> - `core/vulkan/VKShader.h/.cpp`（shaderc 编译 + SPIR-V 模块）
> - `core/vulkan/VKShaderResourceBindings.h/.cpp`（descriptor set 最小实现）
> - `core/vulkan/VKPipeline.h/.cpp`（VkGraphicsPipelineCreateInfo 映射）
> - `core/vulkan/VKFramebuffer.h/.cpp`（render pass 合成 + VkFramebuffer）
> - `core/vulkan/VKCommandBuffer.h/.cpp`（N 缓冲记录式命令缓冲）
> - `core/vulkan/VKRhi.h/.cpp`（工厂 + 帧循环）
> - `main/rhi_verify.cpp` #ifdef 分支（Vulkan 版含 staging buffer 像素回读验证）
> - CMake：`O5M_VULKAN=ON` 时链接 shaderc + 构建 `vulkan_rhi`
>
> **Estimated Effort**: XL（~14 实现任务 + 4 验证任务）
> **Parallel Execution**: YES — 5 个 Wave
> **Critical Path**: T1 ClearValue → T3 VulkanDevice → T4/T5/T6/T7 资源 → T9 VKPipeline → T11 VKCommandBuffer → T12 VKRhi → T13 rhi_verify → F1-F4 → user okay

---

## Context

### Original Request
> 我正在为RHI层编写Vulkan实现，请针对Rhi的api设计一份可以与之配合的实现，并保证rhi在opengl和vulkan都可以很好的运行。

### Interview Summary
**Key Discussions**:
- **后端选择**：编译时 `#ifdef O5M_HAS_VULKAN`（利用已存在但未使用的宏），不做运行时工厂。一次编译一个后端。
- **实现深度**：最小可用 —— 跑通 rhi_verify 场景；descriptor set / sampler / 实例化按接口签名实现为最小可用，可后续 Phase 深化。
- **内存管理**：手写 VkDeviceMemory 分配器（教学价值：讲清 device-local vs host-visible），不引入 VMA。
- **GLSL→SPIR-V**：运行时 shaderc 编译（随 Vulkan SDK 提供），接口零改动。
- **接口修改**：最小侵入 —— 只把 `ClearValue` 从 `GLCommandBuffer.h` 迁到 `core/rhi/ClearValue.h`（float/int 布局，与 VkClearValue 兼容）。
- **像素验证**：Vulkan 版 rhi_verify 用 staging buffer + `vkCmdCopyImageToBuffer` + fence 做完整回读断言（教学点：GPU→CPU 同步）。
- **验证程序**：单文件 `rhi_verify.cpp` 内 `#ifdef` 分支。
- **技术默认值**：`IShader::link()` 为 no-op（管线创建时校验）；validation layers 在 debug 构建默认开启、找不到则警告跳过；present mode 用 FIFO（与 GL vsync 行为一致）。

**Research Findings**:
- RHI 的 `beginPass/endPass`、PSO 状态捆绑、binding 槽位绑定本就是照 Vulkan 概念设计的：映射到 `VkRenderPass`、`VkGraphicsPipelineCreateInfo`、`VkDescriptorSet` 接近 1:1。
- GL 命令缓冲是立即模式（每调用立即执行）；Vulkan 需要真正的记录式命令缓冲（N 缓冲，每帧提交）。
- `IShaderResourceBindings::create()` 在 GL 是 `return true`（no-op）——Vulkan 需要真正的 descriptor set 分配，这是最大的接口分歧点。
- 接口没有 sampler 概念（纹理隐式合并 image-sampler）——Vulkan 需在后端内部提供默认 sampler。
- `IShader::link()`、`uniformBlocks()`、`textureBindings()`、`IShaderResourceBindings` 全部、`setIndexBuffer`/`drawIndexed` 等在 rhi_verify.cpp 中**从未被调用**——可最小实现。
- `IGraphicsPipeline::create()` 在 GL 中无条件调用 `m_gs->setProgram()`/`link()`（geometry 为 nullptr 时潜在崩溃）——Vulkan 需显式处理可选 stage。
- macOS GL 上限 4.1 但代码用 4.5+ DSA——GL 后端在 macOS 可能无法运行（Vulkan 完全绕开此问题，是 VK 后端的动机之一）。
- CMake 已预留 `O5M_VULKAN` 选项 + `vulkan_rhi` 目标脚手架；`O5M_HAS_VULKAN` 宏已定义但未使用。
- **纠正**：项目实际用 `#ifndef __X_H__` 守卫（AGENTS.md 声称 `#pragma once` 有误）。

### Metis Review
**Identified Gaps** (addressed):
- **G1/G2**：无 GL 调用在 `core/opengl/` 之外；无 Vulkan 调用在 `core/vulkan/` 之外（rhi_verify 的 #ifdef 分支除外）→ 纳入 Guardrails。
- **G3**：8 个接口全部要有完整实现，禁止 `assert(false)` 桩 → 最小可用但结构完整。
- **G4**：`ClearValue` 迁移必须最先做（接口变更影响两个后端）→ Task 1。
- **G6-G9**：教学优先——每个 Vulkan 概念注释"为什么存在"、GL vs Vulkan 对比、内存类型讲解 → 纳入每个任务的 What to do。
- **C1-C12 范围锁死**：无 descriptor pool 回收、无 resize 处理、无 MSAA、无 mipmap、无 cubemap/array、无 pipeline cache、无 push constant、单 subpass、compute 桩、无 SPIRV-Cross（反射用空实现，因为 rhi_verify 不调用）、手写分配器、drawIndexed 最小桩 → 纳入 Must NOT Have。
- **A1-A8 假设**：shaderc 可用性（Task 2 验证）、MoltenVK portability subset（Task 3 处理）、validation layers 缺失降级（Task 3）等。

---

## Work Objectives

### Core Objective
在 `core/vulkan/` 实现一个最小可用的 Vulkan RHI 后端，与 OpenGL 后端共享同一套接口，使 `rhi_verify.cpp` 在 `O5M_VULKAN=ON` 时用 Vulkan 跑通完整渲染 + 像素验证流程，且不改动任何 RHI 接口（仅迁移 `ClearValue` 类型）。

### Concrete Deliverables
- `core/rhi/ClearValue.h` — ClearValue 接口类型（float/int 布局）
- `core/vulkan/` 下 10 个实现文件对（VulkanDevice, VKMemoryAllocator, VKBuffer, VKTexture, VKShader, VKShaderResourceBindings, VKPipeline, VKFramebuffer, VKCommandBuffer, VKRhi）
- `main/rhi_verify.cpp` 的 `#ifdef O5M_HAS_VULKAN` 分支（含 staging buffer 像素回读）
- CMake 修改（shaderc 查找 + `vulkan_rhi` 目标激活 + rhi_verify 条件链接）
- 教学文档（前置知识/架构讨论/关键问题/踩坑预警，遵循 AGENTS.md 输出格式）

### Definition of Done
- [ ] `cmake -DO5M_VULKAN=ON .. && make` 成功构建 `vulkan_rhi` + `rhi_verify`
- [ ] 默认（无 O5M_VULKAN）构建不受影响，GL 版 rhi_verify 行为与改动前一致
- [ ] Vulkan 版 rhi_verify 运行：无 validation error，中心像素断言 PASS，退出码 0
- [ ] `grep -r 'vk[A-Z]\|Vk[A-Z]' --include='*.cpp' --include='*.h' core/opengl main/` 结果仅出现在 `#ifdef O5M_HAS_VULKAN` 分支内（G2）

### Must Have
- 全部 8 个 RHI 接口在 Vulkan 后端有完整实现（无 `assert(false)` 桩）
- `beginFrame()`/`endFrame()` 完成 acquire → record → submit → present 全链路
- `beginPass(nullptr)` 约定：nullptr = 渲染到 swapchain（Vulkan 版呈现路径）
- staging buffer 像素回读验证（教学点：GPU→CPU 同步）
- 手写内存分配器 + 内存类型选择逻辑（教学点：device-local vs host-visible）
- 教学注释：每个 Vulkan 概念解释"为什么存在"（G6）

### Must NOT Have (Guardrails)
- **无 VMA**、无 SPIRV-Cross、无 pipeline cache、无 push constant
- **无 swapchain resize 处理**（固定 800×600；检测到 resize → log + skip frame）
- **无 MSAA**（`samples > 1` → assert）、**无 mipmap 生成**（`GENERATEMIPS` 忽略）、**无 cubemap/array**（忽略 CUBEMAP/TEXTUREARRAY）
- **无 descriptor pool 回收**（每帧线性分配）、单 subpass、无多 pass
- **compute shader**：`compile()` 返回 false（桩但非 assert）
- 不扩展 `IRhi` 接口（除了 ClearValue 迁移）；`link()` = no-op
- 无 GL 调用在 `core/opengl/` 之外；无 Vulkan 调用在 `core/vulkan/` 之外（除 rhi_verify #ifdef 分支）
- 不修 GL 后端已有 bug（m_gs null 崩溃、drawIndexed offset、macOS DSA）——记录为已知问题，不在本计划范围

---

## Verification Strategy (MANDATORY)

> **ZERO HUMAN INTERVENTION** — 所有验证由执行代理完成。

### Test Decision
- **Infrastructure exists**: NO（项目无单元测试框架，仅 rhi_verify 可执行程序自验证）
- **Automated tests**: NO（遵循项目现状；rhi_verify 本身就是验证程序）
- **Framework**: none
- **QA Policy**: rhi_verify 是自带断言的验证程序（像素回读 + 退出码），代理通过构建 + 运行 + 检查 stdout/退出码/validation 输出完成验证。

### QA Policy
每个任务必须包含代理执行 QA 场景，证据存 `.sisyphus/evidence/task-{N}-{scenario-slug}.{ext}`。

- **构建验证**: Bash — `cmake -DO5M_VULKAN=ON .. && cmake --build . -j$(nproc)`，捕获成功/失败
- **运行时验证**: Bash — 运行 `./rhi_verify`，检查退出码 + stdout（"center pixel PASS"）+ stderr（无 Vulkan validation error）
- **GL 回归**: Bash — 无 O5M_VULKAN 构建，运行 GL 版 rhi_verify 确认行为不变
- 本任务是 GUI 应用（GLFW 窗口），运行验证在 macOS 桌面会话中执行；窗口打开、程序自动退出（3 帧后）并打印结果

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 0 (前置 — 接口修复，最先完成):
└── T1: ClearValue 迁移到 core/rhi/ [quick]

Wave 1 (基础 — 并行铺开):
├── T2: CMake 接线（shaderc + vulkan_rhi 目标激活）[quick]
├── T3: VulkanDevice 单例（Instance/Device/Queue/Swapchain/validation）[deep]
└── T4: VKMemoryAllocator（手写，依赖 T3）[quick]

Wave 2 (资源 — 最大并行):
├── T5: VKBuffer（依赖 T3, T4）[quick]
├── T6: VKTexture（依赖 T3, T4）[unspecified-high]
├── T7: VKShader + shaderc（依赖 T2, T3）[deep]
└── T8: VKShaderResourceBindings（依赖 T3, T7 — 默认 sampler + descriptor 布局）[unspecified-high]

Wave 3 (管线 — 依赖 Wave 2):
├── T9: VKPipeline（依赖 T7 — reflection + state→VkGraphicsPipelineCreateInfo）[deep]
└── T10: VKFramebuffer + render pass 合成（依赖 T3, T6）[unspecified-high]

Wave 4 (集成):
├── T11: VKCommandBuffer 记录式 N 缓冲（依赖 T5, T6, T8, T9, T10）[deep]
└── T12: VKRhi 工厂 + 帧循环（依赖 T11）[deep]

Wave 5 (应用层 + 文档):
├── T13: rhi_verify.cpp #ifdef 分支 + 像素回读（依赖 T12）[unspecified-high]
└── T14: 教学文档 PHASE-VULKAN.md（依赖 T13 完成以写入真实踩坑）[writing]

Wave FINAL (4 并行审查 → 用户确认):
├── F1: Plan compliance audit (oracle)
├── F2: Code quality review (unspecified-high)
├── F3: Real manual QA (unspecified-high)
└── F4: Scope fidelity check (deep)
-> Present results -> Get explicit user okay

Critical Path: T1 → T3 → T7 → T9 → T11 → T12 → T13 → F1-F4 → user okay
Parallel Speedup: ~60% vs sequential
Max Concurrent: 4 (Wave 1 & 2)
```

### Dependency Matrix

- **T1**: — , blocks T13 (rhi_verify 的 ClearValue include)
- **T2**: — , blocks T7（shaderc 链接）
- **T3**: — , blocks T4, T5, T6, T7, T8, T10
- **T4**: T3, blocks T5, T6
- **T5**: T3, T4, blocks T11
- **T6**: T3, T4, blocks T10, T11
- **T7**: T2, T3, blocks T8, T9
- **T8**: T3, T7, blocks T11
- **T9**: T7, blocks T11
- **T10**: T3, T6, blocks T11
- **T11**: T5, T6, T8, T9, T10, blocks T12
- **T12**: T11, blocks T13
- **T13**: T1, T12, blocks T14
- **T14**: T13

### Agent Dispatch Summary

- **Wave 0**: T1 → `quick`
- **Wave 1**: T2 → `quick`, T3 → `deep`, T4 → `quick`
- **Wave 2**: T5 → `quick`, T6 → `unspecified-high`, T7 → `deep`, T8 → `unspecified-high`
- **Wave 3**: T9 → `deep`, T10 → `unspecified-high`
- **Wave 4**: T11 → `deep`, T12 → `deep`
- **Wave 5**: T13 → `unspecified-high`, T14 → `writing`
- **FINAL**: F1 → `oracle`, F2 → `unspecified-high`, F3 → `unspecified-high`, F4 → `deep`

---

## TODOs

> Implementation + Test = ONE Task. 每个任务必须有：Recommended Agent Profile + Parallelization + QA Scenarios。

- [x] 1. 迁移 `ClearValue` 到 `core/rhi/ClearValue.h`

  **What to do**:
  - 新建 `core/rhi/ClearValue.h`，定义 `struct ClearValue { bool active; float color[4]; float depth; int stencil; };`（float/int 布局，与 `VkClearValue` 内存布局兼容；注释说明为何 active=false 表示"不清除"→ Vulkan loadOp=LOAD）
  - 删除 `core/opengl/GLCommandBuffer.h` 中的 `ClearValue` 定义（原 `GLfloat`/`GLint` 成员改为 float/int），改为 `#include "ClearValue.h"`
  - 检查 `ICommandBuffer.h` 的前向声明 `struct ClearValue;` 是否需要改为 include（保留前向声明即可，但 GLCommandBuffer.h 必须 include 新头）
  - 确认 `main/rhi_verify.cpp` 中构造 ClearValue 的代码（`colorClear.active`、`colorClear.color[0..3]`、`depthClear.depth` 字段名不变）无需修改
  - **教学注释**：解释为何这个类型原本泄漏在 GL 后端、为何 float/int 布局与 VkClearValue 兼容（VkClearValue 是 union{float color[4]; struct{float depth; uint32_t stencil;}}）

  **Must NOT do**:
  - 不改变字段名或顺序（会破坏 rhi_verify.cpp 调用方和 GL 实现）
  - 不引入 Vulkan 头文件依赖（ClearValue.h 必须纯 C++，不含 vk 类型）

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 单文件移动 + 删定义，纯机械重构
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: NO（接口变更必须先落地，且 T13 依赖）
  - **Parallel Group**: Wave 0 (前置)
  - **Blocks**: T13（rhi_verify include）
  - **Blocked By**: None

  **References**:
  - `core/opengl/GLCommandBuffer.h:13-18` - 现有 ClearValue 定义（要删除的）
  - `core/rhi/ICommandBuffer.h:11` - 前向声明 `struct ClearValue;`
  - `main/rhi_verify.cpp:166-176` - 调用方如何构造/使用 ClearValue（字段名契约）
  - `core/opengl/GLCommandBuffer.cpp:9-34` - GL 实现如何使用 ClearValue（active 语义）

  **Acceptance Criteria**:
  - [ ] `core/rhi/ClearValue.h` 存在，含完整 struct
  - [ ] `grep -n 'ClearValue' core/opengl/GLCommandBuffer.h` 只出现 include 和类型使用，无 struct 定义
  - [ ] 默认构建成功（`cmake -S . -B build-gl && cmake --build build-gl -j$(nproc)`）

  **QA Scenarios**:
  ```
  Scenario: GL 构建回归（ClearValue 迁移后）
    Tool: Bash
    Preconditions: 无
    Steps:
      1. cmake -S . -B build-gl && cmake --build build-gl -j$(nproc) 2>&1 | tail -5
      2. 断言 exit code 0 且无 "error:" 行
    Expected Result: 构建成功，无编译错误
    Failure Indicators: 编译错误（头文件路径/类型不匹配）
    Evidence: .sisyphus/evidence/task-1-gl-build.txt

  Scenario: 接口层无 Vulkan/GL 泄漏
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -rn 'vk[A-Z]\|Vk[A-Z]\|gl[A-Z]' core/rhi/ClearValue.h
      2. 断言 0 匹配
    Expected Result: ClearValue.h 是纯 C++，无任何后端类型
    Failure Indicators: 出现 vk/gl 类型
    Evidence: .sisyphus/evidence/task-1-clean-interface.txt
  ```

  **Commit**: YES
  - Message: `refactor(rhi): move ClearValue into core/rhi/`
  - Files: `core/rhi/ClearValue.h`, `core/opengl/GLCommandBuffer.h`
  - Pre-commit: `cmake --build build-gl -j$(nproc)`

- [x] 2. CMake 接线：shaderc + `vulkan_rhi` 目标激活

  **What to do**:
  - 根 `CMakeLists.txt`：在 `if(O5M_VULKAN)` 块内添加 `find_package(shaderc REQUIRED)`（Vulkan SDK 自带 shaderc 的 CMake config；若 `find_package` 失败，fallback 到链接 `${Vulkan_LIBRARIES}` 中的 `shaderc_combined`），并 `message(STATUS)` 输出 shaderc 版本
  - `core/CMakeLists.txt`：`vulkan_rhi` 目标已存在脚手架，补充 `target_link_libraries(vulkan_rhi PRIVATE shaderc::shaderc_combined)`（或按 fallback 路径）；确认 include 目录含 Vulkan + shaderc
  - 新增一个临时占位 `.cpp`（如 `core/vulkan/VKPlaceholder.cpp`，仅 `int vk_placeholder = 0;`）使 GLOB 收集到源文件——后续任务会替换；**或**确认 T3 的 VulkanDevice.cpp 先落盘
  - 根 `CMakeLists.txt` 的 `rhi_verify` 目标：`if(O5M_VULKAN) target_link_libraries(rhi_verify PRIVATE vulkan_rhi) else() target_link_libraries(rhi_verify PRIVATE opengl_rhi) endif()`
  - 验证 `O5M_VULKAN=ON` 构建时 `vulkan_rhi` 目标出现

  **Must NOT do**:
  - 不修改 RHI 接口头文件
  - 不引入 vcpkg/外部包管理（shaderc 必须来自 Vulkan SDK 或系统）

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 纯 CMake 配置修改，无算法逻辑
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with T3, T4)
  - **Blocks**: T7（VKShader 需要 shaderc 链接）
  - **Blocked By**: None

  **References**:
  - `CMakeLists.txt:31-40` - 现有 O5M_VULKAN 块
  - `core/CMakeLists.txt:64-96` - 现有 vulkan_rhi 目标脚手架
  - `CMakeLists.txt:74-79` - rhi_verify 目标（当前硬编码链接 opengl_rhi）

  **Acceptance Criteria**:
  - [ ] `cmake -S . -B build-vk -DO5M_VULKAN=ON` 配置成功，输出 shaderc 找到
  - [ ] `grep -n 'vulkan_rhi' build-vk/CMakeCache.txt` 或构建日志显示目标存在
  - [ ] 默认构建（无 O5M_VULKAN）不受影响

  **QA Scenarios**:
  ```
  Scenario: O5M_VULKAN=ON 配置成功
    Tool: Bash
    Preconditions: 系统装有 Vulkan SDK（含 shaderc）
    Steps:
      1. cmake -S . -B build-vk -DO5M_VULKAN=ON 2>&1 | grep -i 'shaderc\|vulkan'
      2. 断言输出含 shaderc 找到信息且 exit 0
    Expected Result: CMake 配置成功
    Failure Indicators: "Could not find shaderc" / CMake Error
    Evidence: .sisyphus/evidence/task-2-vk-cmake-config.txt

  Scenario: 默认构建回归
    Tool: Bash
    Preconditions: 无
    Steps:
      1. cmake -S . -B build-gl 2>&1 | tail -3
      2. 断言 exit 0
    Expected Result: 默认配置不受 O5M_VULKAN 改动影响
    Failure Indicators: CMake Error
    Evidence: .sisyphus/evidence/task-2-gl-cmake-config.txt
  ```

  **Commit**: YES
  - Message: `build(vulkan): wire shaderc + activate vulkan_rhi target`
  - Files: `CMakeLists.txt`, `core/CMakeLists.txt`
  - Pre-commit: `cmake -S . -B build-vk -DO5M_VULKAN=ON`

- [x] 3. `VulkanDevice` 单例：VkInstance / PhysicalDevice / Device / Queue / Swapchain / validation

  **What to do**:
  - 新建 `core/vulkan/VulkanDevice.h/.cpp`——**这是 Vulkan 后端的心脏**，VKRhi 拥有它，所有 VK* 类通过它获取 VkDevice/VkQueue 进行资源创建/销毁
  - 成员：`VkInstance m_instance`、`VkPhysicalDevice m_physicalDevice`、`VkDevice m_device`、`VkQueue m_graphicsQueue`、`uint32_t m_queueFamilyIndex`、`VkSurfaceKHR m_surface`、`VkSwapchainKHR m_swapchain`、`std::vector<VkImage> m_swapchainImages`、`std::vector<VkImageView> m_swapchainImageViews`、`VkFormat m_swapchainFormat`、`VkExtent2D m_swapchainExtent`、debug messenger（debug 构建）
  - `init(GLFWwindow*)`：创建 instance（glfwGetRequiredInstanceExtensions + VK_KHR_portability_enumeration，macOS 必需）+ debug utils messenger（validation layers 有则开，无则警告跳过 A5）→ 枚举物理设备（选 discrete GPU，A6：启用 VK_KHR_portability_subset）→ 选 queue family（graphics + present 同一族）→ 创建 device（启用 swapchain + portability subset 扩展）→ `glfwCreateWindowSurface` → 创建 swapchain（格式选 SRGB8 优先 fallback BGR，present mode FIFO，image count = 3 或 min+1）
  - 提供查询方法：`swapchainFormat()`、`swapchainExtent()`、`imageCount()`、`swapchainImageView(i)`、`device()`、`queue()`、`queueFamilyIndex()`、`findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags)`（供 VKMemoryAllocator 用）
  - `shutdown()`：按创建逆序销毁所有 Vk 对象（swapchain views → swapchain → surface → device → messenger → instance）
  - **教学注释**：为什么 Vulkan 需要显式 instance/device（GL 是隐式全局状态）；queue family 概念；swapchain 是什么（GL 的默认 framebuffer 等价物，但由应用管理）；为什么 macOS 需要 portability subset（MoltenVK 不是完整 Vulkan）
  - 处理 validation：debug 构建默认 `VK_LAYER_KHRONOS_validation`，若 `vkEnumerateInstanceLayerProperties` 查不到则警告并跳过（不阻塞）

  **Must NOT do**:
  - 不实现 resize 处理（固定 800×600；swapchain 重建逻辑留待未来）
  - 不在此文件写任何 GL 调用
  - 不做多 queue family 的复杂选择（graphics + present 必须同一族，找不到则报错——教学上简化）

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: Vulkan 初始化是全项目最易错的部分（扩展/层/queue family/swapchain 细节多），需要深度理解和验证
  - **Skills**: `[]`
    - 说明：本项目无现成 Vulkan skill；依赖代理自身知识 + 引用官方文档
  - **Skills Evaluated but Omitted**:
    - `context7-mcp`: 若需查 Vulkan 官方 API 细节可用，但核心模式已知，非必需

  **Parallelization**:
  - **Can Run In Parallel**: YES（与 T2 无文件冲突）
  - **Parallel Group**: Wave 1 (with T2, T4)
  - **Blocks**: T4, T5, T6, T7, T8, T10
  - **Blocked By**: None

  **References**:
  - `core/opengl/GLRhi.cpp:init()` - GL 版后端初始化的对照（init(GLFWwindow*) 模式）
  - `main/rhi_verify.cpp:67-81` - 窗口创建（GLFW 上下文，Vulkan 版需 `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`）
  - Vulkan 官方文档: `https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html` - instance/device/swapchain 创建顺序
  - Vulkan Tutorial (macOS): `https://vulkan-tutorial.com/Development_environment#page_macOS` - MoltenVK 注意事项

  **Acceptance Criteria**:
  - [ ] VulkanDevice.h/.cpp 存在，包含上述全部成员和 init/shutdown
  - [ ] 能编译（`cmake --build build-vk -j$(nproc)`，若 T2 已激活目标）
  - [ ] 代码中所有 `vkCreate*`/`vkAllocate*` 返回值用 `VK_CHECK` 宏检查（教学注释解释 VkResult 语义）

  **QA Scenarios**:
  ```
  Scenario: 编译通过
    Tool: Bash
    Preconditions: build-vk 目录已配置（T2）
    Steps:
      1. cmake --build build-vk -j$(nproc) 2>&1 | grep -E 'error|Error' | head -20
      2. 断言无 error
    Expected Result: VulkanDevice 编译通过
    Failure Indicators: 编译错误（扩展名、枚举、签名）
    Evidence: .sisyphus/evidence/task-3-build.txt

  Scenario: 无 GL 泄漏
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -rn 'gl[A-Z]\|glad' core/vulkan/VulkanDevice.h core/vulkan/VulkanDevice.cpp
      2. 断言 0 匹配
    Expected Result: VulkanDevice 纯 Vulkan
    Failure Indicators: 出现 GL 调用
    Evidence: .sisyphus/evidence/task-3-no-gl.txt
  ```

  **Commit**: YES
  - Message: `feat(vulkan): VulkanDevice singleton — instance/device/queue/swapchain`
  - Files: `core/vulkan/VulkanDevice.h`, `core/vulkan/VulkanDevice.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc)`

- [x] 4. `VKMemoryAllocator`：手写 VkDeviceMemory 分配器

  **What to do**:
  - 新建 `core/vulkan/VKMemoryAllocator.h/.cpp`——教学核心：展示"为什么 Vulkan 需要内存分配器"
  - 设计（简化教学版）：
    - `struct MemoryBlock { VkDeviceMemory memory; VkDeviceSize offset; VkDeviceSize size; }`
    - `MemoryBlock allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags preferredProps)`：用 VulkanDevice::findMemoryType 选 memory type，调用 `vkAllocateMemory`（一次性分配，不搞 sub-allocation 池——教学简化），返回 block
    - `void free(MemoryBlock)`：`vkFreeMemory`
    - 静态工具：`VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)`（教学：为什么 Vulkan 要求对齐）
  - 每个 `vkAllocateMemory` 注释解释 memory type 含义（device-local vs host-visible vs host-coherent——对应 BufferDesc::STATIC/DYNAMIC/STREAM 的教学点）
  - **注意**：这是"最简分配器"——每个资源一块独立 VkDeviceMemory，不实现池化/复用（Metis C1 锁死：无回收）。文档注明真实引擎用 VMA 的原因（碎片、sub-allocation）

  **Must NOT do**:
  - 不引入 VMA
  - 不实现空闲链表/复用/池化
  - 不在此文件写 GL

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 单类两个方法 + 一个对齐工具，结构简单
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with T2, T3)
  - **Blocks**: T5, T6
  - **Blocked By**: T3（需要 VulkanDevice 的 findMemoryType）

  **References**:
  - Vulkan 官方文档 memory 部分: `https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#memory` - memory types & properties
  - `core/rhi/IBuffer.h:BufferDesc::E_MEMORYHINT` - STATIC/DYNAMIC/STREAM 语义（本任务只实现分配器，映射在 T5）

  **Acceptance Criteria**:
  - [ ] VKMemoryAllocator.h/.cpp 存在，含 allocate/free/alignUp
  - [ ] `grep -rn 'VMA\|vmaAllocate' core/vulkan/` → 0 匹配
  - [ ] 编译通过

  **QA Scenarios**:
  ```
  Scenario: 编译通过
    Tool: Bash
    Preconditions: build-vk 已配置
    Steps:
      1. cmake --build build-vk -j$(nproc) 2>&1 | grep -E 'error' | head -10
      2. 断言无 error
    Expected Result: 编译通过
    Failure Indicators: 编译错误
    Evidence: .sisyphus/evidence/task-4-build.txt

  Scenario: 无 VMA 依赖
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -rin 'vma' core/vulkan/ --include='*.h' --include='*.cpp'
      2. 断言 0 匹配
    Expected Result: 纯手写分配器
    Failure Indicators: 出现 vma
    Evidence: .sisyphus/evidence/task-4-no-vma.txt
  ```

  **Commit**: YES
  - Message: `feat(vulkan): hand-written VkDeviceMemory allocator`
  - Files: `core/vulkan/VKMemoryAllocator.h`, `core/vulkan/VKMemoryAllocator.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc)`

- [x] 5. `VKBuffer`：重写现有空壳，实现完整 IBuffer

  **What to do**:
  - **重写** `core/vulkan/VKBuffer.h`（现有文件只有 `BufferDesc m_desc` + `#ifndef` 守卫，无任何 Vk 成员）并新建 `VKBuffer.cpp`
  - 类结构（对照 GLBuffer）：
    ```cpp
    class VKBuffer : public IBuffer {
    public:
        explicit VKBuffer(const BufferDesc& desc);
        ~VKBuffer() override;                    // vkDestroyBuffer + 释放 memory
        bool create() override;
        void upload(const void* data, size_t size, size_t offset) override;
        void* map(size_t offset, size_t size) override;
        void unmap() override;
        size_t sizeInBytes() const override;
        const BufferDesc& desc() const;
        VkBuffer handle() const;                 // 供命令缓冲绑定
    private:
        BufferDesc m_desc;
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_memory = VK_NULL_HANDLE;
        VkDeviceSize m_allocationSize = 0;
        void* m_mappedPtr = nullptr;             // 持久映射（教学：动态缓冲优化）
        VkBuffer m_stagingBuffer = VK_NULL_HANDLE; // STATIC 上传用
    };
    ```
  - `create()`：`BufferDesc.usage` 位掩码 → `VkBufferUsageFlags`（VERTEXBUFFER→VERTEX_BUFFER_BIT、INDEXBUFFER→INDEX_BUFFER_BIT、UNIFORMBUFFER→UNIFORM_BUFFER_BIT、STORAGEBUFFER→STORAGE_BUFFER_BIT）——**教学注释**：这是 GL 里隐式的（GL 用 glBufferData 目标暗示用途），Vulkan 显式声明
  - 内存策略（教学点——对应 E_MEMORYHINT）：
    - STATIC → `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`，`upload()` 通过 staging buffer（host-visible）+ `vkCmdCopyBuffer`（需要命令缓冲——简化方案：用单次提交的临时命令缓冲，或后续 T11 完成后再细化；**推荐**：本任务实现 staging + 临时立即提交辅助函数）
    - DYNAMIC/STREAM → `VK_MEMORY_PROPERTY_HOST_VISIBLE | HOST_COHERENT`，`map()` 直接 `vkMapMemory`（或 create 时持久映射，对应 IBuffer.h 的 `//TODO Permentally mapping` 注释！）
  - `upload(data, size, offset)`：host-visible 直接 memcpy 到 mapped ptr；device-local 走 staging + copy
  - `unmap()`：host-visible 无操作（coherent）；注释解释 HOST_COHERENT 与 flush 的关系（教学点）
  - 析构：`vkDestroyBuffer` + allocator.free
  - **重写时注意**：现有 VKBuffer.h 的 `desc()` 返回值是 `BufferDesc`（by value），GLBuffer 是 `const BufferDesc&`——统一为 `const BufferDesc&`（与 GL 一致）

  **Must NOT do**:
  - 不实现 MSAA/索引缓冲特殊路径（index 就是普通 buffer）
  - 不在此文件写 GL
  - 不引入 VMA

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 单类实现，模式与 GLBuffer 平行；staging 提交是唯一复杂度（可复用 T3 的辅助或临时 cmd buffer）
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with T6, T7, T8)
  - **Blocks**: T11（命令缓冲需要 VKBuffer）
  - **Blocked By**: T3, T4

  **References**:
  - `core/opengl/GLBuffer.h/.cpp` - 完整的参考实现（create/upload/map/unmap 模式、desc() 返回 const ref）
  - `core/rhi/IBuffer.h` - 接口契约（usage 位掩码、E_MEMORYHINT）
  - `core/vulkan/VKBuffer.h` - 现有空壳（要重写的）
  - Vulkan 官方文档 buffer 部分: `https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#buffers`
  - Vulkan Tutorial buffers: `https://vulkan-tutorial.com/Vertex_buffers` - staging buffer 模式

  **Acceptance Criteria**:
  - [ ] VKBuffer.h 重写 + VKBuffer.cpp 存在，实现全部 6 个接口方法 + desc()/handle()
  - [ ] `grep -n 'VkBuffer\|VkDeviceMemory\|vkMapMemory' core/vulkan/VKBuffer.h` → 有实际成员（不再是空壳）
  - [ ] 编译通过

  **QA Scenarios**:
  ```
  Scenario: 编译通过
    Tool: Bash
    Preconditions: build-vk 已配置
    Steps:
      1. cmake --build build-vk -j$(nproc) 2>&1 | grep -E 'error' | head -10
      2. 断言无 error
    Expected Result: VKBuffer 编译通过
    Failure Indicators: 编译错误
    Evidence: .sisyphus/evidence/task-5-build.txt

  Scenario: 空壳已重写（有真实 Vulkan 成员）
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -c 'VkBuffer\|VkDeviceMemory\|vkMapMemory\|vkDestroyBuffer' core/vulkan/VKBuffer.cpp
      2. 断言输出 > 5
    Expected Result: 有真实 Vulkan 实现
    Failure Indicators: 计数为 0（仍是空壳）
    Evidence: .sisyphus/evidence/task-5-real-impl.txt
  ```

  **Commit**: YES
  - Message: `feat(vulkan): VKBuffer — staging + device-local memory`
  - Files: `core/vulkan/VKBuffer.h`, `core/vulkan/VKBuffer.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc)`

- [x] 6. `VKTexture`：VkImage + 内存 + VkImageView

  **What to do**:
  - 新建 `core/vulkan/VKTexture.h/.cpp`（对照 GLTexture）
  - 类结构：
    ```cpp
    class VKTexture : public ITexture {
    public:
        explicit VKTexture(const TextureDesc& desc);
        ~VKTexture() override;                   // vkDestroyImageView + vkDestroyImage + free
        bool create() override;
        void upload(const void* data, int miplevel = 0, int layer = 0) override;
        int width() const override; int height() const override; int depth() const override;
        int miplevels() const override; int layers() const override; int samples() const override;
        const TextureDesc& desc() const;
        VkImageView imageView() const;           // 供 framebuffer 附件
        VkImage image() const;
        VkFormat vkFormat() const;
    private:
        TextureDesc m_desc;
        VkImage m_image = VK_NULL_HANDLE;
        VkDeviceMemory m_memory = VK_NULL_HANDLE;
        VkImageView m_imageView = VK_NULL_HANDLE;
    };
    ```
  - `create()`：`TextureDesc.format` → `VkFormat` 映射表（R8_UNORM→R8_UNORM、RGBA8_UNORM→R8G8B8A8_UNORM、RGBA16_SFLOAT→R16G16B16A16_SFLOAT、RGBA32_SFLOAT→R32G32B32A32_SFLOAT、D16_UNORM、D24_UNORM_S8_UINT、D32_SFLOAT）
  - flags → `VkImageUsageFlags`：RENDERTARGET + 非深度格式 → `COLOR_ATTACHMENT_BIT | SAMPLED_BIT`；深度格式 → `DEPTH_STENCIL_ATTACHMENT_BIT | SAMPLED_BIT`；始终含 `TRANSFER_DST_BIT`（staging upload）
  - `VkImageCreateInfo`：imageType=2D、arrayLayers=1（忽略 layers/CUBEMAP/TEXTUREARRAY，Metis C5）、mipLevels=1（忽略 GENERATEMIPS，C4）、samples=1（C3：`samples > 1` → `assert` 报错）
  - 内存：depth 格式选 `DEVICE_LOCAL`（渲染目标）；upload 走 staging（同 T5 模式）
  - `create()` 内创建 `VkImageView`（viewType=2D，aspectMask 按格式：深度→DEPTH_STENCIL_ASPECT_BIT 否则 COLOR_ASPECT_BIT）——**教学注释**：为什么 Vulkan 区分 VkImage 和 VkImageView（image=内存布局，view=如何解读，对应 GL 的 texture 单元绑定时隐式概念）
  - 深度纹理的 stencil aspect：D24_UNORM_S8_UINT 时 aspect 含 STENCIL

  **Must NOT do**:
  - 不实现 MSAA、mipmap 生成、cubemap、texture array（C3/C4/C5 锁死）
  - 不实现 sampler（T8 提供默认 sampler）
  - 不写 GL

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 格式映射 + image 创建 + view 创建，中等复杂度；深度格式 aspect 处理易错
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with T5, T7, T8)
  - **Blocks**: T10（framebuffer 附件）、T11
  - **Blocked By**: T3, T4

  **References**:
  - `core/opengl/GLTexture.h/.cpp` - GL 参考实现（format 映射表、RENDERTARGET 处理）
  - `core/rhi/ITexture.h` - 接口契约（TextureDesc、E_TEXTURE_FORMAT、flags）
  - Vulkan Tutorial images: `https://vulkan-tutorial.com/Depth_buffering` - 深度图像 + aspect mask
  - `main/rhi_verify.cpp:113-128` - 实际使用（800×600 RGBA8_UNORM + D24_UNORM_S8_UINT RENDERTARGET）

  **Acceptance Criteria**:
  - [ ] VKTexture.h/.cpp 存在，实现全部接口 + imageView()/image()/vkFormat()
  - [ ] 7 种格式全部在映射表中
  - [ ] `samples > 1` 时 assert（C3）
  - [ ] 编译通过

  **QA Scenarios**:
  ```
  Scenario: 编译通过
    Tool: Bash
    Preconditions: build-vk 已配置
    Steps:
      1. cmake --build build-vk -j$(nproc) 2>&1 | grep -E 'error' | head -10
      2. 断言无 error
    Expected Result: 编译通过
    Failure Indicators: 编译错误
    Evidence: .sisyphus/evidence/task-6-build.txt

  Scenario: 格式映射完整性
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -c 'VK_FORMAT_' core/vulkan/VKTexture.cpp
      2. 断言输出 >= 7
    Expected Result: 7 种格式全部映射
    Failure Indicators: 少于 7（漏映射）
    Evidence: .sisyphus/evidence/task-6-formats.txt
  ```

  **Commit**: YES
  - Message: `feat(vulkan): VKTexture — image + view + format mapping`
  - Files: `core/vulkan/VKTexture.h`, `core/vulkan/VKTexture.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc)`

- [x] 7. `VKShader`：shaderc 运行时编译 GLSL → SPIR-V → VkShaderModule

  **What to do**:
  - 新建 `core/vulkan/VKShader.h/.cpp`
  - 类结构（对照 GLShader，但无 program 概念——**教学注释**：Vulkan 无"链接"步骤，stage 编译 + pipeline 创建即"链接"）：
    ```cpp
    class VKShader : public IShader {
    public:
        VKShader(E_SHADER_TYPE type, const char* source);
        ~VKShader() override;                    // vkDestroyShaderModule
        bool compile() override;                 // shaderc_compile_into_spv → vkCreateShaderModule
        bool link() override { return true; }    // no-op（管线创建时校验）
        E_SHADER_TYPE type() const override;
        const std::string& compileLog() const override;
        const std::vector<UniformBlock>& uniformBlocks() const override { return m_uniformBlocks; } // 空
        const std::vector<TextureBinding>& textureBindings() const override { return m_textureBindings; } // 空
        VkShaderModule module() const;
        VkShaderStageFlagBits stageFlag() const;
    private:
        E_SHADER_TYPE m_type;
        std::string m_source;
        std::string m_compileLog;
        VkShaderModule m_module = VK_NULL_HANDLE;
        std::vector<UniformBlock> m_uniformBlocks;    // 最小可用：空（rhi_verify 不调用反射）
        std::vector<TextureBinding> m_textureBindings; // 最小可用：空
    };
    ```
  - `compile()`：`shaderc_compiler_initialize()` → `shaderc_compile_into_spv(compiler, source, strlen, stage, "main", ...)`（E_SHADER_TYPE→shaderc_shader_kind：VERTEX→vertex_shader 等）→ 检查 `result_status == shaderc_compilation_status_success` → `shaderc_result_get_length/GetBytes` 拿 SPIR-V → `vkCreateShaderModule` → 失败时 `compileLog()` 存 shaderc 错误消息
  - **GLSL 兼容性**：rhi_verify 的 shader 是 `#version 410 core`——shaderc 能处理吗？**处理策略**：检测 source 中的 `#version`，若 < 450 则自动替换为 `#version 450`（Vulkan 需要 GLSL 450 语义：显式 location 等）；`layout(location=...)` 语法在 450 下兼容。注释解释为何 Vulkan 用 450（SPIR-V 目标版本）
  - `link()` 返回 true（no-op，符合用户决策）；`uniformBlocks()/textureBindings()` 返回空 vector（rhi_verify 不调用——Metis C10：不用 SPIRV-Cross）
  - E_SHADER_TYPE::COMPUTE → `compile()` 返回 false（Metis C9 桩，非 assert）

  **Must NOT do**:
  - 不引入 SPIRV-Cross（C10）
  - 不实现真实反射解析（最小可用：空 vector）
  - 不写 GL

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: shaderc API 使用 + GLSL 版本兼容处理 + SPIR-V 模块创建，需要验证 shaderc 实际行为
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with T5, T6, T8)
  - **Blocks**: T8（descriptor 布局参考 shader）、T9（pipeline stage）
  - **Blocked By**: T2（shaderc 链接）、T3

  **References**:
  - `core/opengl/GLShader.h/.cpp` - GL 参考（compile/link 模式、compileLog）
  - `core/rhi/IShader.h` - 接口契约
  - `main/rhi_verify.cpp:25-43` - 实际 GLSL 源码（#version 410 core）
  - shaderc API: `https://github.com/google/shaderc/blob/main/libshaderc/include/shaderc/shaderc.h` - shaderc_compile_into_spv

  **Acceptance Criteria**:
  - [ ] VKShader.h/.cpp 存在，实现全部接口
  - [ ] `grep -c 'shaderc_' core/vulkan/VKShader.cpp` → > 3（真实使用 shaderc）
  - [ ] link() 返回 true（no-op）
  - [ ] 编译通过

  **QA Scenarios**:
  ```
  Scenario: 编译通过（shaderc 已链接）
    Tool: Bash
    Preconditions: build-vk 已配置（T2 shaderc 接线完成）
    Steps:
      1. cmake --build build-vk -j$(nproc) 2>&1 | grep -E 'error|undefined' | head -10
      2. 断言无 error 和 undefined reference
    Expected Result: 编译链接通过
    Failure Indicators: undefined reference to shaderc_*（链接问题）
    Evidence: .sisyphus/evidence/task-7-build.txt

  Scenario: shaderc 真实使用
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -c 'shaderc_compile_into_spv\|shaderc_compiler_initialize\|shaderc_result_get' core/vulkan/VKShader.cpp
      2. 断言输出 >= 3
    Expected Result: 真实调用 shaderc API
    Failure Indicators: 计数 < 3（未实现）
    Evidence: .sisyphus/evidence/task-7-shaderc.txt
  ```

  **Commit**: YES
  - Message: `feat(vulkan): VKShader — shaderc GLSL→SPIR-V compile`
  - Files: `core/vulkan/VKShader.h`, `core/vulkan/VKShader.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc)`

- [x] 8. `VKShaderResourceBindings`：descriptor set 最小实现

  **What to do**:
  - 新建 `core/vulkan/VKShaderResourceBindings.h/.cpp`
  - 类结构（对照 GLShaderResourceBindings——GL 版 create() 是 `return true`，Vulkan 版要真做）：
    ```cpp
    class VKShaderResourceBindings : public IShaderResourceBindings {
    public:
        explicit VKShaderResourceBindings();
        ~VKShaderResourceBindings() override;    // vkDestroyDescriptorPool
        void bindUniformBuffer(int binding, IBuffer* buffer, size_t offset = 0, size_t size = 0) override;
        void bindTexture(int binding, ITexture* texture) override;
        bool create() override;                  // 构建 descriptor set layout + pool + set
        VkDescriptorSet descriptorSet() const;
    private:
        struct UBOBinding { VKBuffer* buffer; size_t offset; size_t size; };
        struct TexBinding { VKTexture* texture; };
        std::map<int, UBOBinding> m_UBOs;
        std::map<int, TexBinding> m_textures;
        VkDescriptorPool m_pool = VK_NULL_HANDLE;
        VkDescriptorSet m_set = VK_NULL_HANDLE;
    };
    ```
  - **默认 sampler**：VulkanDevice 维护一个全局默认 `VkSampler`（LINEAR filter、REPEAT address、无比较模式）——**教学注释**：GL 里采样器状态隐式在纹理上，Vulkan 分离了（对应 IShader::TextureBinding::Type 的 Sampler2D/SamplerCube/Sampler2DShadow 枚举）;此 sampler 在 VulkanDevice::init 创建、shutdown 销毁
  - `create()`：根据已 bind 的资源构建 `VkDescriptorSetLayoutBinding` 数组（UBO→UNIFORM_BUFFER、texture→COMBINED_IMAGE_SAMPLER，binding 用 map key）→ `vkCreateDescriptorSetLayout` → `vkCreateDescriptorPool`（maxSets=1 + 对应类型计数）→ `vkAllocateDescriptorSets` → `vkUpdateDescriptorSets`（VkDescriptorBufferInfo.range：size==0 → VK_WHOLE_SIZE，对照 GL 的 "size 0 = 整块缓冲" 约定；VkDescriptorImageInfo 带默认 sampler）→ 销毁临时 layout（或保留到 set 使用期——**简化**：layout 生命周期与 pool 一致，绑定时需要兼容性）
  - **与 pipeline layout 的兼容性问题**（Metis 已识别）：Vulkan 中 descriptor set 必须与 pipeline 的 layout 兼容。最小方案：VKPipeline 创建时根据 shader 的（空）反射构建 pipeline layout（无 descriptor），而 SRB 的 set 在 `setShaderResources` 时绑定——由于 rhi_verify 从不调用 setShaderResources，本任务实现为结构完整 + 可运行即可，**明确注释**：当未来 Phase 引入 UBO/纹理时，需要统一 descriptor layout 来源（shader 反射 vs SRB 动态构建）
  - `bindTexture` 的 ITexture* 转 VKTexture*（static_cast，与 GL 版一致的模式）

  **Must NOT do**:
  - 不做 descriptor pool 回收/复用（C1：每帧线性）
  - 不实现多 set（单 set 0）
  - 不扩展接口（不新增 create(IShader*) 参数——用户决策：最小侵入）

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: descriptor layout/pool/set 三步创建流程，需要 Vk 对象生命周期管理
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with T5, T6, T7)
  - **Blocks**: T11（命令缓冲 setShaderResources）
  - **Blocked By**: T3、T7（理论上可先做，但默认 sampler 在 T3；与 shader 反射联动在 T7 后明确）

  **References**:
  - `core/opengl/GLShaderResourceBindings.h/.cpp` - GL 参考（map<int,...> 结构、create() 的 no-op 现状）
  - `core/rhi/IShaderResourceBindings.h` - 接口契约
  - `core/rhi/IShader.h:TextureBinding` - 类型枚举（Sampler2D/Cube/Shadow → 采样器形态）
  - Vulkan Tutorial descriptor sets: `https://vulkan-tutorial.com/Uniform_buffers/Descriptor_sets`

  **Acceptance Criteria**:
  - [ ] VKShaderResourceBindings.h/.cpp 存在，实现全部接口
  - [ ] `grep -c 'vkCreateDescriptorPool\|vkUpdateDescriptorSets\|vkAllocateDescriptorSets' core/vulkan/VKShaderResourceBindings.cpp` → >= 3
  - [ ] 编译通过

  **QA Scenarios**:
  ```
  Scenario: 编译通过
    Tool: Bash
    Preconditions: build-vk 已配置
    Steps:
      1. cmake --build build-vk -j$(nproc) 2>&1 | grep -E 'error' | head -10
      2. 断言无 error
    Expected Result: 编译通过
    Failure Indicators: 编译错误
    Evidence: .sisyphus/evidence/task-8-build.txt

  Scenario: descriptor 真实实现（非 no-op）
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -c 'vkCreateDescriptorPool\|vkAllocateDescriptorSets\|vkUpdateDescriptorSets' core/vulkan/VKShaderResourceBindings.cpp
      2. 断言输出 >= 3
    Expected Result: 真实 descriptor set 分配
    Failure Indicators: 计数 < 3（仍是 no-op）
    Evidence: .sisyphus/evidence/task-8-descriptor.txt
  ```

  **Commit**: YES
  - Message: `feat(vulkan): VKShaderResourceBindings — descriptor set minimal`
  - Files: `core/vulkan/VKShaderResourceBindings.h`, `core/vulkan/VKShaderResourceBindings.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc)`

- [x] 9. `VKPipeline`：state structs → VkGraphicsPipelineCreateInfo

  **What to do**:
  - 新建 `core/vulkan/VKPipeline.h/.cpp`
  - 类结构（对照 GLPipeline——GL 的 create() 只 link program 并把 state 存起来延迟应用；**Vulkan 必须 create() 时烘焙全部状态**——Metis 关键提醒，教学点）：
    ```cpp
    class VKPipeline : public IGraphicsPipeline {
    public:
        explicit VKPipeline();
        ~VKPipeline() override;                  // vkDestroyPipeline + vkDestroyPipelineLayout
        void setShaderStages(IShader* vertex, IShader* fragment, IShader* geometry = nullptr) override;
        void setVertexInputLayout(const VertexInputLayout& layout) override;
        void setRasterizerState(const RasterizerState& state) override;
        void setDepthStencilState(const DepthStencilState& state) override;
        void setBlendState(int idx, const BlendState& state) override;
        bool create() override;                  // 烘焙所有状态 → VkGraphicsPipelineCreateInfo
        VkPipeline pipeline() const;
        VkPipelineLayout layout() const;
        const VertexInputLayout& vertexInputLayout() const;
        bool isValid() const;
    private:
        VKShader* m_vs = nullptr; VKShader* m_fs = nullptr; VKShader* m_gs = nullptr;
        VertexInputLayout m_layout;
        RasterizerState m_raster; DepthStencilState m_depth;
        std::vector<BlendState> m_blends;
        VkPipeline m_pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_layout = VK_NULL_HANDLE;
        bool m_isValid = false;
    };
    ```
  - **VkRenderPass 问题**（核心架构难点，Metis 已识别）：VkPipeline 创建必须带 VkRenderPass + subpass，但接口不给 pipeline framebuffer 格式信息。**方案（与 T10 协作）**：VKPipeline::create() 不立即创建 VkPipeline，而是标记 dirty；`VKCommandBuffer::beginPass` 拿到当前 framebuffer 后，在 `setGraphicPipeline`/首次 draw 时调用一个后端内部方法 `VKPipeline::build(VkRenderPass)` 延迟创建并缓存（key = renderPass）。**或者更简单（推荐教学版）**：由于本项目固定 800×600 + 单一 color+depth 附件组合，VKPipeline::create() 时向 VulkanDevice 请求"默认 render pass"（VulkanDevice 维护一个懒创建的默认 render pass：color=swapchain format + depth=D24_UNORM_S8_UINT）。**选择后者**——最简单且足够 rhi_verify；注释说明真实引擎如何做 render pass 兼容性缓存
  - **状态映射**（教学注释：为什么 GL 的散装 glEnable/glDisable 在 Vulkan 变成结构体）：
    - RasterizerState → `VkPipelineRasterizationStateCreateInfo`（polygonMode FILL/LINE → VK_POLYGON_MODE_FILL/LINE、cullMode NONE/FRONT/BACK → VK_CULL_MODE_NONE/FRONT/BACK、depthClamp → depthClampEnable）
    - DepthStencilState → `VkPipelineDepthStencilStateCreateInfo`（depthTest/depthWrite → enable/writeEnable、CompareOp 6 值直接映射）
    - BlendState → `VkPipelineColorBlendAttachmentState` 数组（enable、srcColorBlendFactor/dstColorBlendFactor 映射 ZERO/ONE/SRC_ALPHA/DST_ALPHA/ONE_MINUS_*；alpha 因子固定 ONE/ZERO——与 GL 实现一致；**注意**：m_blends 可能为空或少于 attachment 数，需按 0 附件或补齐默认）——**教学注释**：Vulkan 按 attachment 索引给 blend 状态，GL 的 glEnablei 同理但隐式
    - VertexInputLayout → `VkPipelineVertexInputStateCreateInfo`：Binding.stride → VkVertexInputBindingDescription（stride、inputRate=perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VERTEX；**GL 后端忽略 perInstance——Vulkan 真正实现它**，Metis 教学点）、Attribute.format → VkFormat（19 种 FORMAT 枚举映射表，FLOAT32X3→R32G32B32_SFLOAT 等）、binding/location/offset 直传
    - Shader stages → `VkPipelineShaderStageCreateInfo` 数组（vs 必填 + fs 必填 + gs 可选——**教学注释**：Vulkan 不要求 gs，GL 后端有 m_gs null 崩溃 bug，这里显式判断）
  - **VkPipelineLayout**：shader 反射为空 → pipeline layout 无 descriptor set 布局（教学注释：未来 UBO/纹理 Phase 需从 shader 反射构建 layout，与 T8 联动）
  - 需要 `VkPipelineVertexInputStateCreateInfo` + viewport/scissor state（viewport 数固定 1，dynamic state 用 VK_DYNAMIC_STATE_VIEWPORT/SCISSOR——因为命令缓冲在 beginPass 后才知道尺寸）

  **Must NOT do**:
  - 不实现 pipeline cache（C6）
  - 不做 push constant（C7）
  - 不写 GL
  - 不依赖真实 shader 反射（空反射）

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 这是 state→Vk 结构映射最多的类（vertex input/raster/depth/blend/stage 五组映射），且要处理 render pass 依赖 + dynamic state，易错
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3 (with T10)
  - **Blocks**: T11
  - **Blocked By**: T7（shader 类型/模块）

  **References**:
  - `core/opengl/GLPipeline.h/.cpp` - GL 参考（state 结构使用、attributeFormatInfo 映射模式）
  - `core/rhi/IGraphicsPipeline.h` - 接口契约（全部 state structs + 19 个 FORMAT 枚举）
  - `main/rhi_verify.cpp:140-158` - 实际使用（stride=24、两个 FLOAT32X3 attribute）
  - Vulkan Tutorial pipelines: `https://vulkan-tutorial.com/Drawing_a_triangle/Graphics_pipeline_basics` - 完整 VkGraphicsPipelineCreateInfo

  **Acceptance Criteria**:
  - [ ] VKPipeline.h/.cpp 存在，实现全部接口 + pipeline()/layout()
  - [ ] `grep -c 'VkGraphicsPipelineCreateInfo\|VkPipelineVertexInputStateCreateInfo' core/vulkan/VKPipeline.cpp` → >= 2
  - [ ] 19 种 FORMAT 枚举全部映射
  - [ ] 编译通过

  **QA Scenarios**:
  ```
  Scenario: 编译通过
    Tool: Bash
    Preconditions: build-vk 已配置
    Steps:
      1. cmake --build build-vk -j$(nproc) 2>&1 | grep -E 'error' | head -10
      2. 断言无 error
    Expected Result: 编译通过
    Failure Indicators: 编译错误
    Evidence: .sisyphus/evidence/task-9-build.txt

  Scenario: 19 种顶点格式完整映射
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -o 'VK_FORMAT_R[0-9]*G[0-9]*B[0-9]*A[0-9]*_[A-Z]*\|VK_FORMAT_R[0-9]*_[A-Z]*\|VK_FORMAT_R[0-9]*G[0-9]*_[A-Z]*' core/vulkan/VKPipeline.cpp | sort -u | wc -l
      2. 断言输出 >= 15（FLOAT/UINT 系主要格式）
    Expected Result: 顶点格式映射覆盖主要类型
    Failure Indicators: 远少于 15（漏映射）
    Evidence: .sisyphus/evidence/task-9-vertex-formats.txt
  ```

  **Commit**: YES
  - Message: `feat(vulkan): VKPipeline — state to VkGraphicsPipelineCreateInfo`
  - Files: `core/vulkan/VKPipeline.h`, `core/vulkan/VKPipeline.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc)`

- [x] 10. `VKFramebuffer`：render pass 合成 + VkFramebuffer

  **What to do**:
  - 新建 `core/vulkan/VKFramebuffer.h/.cpp`
  - 类结构（对照 GLFramebuffer——GL 用 glCreateFramebuffers + glNamedFramebufferTexture；Vulkan 需要 render pass + framebuffer 双对象）：
    ```cpp
    class VKFramebuffer : public IFramebuffer {
    public:
        explicit VKFramebuffer();
        ~VKFramebuffer() override;               // vkDestroyFramebuffer + vkDestroyRenderPass
        void attachColor(int index, ITexture* texture, int mipLevel = 0, int layer = 0) override;
        void attachDepthStencil(ITexture* texture, int mipLevel = 0, int layer = 0) override;
        bool create() override;                  // 从附件合成 render pass + 创建 framebuffer
        VkRenderPass renderPass() const;
        VkFramebuffer framebuffer() const;
        VkExtent2D extent() const;               // 从附件纹理尺寸推断
        int colorCount() const;
    private:
        struct Attachment { VKTexture* texture; int mipLevel; int layer; };
        std::map<int, Attachment> m_colorAttachments;
        Attachment m_depthAttachment;
        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    };
    ```
  - **render pass 合成**（教学核心——回答"Vulkan 为什么需要显式 render pass"）：`create()` 时遍历附件，构建 `VkAttachmentDescription` 数组：
    - color 附件：format=纹理格式、loadOp=CLEAR（**简化**：本项目总是清除；VkClearValue 的 active 语义留待未来——注释说明真正的 loadOp 决策）、storeOp=STORE、samples=1
    - depth 附件：loadOp=CLEAR、storeOp=STORE
    - `VkSubpassDescription`：colorAttachment 引用（按 attachColor 的 index 顺序）、depthStencilAttachment
    - `VkSubpassDependency`：EXTERNAL → 0（教学注释：barrier 在 Vulkan 是显式的，GL 隐式）
    - `vkCreateRenderPass`
  - **VkFramebuffer**：attachments = 各纹理的 `imageView()`（mipLevel/layer 参数：GL 实现存了但忽略 layer——**Vulkan 尊重 layer**，教学点：VkImageView 的 baseMipLevel/baseArrayLayer）、width/height 从附件纹理 `width()/height()`（**注意**：所有附件尺寸必须一致，不一致则 assert——教学点：framebuffer 尺寸一致性）、`vkCreateFramebuffer`
  - 提供 `renderPass()/framebuffer()/extent()` 供 VKCommandBuffer 使用
  - **兼容性说明**：VKPipeline 用 VulkanDevice 的"默认 render pass"（T9 方案），而 VKFramebuffer 合成自己的 render pass——**两者必须兼容**（VkRenderPass 兼容性规则：attachment format/count/sample 一致）。VulkanDevice 的默认 render pass 必须与 rhi_verify 的附件组合一致（1 color RGBA8 + 1 depth D24S8）。注释解释 render pass compatibility 规则

  **Must NOT do**:
  - 不做多 subpass（C8）
  - 不做 loadOp 智能决策（固定 CLEAR）
  - 不写 GL

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: render pass 合成是 Vulkan 独有概念（GL 没有等价物），需理解 attachment/subpass/dependency
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 3 (with T9)
  - **Blocks**: T11
  - **Blocked By**: T3、T6

  **References**:
  - `core/opengl/GLFramebuffer.h/.cpp` - GL 参考（attachColor/attachDepthStencil/create 模式、colorCount()）
  - `core/rhi/IFramebuffer.h` - 接口契约
  - `main/rhi_verify.cpp:130-134` - 实际使用（1 color + 1 depth）
  - Vulkan Tutorial render passes: `https://vulkan-tutorial.com/Drawing_a_triangle/Render_passes` - 完整 VkRenderPass 示例

  **Acceptance Criteria**:
  - [ ] VKFramebuffer.h/.cpp 存在，实现全部接口 + renderPass()/framebuffer()/extent()
  - [ ] `grep -c 'vkCreateRenderPass\|vkCreateFramebuffer' core/vulkan/VKFramebuffer.cpp` → >= 2
  - [ ] 附件尺寸不一致时 assert
  - [ ] 编译通过

  **QA Scenarios**:
  ```
  Scenario: 编译通过
    Tool: Bash
    Preconditions: build-vk 已配置
    Steps:
      1. cmake --build build-vk -j$(nproc) 2>&1 | grep -E 'error' | head -10
      2. 断言无 error
    Expected Result: 编译通过
    Failure Indicators: 编译错误
    Evidence: .sisyphus/evidence/task-10-build.txt

  Scenario: render pass 合成真实存在
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -c 'VkAttachmentDescription\|VkSubpassDescription\|VkSubpassDependency' core/vulkan/VKFramebuffer.cpp
      2. 断言输出 >= 3
    Expected Result: 真实 render pass 合成
    Failure Indicators: 计数 < 3（未合成 render pass）
    Evidence: .sisyphus/evidence/task-10-renderpass.txt
  ```

  **Commit**: YES
  - Message: `feat(vulkan): VKFramebuffer + render pass synthesis`
  - Files: `core/vulkan/VKFramebuffer.h`, `core/vulkan/VKFramebuffer.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc)`

- [x] 11. `VKCommandBuffer`：记录式 N 缓冲命令缓冲

  **What to do**:
  - 新建 `core/vulkan/VKCommandBuffer.h/.cpp`——**这是 GL 与 Vulkan 语义差异最大的类**（GL 立即执行 vs Vulkan 记录+提交，教学点）
  - 类结构：
    ```cpp
    class VKCommandBuffer : public ICommandBuffer {
    public:
        explicit VKCommandBuffer();
        ~VKCommandBuffer() override;             // 销毁 N 个命令缓冲
        void beginPass(IFramebuffer* fb, const ClearValue& colorClear, const ClearValue& depthClear) override;
        void endPass() override;
        void setGraphicPipeline(IGraphicsPipeline* pso) override;
        void setViewport(int x, int y, int w, int h) override;
        void setScissor(int x, int y, int w, int h) override;
        void setShaderResources(IShaderResourceBindings* bindings) override;
        void setVertexInput(int bindingSlot, IBuffer* buffer, size_t offset) override;
        void setIndexBuffer(IBuffer* buffer, E_INDEX_FORMAT format) override;
        void draw(int vertexCount, int firstVertex = 0) override;
        void drawIndexed(int indexCount, int firstIndex = 0, int vertexOffset = 0) override;
        void drawInstanced(int vertexCount, int instanceCount, int firstVertex = 0, int firstInstance = 0) override;
        void pushDebugGroup(const char* name) override;
        void popDebugGroup() override;
        // 后端内部（VKRhi 调用）：
        void beginRecording(VkCommandBuffer cmd, VkFramebuffer swapchainFb, VkRenderPass swapchainRp, VkExtent2D extent);
        void endRecording();
    private:
        VkCommandBuffer m_cmd = VK_NULL_HANDLE;   // 当前记录中的命令缓冲
        VKPipeline* m_currentPipeline = nullptr;
        VKFramebuffer* m_currentFramebuffer = nullptr;
        VKShaderResourceBindings* m_currentSRB = nullptr;
        std::map<int, std::pair<VKBuffer*, size_t>> m_vertexBindings;
        VKBuffer* m_indexBuffer = nullptr;
        E_INDEX_FORMAT m_indexFormat = UINT16;
        bool m_recording = false;
    };
    ```
  - **N 缓冲**：VKRhi 持有 N 个 VkCommandBuffer（N = swapchain image count，VulkanDevice 创建命令池 + 每帧一个命令缓冲）；`beginRecording(cmd, ...)` 由 VKRhi 在 beginFrame 时调用，注入当前帧的命令缓冲
  - **beginPass(fb)**：
    - fb != nullptr → 用 VKFramebuffer::renderPass()/framebuffer()/extent()
    - **fb == nullptr → 渲染到 swapchain**（用户确认的约定）：用 VulkanDevice 的默认 render pass + 当前 swapchain image view（VKRhi 在 beginRecording 注入）——**教学注释**：这是 RHI 无 surface 抽象的 workaround，Vulkan 的 swapchain 呈现路径在这里
    - `vkCmdBeginRenderPass`，clear values 从 ClearValue 转换（color[4] → VkClearValue.color，depth → VkClearValue.depthStencil；active=false → 本项目固定 CLEAR loadOp，注释说明未来可映射为 LOAD）
  - **endPass** → `vkCmdEndRenderPass`
  - **setViewport**：`(int x, int y, int w, int h)` → `VkViewport`（**注意坐标约定**：GL 原点左下 vs Vulkan 原点左上——教学点。方案：y 翻转 `height - y - h` 或用负高度 viewport 匹配 GL 的 NDC 约定。**推荐**：用负高度 + 正 y 保持与 GL 一致的 bottom-left 语义，注释解释；或者直接按 Vulkan 惯例。**与 T13 验证对齐**：选择与 GL 版输出一致的约定，使像素断言可比对）
  - **setScissor** → `VkRect2D`（offset/offset + extent；GL 的 scissor 语义 endPass 时关闭——Vulkan 无此概念，注释说明）
  - **setGraphicPipeline** → 确保 VKPipeline 已 build（若 T9 用延迟 build，这里触发）→ `vkCmdBindPipeline(PIPELINE_BIND_POINT_GRAPHICS, ...)`
  - **setVertexInput(bindingSlot, buffer, offset)** → 记录到 map（对照 GL 的 m_vertexBindings）
  - **draw** → `vkCmdBindVertexBuffers`（遍历 map：bindingSlot 数组 + offsets）→ `vkCmdDraw(vertexCount, 1, firstVertex, 0)`。**GL 的防御性 guard**（无 pipeline/framebuffer 则 return）保留
  - **drawIndexed** → `vkCmdBindIndexBuffer(indexBuffer, 0, indexType)` + `vkCmdDrawIndexed`（最小实现，rhi_verify 不用）
  - **drawInstanced** → `vkCmdDraw(vertexCount, instanceCount, firstVertex, firstInstance)`（GL 后端无 divisor 支持——Vulkan 在 VKPipeline 的 inputRate 真正支持 perInstance，教学点）
  - **pushDebugGroup/popDebugGroup** → `vkCmdBeginDebugUtilsLabelEXT/vkCmdEndDebugUtilsLabelEXT`（需加载扩展函数指针）
  - **setShaderResources** → `vkCmdBindDescriptorSets(PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &srb->descriptorSet(), ...)`（需 pipeline layout——从 VKPipeline 拿）

  **Must NOT do**:
  - 不做 secondary command buffer
  - 不实现命令回放/持久命令池优化（每帧分配即可）
  - 不写 GL

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 记录式命令缓冲 + 坐标约定 + 与 pipeline/render pass/descriptor 的联动最多，是本项目逻辑最密集的类
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: NO（集成点，依赖所有资源类）
  - **Parallel Group**: Wave 4 (sequential)
  - **Blocks**: T12
  - **Blocked By**: T5, T6, T8, T9, T10

  **References**:
  - `core/opengl/GLCommandBuffer.h/.cpp` - GL 参考（beginPass/endPass/set*/draw 模式、防御性 guard、vertex bindings map）
  - `core/rhi/ICommandBuffer.h` - 接口契约（含 ClearValue 前向声明——T1 后改为 include）
  - Vulkan Tutorial command buffers: `https://vulkan-tutorial.com/Drawing_a_triangle/Drawing/Command_buffers` - 记录 + 提交
  - Vulkan Tutorial rendering: `https://vulkan-tutorial.com/Drawing_a_triangle/Drawing/Rendering_and_presentation` - vkCmdBeginRenderPass 完整用法

  **Acceptance Criteria**:
  - [ ] VKCommandBuffer.h/.cpp 存在，实现全部接口 + beginRecording/endRecording
  - [ ] `grep -c 'vkCmdBeginRenderPass\|vkCmdDraw\|vkCmdBindPipeline' core/vulkan/VKCommandBuffer.cpp` → >= 3
  - [ ] beginPass(nullptr) → swapchain 路径存在
  - [ ] 编译通过

  **QA Scenarios**:
  ```
  Scenario: 编译通过
    Tool: Bash
    Preconditions: build-vk 已配置
    Steps:
      1. cmake --build build-vk -j$(nproc) 2>&1 | grep -E 'error' | head -10
      2. 断言无 error
    Expected Result: 编译通过
    Failure Indicators: 编译错误
    Evidence: .sisyphus/evidence/task-11-build.txt

  Scenario: 记录式命令缓冲（非立即模式）
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -c 'vkCmdBeginRenderPass\|vkCmdEndRenderPass\|vkCmdDraw\b' core/vulkan/VKCommandBuffer.cpp
      2. 断言输出 >= 4
    Expected Result: 真实 vkCmd* 记录调用
    Failure Indicators: 计数 < 4（未实现记录）
    Evidence: .sisyphus/evidence/task-11-recorded.txt
  ```

  **Commit**: YES
  - Message: `feat(vulkan): VKCommandBuffer — recorded N-buffered`
  - Files: `core/vulkan/VKCommandBuffer.h`, `core/vulkan/VKCommandBuffer.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc)`

- [x] 12. `VKRhi`：工厂 + acquire/submit/present 帧循环

  **What to do**:
  - 新建 `core/vulkan/VKRhi.h/.cpp`（对照 GLRhi——GL 的 beginFrame 是 no-op、endFrame 是 glfwSwapBuffers；Vulkan 要承担全部同步，教学点）
  - 类结构：
    ```cpp
    class VKRhi : public IRhi {
    public:
        bool init(GLFWwindow* window);           // 与 GLRhi 同签名（对照）
        std::unique_ptr<IBuffer> newBuffer(const BufferDesc&) override;
        std::unique_ptr<ITexture> newTexture(const TextureDesc&) override;
        std::unique_ptr<IShader> newShader(E_SHADER_TYPE, const char* source) override;
        std::unique_ptr<IShaderResourceBindings> newShaderResourceBindings() override;
        std::unique_ptr<IFramebuffer> newFramebuffer() override;
        std::unique_ptr<IGraphicsPipeline> newGraphicsPipeline() override;
        ICommandBuffer* commandBuffer() override;
        void beginFrame() override;              // acquire + 重置命令缓冲
        void endFrame() override;                // submit + present + fence
        VulkanDevice& device();
    private:
        GLFWwindow* m_window = nullptr;
        std::unique_ptr<VulkanDevice> m_device;
        std::unique_ptr<VKCommandBuffer> m_commandBuffer;
        std::vector<VkCommandBuffer> m_commandBuffers;   // N 缓冲
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        std::vector<VkFence> m_inFlightFences;           // N 缓冲
        std::vector<VkSemaphore> m_imageAvailable, m_renderFinished;
        uint32_t m_currentFrame = 0;
        uint32_t m_swapchainImageIndex = 0;
    };
    ```
  - `init(GLFWwindow*)`：
    - `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`（在 app 层窗口创建时——T13 处理；此处保证 VulkanDevice 用 `glfwCreateWindowSurface`）
    - `m_device = std::make_unique<VulkanDevice>(); m_device->init(window)`
    - 创建命令池（VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT）+ N 个命令缓冲 + N 个 in-flight fence（CREATE_SIGNALED）+ 2N 个 semaphore
    - `m_commandBuffer = std::make_unique<VKCommandBuffer>()`
  - `new*` 工厂：`std::make_unique<VKBuffer>(desc)` 等（对照 GLRhi 的 new* 实现）
  - `beginFrame()`（**教学注释：swapchain acquire 同步**）：
    - `vkWaitForFences(inFlight[currentFrame])` → `vkAcquireNextImageKHR(swapchain, UINT64_MAX, imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex)`
    - `vkResetFences` → `vkResetCommandBuffer` → `vkBeginCommandBuffer` → `m_commandBuffer->beginRecording(cmd, swapchainImageView[imageIndex] 作为 framebuffer, swapchain render pass, extent)`
    - 处理 `VK_ERROR_OUT_OF_DATE_KHR`：log + skip 本帧（Metis C2：不做重建）
  - `endFrame()`：
    - `m_commandBuffer->endRecording()` → `vkEndCommandBuffer`
    - `vkQueueSubmit(queue, 1, &submitInfo{wait=imageAvailable, signal=renderFinished}, fence)` —— **教学注释：semaphore 同步链**
    - `vkQueuePresentKHR(queue, &presentInfo{wait=renderFinished, swapchain, imageIndex})`
    - `m_currentFrame = (m_currentFrame + 1) % N`
  - `commandBuffer()` 返回 `m_commandBuffer.get()`（raw 指针，与 GL 一致）

  **Must NOT do**:
  - 不处理 swapchain resize（log + skip，C2）
  - 不做 multi-frame 复杂同步（简化 N 缓冲 + fence + 2 semaphore 即可）
  - 不写 GL

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 帧同步（fence/semaphore/acquire/present）是 Vulkan 最易错部分，涉及 GPU 执行顺序理解
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: NO（集成点）
  - **Parallel Group**: Wave 4 (sequential)
  - **Blocks**: T13
  - **Blocked By**: T11

  **References**:
  - `core/opengl/GLRhi.h/.cpp` - GL 参考（init 签名、new* 工厂模式、commandBuffer 返回 raw 指针、beginFrame/endFrame 结构）
  - `core/rhi/IRhi.h` - 接口契约
  - Vulkan Tutorial rendering: `https://vulkan-tutorial.com/Drawing_a_triangle/Drawing/Rendering_and_presentation` - fence/semaphore/acquire/present 全流程
  - Vulkan Tutorial swapchain recreation: `https://vulkan-tutorial.com/Drawing_a_triangle/Swap_chain_recreation` - 参考（本任务只 log+skip）

  **Acceptance Criteria**:
  - [ ] VKRhi.h/.cpp 存在，实现全部接口 + init() + device()
  - [ ] `grep -c 'vkAcquireNextImageKHR\|vkQueuePresentKHR\|vkQueueSubmit' core/vulkan/VKRhi.cpp` → >= 3
  - [ ] beginFrame/endFrame 完整帧循环
  - [ ] 编译通过

  **QA Scenarios**:
  ```
  Scenario: 编译通过
    Tool: Bash
    Preconditions: build-vk 已配置
    Steps:
      1. cmake --build build-vk -j$(nproc) 2>&1 | grep -E 'error' | head -10
      2. 断言无 error
    Expected Result: 编译通过
    Failure Indicators: 编译错误
    Evidence: .sisyphus/evidence/task-12-build.txt

  Scenario: 帧同步完整实现
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -c 'vkAcquireNextImageKHR\|vkQueueSubmit\|vkQueuePresentKHR\|vkWaitForFences' core/vulkan/VKRhi.cpp
      2. 断言输出 >= 4
    Expected Result: 完整 acquire/submit/present 链路
    Failure Indicators: 计数 < 4（帧循环不完整）
    Evidence: .sisyphus/evidence/task-12-frame-sync.txt
  ```

  **Commit**: YES
  - Message: `feat(vulkan): VKRhi — factory + acquire/submit/present`
  - Files: `core/vulkan/VKRhi.h`, `core/vulkan/VKRhi.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc)`

- [x] 13. `rhi_verify.cpp`：`#ifdef O5M_HAS_VULKAN` 分支 + staging buffer 像素回读

  **What to do**:
  - 修改 `main/rhi_verify.cpp`，用 `#if defined(O5M_HAS_VULKAN)` 分支选择后端（编译时决策，用户确认的单文件方案）：
    ```cpp
    #if defined(O5M_HAS_VULKAN)
    #include "VKRhi.h"
    using RhiType = VKRhi;
    #else
    #include "GLRhi.h"
    using RhiType = GLRhi;
    #endif
    ```
  - **窗口创建差异**：`#ifdef` 内 Vulkan 版加 `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`（Vulkan 不需要 GL 上下文），GL 版保持现有 4.1 core hints
  - **渲染循环差异**：
    - GL 版：现有代码保留（离屏 FBO 渲染 + 裸 GL 画窗口 + glReadPixels）
    - Vulkan 版：
      - 用 RHI 渲染到**离屏 FBO**（复用现有 RHI 代码路径，验证 VKFramebuffer 合成）
      - **呈现**：`cmd->beginPass(nullptr, colorClear, depthClear)` 渲染到 swapchain（T11 约定）→ 第二个 draw
      - **像素回读**：staging buffer + `vkCmdCopyImageToBuffer`（把 swapchain 图像或离屏 FBO 颜色附件拷贝到 host-visible staging buffer）+ fence 同步 → `vkMapMemory` 读取中心像素 → 断言（对照 GL 版 glReadPixels 的 `(128, 128, 128)` 或实际值）——**教学注释：GPU→CPU 回读的同步链**（为什么需要 fence、为什么需要 staging）
  - **Cleanup**：Vulkan 版退出前 `vkDeviceWaitIdle`（教学点：为什么退出前要等 GPU）——可在 VKRhi 析构或显式调用
  - 保留 GL 版所有现有行为不变（回归目标）
  - CMake：根 CMakeLists 的 rhi_verify 链接已由 T2 处理（条件链接 vulkan_rhi/opengl_rhi）

  **Must NOT do**:
  - 不删 GL 版代码路径（#ifdef 共存）
  - 不改变 RHI 接口
  - 不在此文件写 GL 之外的裸 API（Vulkan 回读细节封装在 T11/T12 或本文件 #ifdef 分支内的后端内部方法——**优先**：把回读逻辑做成 VKRhi 的辅助方法或 T11 的命令缓冲辅助，避免 rhi_verify 直接碰 vk API，保持"上层只用 RHI"原则。若实现困难，允许 #ifdef 分支内少量 vk 调用并注释原因）

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 双后端条件编译 + 像素回读同步，需要同时理解两条路径
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: NO（依赖全部后端完成）
  - **Parallel Group**: Wave 5 (with T14)
  - **Blocks**: T14
  - **Blocked By**: T1（ClearValue include）、T12

  **References**:
  - `main/rhi_verify.cpp`（全文 246 行）- 现有 GL 版（要加 #ifdef 的）
  - `main/main.cpp:12-14` - 现有窗口 hints（Vulkan 版需 GLFW_NO_API 的参照）
  - `core/opengl/GLRhi.h` - GL 后端 init 签名（VKRhi 同签名）
  - Vulkan Tutorial staging buffer: `https://vulkan-tutorial.com/Vertex_buffers/Staging_buffer` - vkCmdCopyBuffer 模式（回读是其镜像）
  - Vulkan Tutorial images: `https://vulkan-tutorial.com/Generating_Mipmaps` - vkCmdCopyImageToBuffer 相关

  **Acceptance Criteria**:
  - [ ] rhi_verify.cpp 含 `#if defined(O5M_HAS_VULKAN)` 分支
  - [ ] Vulkan 版：离屏 FBO 渲染 + swapchain 呈现 + staging 回读断言全部存在
  - [ ] GL 版构建 + 运行行为不变（回归）
  - [ ] Vulkan 版运行：中心像素断言 PASS，无 validation error，退出码 0

  **QA Scenarios**:
  ```
  Scenario: GL 版回归（#ifdef 不破坏现有路径）
    Tool: Bash
    Preconditions: build-gl 已构建
    Steps:
      1. ./build-gl/rhi_verify 2>&1 | tail -10; echo "EXIT=$?"
      2. 断言 EXIT=0 且输出含 pixel 验证 PASS（GL 版原有输出）
    Expected Result: GL 版行为与改动前一致
    Failure Indicators: 编译错误 / 运行崩溃 / 断言失败
    Evidence: .sisyphus/evidence/task-13-gl-regression.txt

  Scenario: Vulkan 版完整运行（核心验收）
    Tool: Bash
    Preconditions: build-vk 已构建（O5M_VULKAN=ON）
    Steps:
      1. ./build-vk/rhi_verify 2>&1 | tee .sisyphus/evidence/task-13-vk-run.txt; echo "EXIT=$?"
      2. 断言 EXIT=0
      3. 断言输出含 "PASS" 或像素断言成功信息
      4. 断言 stderr/stdout 无 "VUID-\|Validation Error\|UNASSIGNED-CoreValidation" 行（若 validation layers 可用）
    Expected Result: Vulkan 版完整跑通，像素断言 PASS，无 validation error
    Failure Indicators: 非零退出 / 断言失败 / validation error
    Evidence: .sisyphus/evidence/task-13-vk-run.txt
  ```

  **Commit**: YES
  - Message: `feat(verify): rhi_verify Vulkan branch + staging readback`
  - Files: `main/rhi_verify.cpp`
  - Pre-commit: `cmake --build build-vk -j$(nproc) && cmake --build build-gl -j$(nproc)`

- [x] 14. 教学文档 `PHASE-VULKAN.md`（遵循 AGENTS.md 输出格式）

  **What to do**:
  - 新建 `PHASE-VULKAN.md`（项目根目录，对照 AGENTS.md 的"输出格式（每Phase）"）：
    1. **前置知识**（图形学概念 + GPU 内部）：Vulkan 与 GL 的哲学差异（显式 vs 隐式状态机）、SPIR-V 是什么、command buffer 记录-提交模型、semaphore/fence 同步、render pass 必要性
    2. **架构讨论**：为什么 RHI 用这些接口（beginPass/endPass、PSO 捆绑、binding 槽位）、Vulkan 后端如何消化 4 个接口差距（ClearValue 迁移、render pass 合成、无 sampler 概念、swapchain 呈现）、替代方案（VMA vs 手写、shaderc vs 离线、运行时 vs 编译时后端选择）
    3. **文件列表**（按依赖顺序）：ClearValue.h → VulkanDevice → VKMemoryAllocator → VKBuffer/VKTexture/VKShader/VKShaderResourceBindings → VKPipeline/VKFramebuffer → VKCommandBuffer → VKRhi
    4. **关键问题回答**（从 AGENTS.md 精神延伸）：
       - Vulkan 为什么需要显式 instance/device 而 GL 不需要？
       - queue family 是什么？为什么 graphics+present 要同一族？
       - swapchain 与 GL 默认 framebuffer 的关系？
       - 为什么 Vulkan 需要 VkRenderPass 而 GL 没有？
       - 手写内存分配器教会了什么？（device-local vs host-visible）
       - 为什么回读需要 staging buffer + fence？
       - GL 4.1 vs 4.6 的 DSA 问题为何在 Vulkan 不存在？
    5. **踩坑预警**（2-3 个）：shaderc 链接失败（find_package vs shaderc_combined）、MoltenVK portability subset 缺失、validation layers 在 macOS 不可用、view port 坐标约定（GL bottom-left vs VK top-left）、fence 初始 signaled 状态
  - 记录本计划的实际实现决策 + 已知未做项（resize/MSAA/mipmap/descriptor 池回收——未来 Phase 方向）

  **Must NOT do**:
  - 不写虚构内容——只记录实际实现的技术细节（T14 在 T13 后执行，此时可引用真实代码）
  - 不修改 AGENTS.md

  **Recommended Agent Profile**:
  - **Category**: `writing`
    - Reason: 教学文档写作，需把技术决策转化为学习叙事
  - **Skills**: `[]`
  - **Skills Evaluated but Omitted**: 无

  **Parallelization**:
  - **Can Run In Parallel**: NO（需要 T13 完成后的真实实现细节）
  - **Parallel Group**: Wave 5 (with T13 — 实际在 T13 后)
  - **Blocks**: None
  - **Blocked By**: T13

  **References**:
  - `AGENTS.md` - 输出格式模板（前置知识/架构讨论/文件列表/逐文件实现/关键问题/踩坑预警）
  - `core/vulkan/*.cpp` - 实际实现（T1-T13 完成后引用）
  - `.sisyphus/plans/rhi-vulkan-backend.md` - 本计划（决策记录来源）

  **Acceptance Criteria**:
  - [ ] PHASE-VULKAN.md 存在，含全部 6 个章节
  - [ ] 关键问题章节 >= 7 个问题及回答
  - [ ] 踩坑预警 >= 2 个（基于 T1-T13 实际遇到的坑）

  **QA Scenarios**:
  ```
  Scenario: 文档结构完整
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -c '^## ' PHASE-VULKAN.md
      2. 断言输出 >= 6（前置知识/架构讨论/文件列表/关键问题/踩坑预警等章节）
    Expected Result: 文档含全部 6 个章节
    Failure Indicators: 章节数 < 6
    Evidence: .sisyphus/evidence/task-14-doc-sections.txt

  Scenario: 踩坑预警非空
    Tool: Bash
    Preconditions: 无
    Steps:
      1. grep -c '踩坑\|pitfall\|坑' PHASE-VULKAN.md
      2. 断言输出 >= 2
    Expected Result: 至少 2 个踩坑预警
    Failure Indicators: 计数 < 2
    Evidence: .sisyphus/evidence/task-14-pitfalls.txt
  ```

  **Commit**: YES
  - Message: `docs(vulkan): Phase document — concepts, Q&A, pitfalls`
  - Files: `PHASE-VULKAN.md`
  - Pre-commit: 无（纯文档）

---

## Final Verification Wave (MANDATORY — after ALL implementation tasks)

> 4 个审查代理并行。全部 APPROVE 后向用户呈现综合结果，**必须获得用户明确确认**后才算完成。

- [x] F1. **Plan Compliance Audit** — `oracle`
  逐条核对 Must Have / Must NOT Have（grep `vk[A-Z]`/`Vk[A-Z]` 验证 G2、grep `gl[A-Z]` 验证 G1、检查无 VMA/无 assert(false) 桩）。核对 evidence 文件存在。
  Output: `Must Have [N/N] | Must NOT Have [N/N] | Tasks [N/N] | VERDICT`

- [x] F2. **Code Quality Review** — `unspecified-high`
  构建通过（O5M_VULKAN=ON + 默认）。检查：VkResult 全部检查、无裸指针泄漏（Vk* 对象全部 vkDestroy）、无 `#ifndef` 混用、教学注释存在。
  Output: `Build [PASS/FAIL] | Files [N clean/N issues] | VERDICT`

- [x] F3. **Real Manual QA** — `unspecified-high`
  从干净状态：默认构建运行 GL 版（回归）、O5M_VULKAN=ON 构建运行 VK 版。执行每个任务的 QA 场景，检查像素断言输出 + 无 validation error。存 `.sisyphus/evidence/final-qa/`。
  Output: `Scenarios [N/N pass] | VERDICT`

- [x] F4. **Scope Fidelity Check** — `deep`
  逐任务核对 diff：spec 内全部实现、spec 外零实现（无 VMA、无 resize、无 MSAA 等 creep）。检查跨任务文件污染。
  Output: `Tasks [N/N compliant] | Contamination [CLEAN/N issues] | VERDICT`

---

## Commit Strategy

- **T1**: `refactor(rhi): move ClearValue into core/rhi/` - ClearValue.h, GLCommandBuffer.h, rhi_verify.cpp
- **T2**: `build(vulkan): wire shaderc + activate vulkan_rhi target` - CMakeLists.txt, core/CMakeLists.txt
- **T3**: `feat(vulkan): VulkanDevice singleton — instance/device/queue/swapchain` - VulkanDevice.h/.cpp
- **T4**: `feat(vulkan): hand-written VkDeviceMemory allocator` - VKMemoryAllocator.h/.cpp
- **T5**: `feat(vulkan): VKBuffer — staging + device-local memory` - VKBuffer.h/.cpp
- **T6**: `feat(vulkan): VKTexture — image + view + format mapping` - VKTexture.h/.cpp
- **T7**: `feat(vulkan): VKShader — shaderc GLSL→SPIR-V compile` - VKShader.h/.cpp
- **T8**: `feat(vulkan): VKShaderResourceBindings — descriptor set minimal` - VKShaderResourceBindings.h/.cpp
- **T9**: `feat(vulkan): VKPipeline — state to VkGraphicsPipelineCreateInfo` - VKPipeline.h/.cpp
- **T10**: `feat(vulkan): VKFramebuffer + render pass synthesis` - VKFramebuffer.h/.cpp
- **T11**: `feat(vulkan): VKCommandBuffer — recorded N-buffered` - VKCommandBuffer.h/.cpp
- **T12**: `feat(vulkan): VKRhi — factory + acquire/submit/present` - VKRhi.h/.cpp
- **T13**: `feat(verify): rhi_verify Vulkan branch + staging readback` - rhi_verify.cpp
- **T14**: `docs(vulkan): Phase document — concepts, Q&A, pitfalls` - PHASE-VULKAN.md

---

## Success Criteria

### Verification Commands
```bash
# GL 回归
cmake -S . -B build-gl && cmake --build build-gl -j$(nproc)
./build-gl/rhi_verify   # Expected: exit 0, center pixel PASS

# Vulkan
cmake -S . -B build-vk -DO5M_VULKAN=ON && cmake --build build-vk -j$(nproc)
./build-vk/rhi_verify   # Expected: exit 0, center pixel PASS, no validation errors

# 架构约束
grep -rn 'vk[A-Z]\|Vk[A-Z]' --include='*.cpp' --include='*.h' core/opengl main/   # Expected: 仅 #ifdef 分支内
grep -rn 'gl[A-Z]' --include='*.cpp' --include='*.h' core/vulkan/                # Expected: 0 matches
grep -rn 'VMA\|vma' core/vulkan/                                                # Expected: 0 matches
```

### Final Checklist
- [ ] 所有 Must Have 存在
- [ ] 所有 Must NOT Have 不存在
- [ ] GL 版 rhi_verify 回归通过
- [ ] Vulkan 版 rhi_verify 像素断言通过 + 无 validation error
- [ ] 教学文档完成（前置知识/架构讨论/关键问题/踩坑预警）
