# Phase V: Vulkan 后端（RHI 的镜像实现）

> 本 Phase 是 RHI 抽象的教学终点站。Phase 1 到 Phase 4 你一直活在 OpenGL 的"司机帮你开车"模型里；这一章我们把引擎切到 Vulkan，逼你亲手握住方向盘。所有代码在 `core/vulkan/` 下，与 `core/opengl/` 的 GL 后端共享同一套 `core/rhi/` 接口，编译时通过 `O5M_HAS_VULKAN` 二选一。
>
> 阅读前提：你已经理解 Phase 1 的 RHI 接口（IBuffer/IShader/IGraphicsPipeline/ICommandBuffer）和 Phase 6 的 framebuffer 概念。本 Phase 不教 Vulkan 的每一个 API，只讲**为什么**这些概念存在，以及 RHI 接口是如何消化 GL 与 Vulkan 之间哲学差异的。

---

## 1. 前置知识（图形学概念 + GPU 内部）

### 1.1 显式状态机 vs 隐式全局状态

OpenGL 是一个**隐式全局状态机**。你调 `glBindBuffer`、`glEnable(GL_DEPTH_TEST)`、`glUseProgram`，改的都是一个藏在驱动里的全局 context。下一次 draw 读到什么状态，取决于你上一次改了哪里。这个模型好写，但有两个致命问题：

1. **驱动要猜**。GPU 不知道你要干嘛，driver 只能等你 draw 的时候检查当前所有状态，临时拼出一次有效的渲染配置。状态切得越频繁，driver 的验证成本越高。
2. **无法预编译**。因为状态是运行时散装的，driver 没法提前把"渲染管线"编译成 GPU 原生代码。

Vulkan 把这一切翻过来：**没有任何全局状态**。所有东西都是你显式创建的对象：`VkInstance`（驱动连接）、`VkDevice`（逻辑设备）、`VkPipeline`（烘焙好的全部渲染状态）、`VkCommandBuffer`（记录好的指令序列）。一次创建，反复使用，driver 可以提前把管线编译好。代价是：每个对象都要你亲手创建、亲手销毁、亲手同步。

这就是本 Phase 的核心张力：`core/rhi/` 的接口是按 GL 的习惯设计的（`beginPass/endPass`、散装的 `setRasterizerState`），Vulkan 后端必须把这些接口调用翻译成显式的对象创建。

### 1.2 SPIR-V：GPU 的字节码

GLSL 源码在 GL 里是被**驱动即时编译**（JIT）的：每次 `glCompileShader` 时每个厂商的编译器各编译各的，同一个 shader 在 N 卡和 A 卡上产生的 GPU 指令完全不同。

SPIR-V 是 Khronos 定义的一种**中间字节码**，相当于 LLVM IR 在图形世界的对应物。Vulkan 规定：shader 必须以 SPIR-V 形式提交给驱动（`vkCreateShaderModule`）。编译过程被前置到你的引擎里，用一个叫做 **shaderc** 的工具完成：GLSL → SPIR-V → `VkShaderModule`。

本项目的 `VKShader::compile()`（`core/vulkan/VKShader.cpp`）就是这个管线：

```
GLSL 源码 → shaderc（glslang 内核）→ SPIR-V 字节码 → VkShaderModule
```

好处：驱动拿到的是一份标准化的字节码，跨厂商行为一致；坏处：你的引擎要自己做 shaderc 的编译期集成（或离线的 glslangValidator）。GL 里 `link()` 是独立的步骤，Vulkan 里没有"链接"这一步，多个 stage 的 SPIR-V 模块在创建管线时被 `VkPipelineShaderStageCreateInfo` 数组打包在一起，这就是为什么 `VKShader::link()` 是一个 `return true` 的 no-op。

### 1.3 Command Buffer 记录-提交模型

GL 是**立即模式**：每一个 `glDrawArrays` 都是自包含的，driver 当场验证、当场打包、提交给 GPU。没有"开始记录 / 结束记录"的概念。

Vulkan 是**记录模式**（recorded mode），见 `core/vulkan/VKCommandBuffer.h` 头部的注释：

```
vkBeginCommandBuffer → 记录一堆 vkCmd* → vkEndCommandBuffer → vkQueueSubmit
```

GPU 什么时候执行这份命令缓冲完全由调度器决定，可能是几帧之后。这个模型带来四个关键收益：

1. **多线程记录**：每个工作线程用自己的 command pool 并行记录，GL 的全局 context 天生单线程。
2. **复用**：静态几何（比如天空盒）的命令缓冲可以记录一次、反复提交；GL 每帧都要重发一遍所有调用。
3. **批量验证**：把 1000 个 draw 打包进一次 submit，driver 只验证一次状态，而不是每个 draw 验证一次。
4. **显式同步**：fence/semaphore 告诉你命令缓冲何时执行完，不用 `glFinish()` 猜。

代价是你要管理一个**环形缓冲**：N = swapchain 图像数（通常 2 到 3），GPU 执行第 1 份时 CPU 记录第 2 份，`VKRhi` 里的 `m_currentFrame = (m_currentFrame + 1) % N` 就是这个环。

### 1.4 Semaphore（GPU→GPU）vs Fence（GPU→CPU）

Vulkan 有两种同步原语，方向完全不同，见 `core/vulkan/VKRhi.cpp` 的注释：

- **Fence**：GPU → CPU 方向。CPU 等 GPU 完成执行。`vkWaitForFences` 会阻塞 CPU。一帧的 command buffer 在执行的时候 GPU 还在读它，CPU 必须等它用完才能重新记录，所以 `beginFrame()` 开头要先 `vkWaitForFences` 再 `vkResetFences`。
- **Semaphore**：GPU → GPU 方向。两个队列操作之间的握手，CPU 全程不参与。`imageAvailable` 表示"swapchain 图像可以开始渲染了"，`renderFinished` 表示"渲染完了可以呈现了"。

为什么需要两个 semaphore？因为提交链是：

```
acquire → [wait: imageAvailable] → 记录的命令 → [signal: renderFinished] → present
```

semaphore 比 fence 快，因为不经过 CPU。还有一个细节：`pWaitDstStageMask` 指定 GPU 在**哪个管线阶段**等 semaphore，这里是 `COLOR_ATTACHMENT_OUTPUT_BIT`，意思是"等到图像可用再开始写颜色附件"，而不是从一开始就空等。

### 1.5 Render Pass 为什么必要：tile-based GPU

这是整个 Vulkan 设计里最"反直觉"也最重要的一块。**手机 GPU（Apple Silicon、ARM Mali、Qualcomm Adreno）是 tile-based 的**：它们把 framebuffer 切成一小块一小块（tile），每一块在**片上高速缓存**（tile memory）里完整渲染完，再一次性写回显存。片上内存比显存带宽高一个数量级，所以省显存读写 = 省电 = 更快。

问题在于：驱动怎么知道哪些附件可以留在片上、哪些必须写回？GL 的办法是**猜**：它观察你绑了什么、画了什么，用启发式推断。Vulkan 的办法是**逼你声明**：`VkRenderPass` 的 `loadOp` 和 `storeOp` 直接告诉驱动答案。

```
loadOp = CLEAR   → 附件内容反正要清掉，不用从显存读，直接在 tile 里写
loadOp = LOAD    → 需要保留上次内容，必须读回 tile
storeOp = STORE  → 渲染完写回显存（比如要呈现的 swapchain 图像）
storeOp = DONT_CARE → 用完就扔，留在片上（比如临时深度缓冲）
```

`core/vulkan/VKFramebuffer.h` 的头部注释把这个讲得很透：为什么 Vulkan 把 framebuffer 拆成 `VkRenderPass`（元数据：格式、load/store、layout 转换）和 `VkFramebuffer`（具体的 VkImageView 集合）两个对象，而 GL 一个 `glCreateFramebuffers` 就全干了。答案就是 tile memory 优化：渲染意图声明得越清楚，驱动越不需要猜。

---

## 2. 架构讨论（为什么这样设计？替代方案？）

### 2.1 为什么 RHI 接口天然贴合 Vulkan

回头看 Phase 1 的 RHI 设计，你会发现它的形状是照着 Vulkan 的概念画的，只是当时我们不知道：

| RHI 接口 | GL 端实现 | Vulkan 端实现 | 映射关系 |
|---|---|---|---|
| `beginPass(fb, clear)` | `glBindFramebuffer` + `glClear*` | `vkCmdBeginRenderPass` | 显式 render pass，GL 是隐式的 |
| `IGraphicsPipeline`（set* + create） | 存状态，draw 时逐项 `glEnable/glDepthFunc` | `VkGraphicsPipelineCreateInfo` 一次性烘焙 | 散装状态 vs PSO |
| `IShaderResourceBindings::bind*(binding, ...)` | `glBindBufferRange` / `glBindTextureUnit` | `VkDescriptorSet` + `vkUpdateDescriptorSets` | 槽位概念本来就和 descriptor binding 同构 |
| `setVertexInput(bindingSlot, ...)` | VAO 间接寻址 | `vkCmdBindVertexBuffers` 直接传数组 | 槽位编号就是 VkVertexInputBindingDescription |

这就是为什么 GL 端 `IShaderResourceBindings::create()` 可以是 `return true` 的 no-op（GL 在 draw 时现场绑定），而 Vulkan 端必须真的建 descriptor set：**接口没有撒谎，是 GL 偷懒了**。

### 2.2 Vulkan 后端如何消化接口差距

接口是按 GL 习惯设计的，Vulkan 后端要处理五个"接口缺省值"：

1. **ClearValue 迁移**：`ClearValue` 原本定义在 `core/opengl/GLCommandBuffer.h` 里，是个接口泄漏。Vulkan 端要调 `beginPass` 就得 include 一个 GL 头。修复：迁到 `core/rhi/ClearValue.h`，并把 `GLfloat/GLint` 改成 `float/int`。教学重点：`(float[4], float, int)` 的内存布局和 `VkClearValue` 这个 union 天然兼容，`VKCommandBuffer::toVkClearValue()` 可以直接 `memcpy` 颜色数组，零转换成本。`active == false` 对应 Vulkan 的 `loadOp = LOAD`，GL 端跳过 `glClearBuffer*`，语义一致。
2. **Render pass 合成**：接口的 `IFramebuffer::create()` 不知道 attachment 的格式和用途，`VKFramebuffer::create()` 要从 color/depth 纹理里收集格式，合成 `VkAttachmentDescription` → `VkSubpassDescription` → `VkRenderPass` → `VkFramebuffer` 四层结构。这是 GL 的 `glNamedFramebufferTexture` 在 Vulkan 里的展开版。
3. **默认 sampler**：接口没有 sampler 概念（GL 的采样状态活在纹理对象里），`VulkanDevice` 维护一个全局默认 `VkSampler`（LINEAR + REPEAT），`VKShaderResourceBindings` 绑定纹理时带上它。
4. **Swapchain 呈现**：接口的 `endFrame()` 背后，GL 是 `glfwSwapBuffers`，Vulkan 是 `vkQueuePresentKHR`。呈现路径（图像获取、提交、呈现）全部藏在 `VKRhi::beginFrame/endFrame` 里，上层代码感知不到。
5. **延迟管线创建**：`VKPipeline::create()` 没有参数，但 Vulkan 管线必须带 `VkRenderPass`。解法：`VKCommandBuffer::setGraphicPipeline()` 里判断 `isValid()`，为 false 时用**当前 render pass + extent** 懒创建（`create(rp, extent)`），并缓存。注释里写明：真实引擎会在资源加载时预烘焙，这里为了对齐 GL 的"draw 前设状态"习惯而懒创建。

### 2.3 替代方案

**VMA vs 手写分配器**。Vulkan 里 `vkAllocateMemory` 是驱动级昂贵调用，真实引擎都用 VMA（VulkanMemoryAllocator）做 sub-allocation：从大块内存里切成小块给资源用，减少调用次数、防碎片。本项目故意手写了一个"一个资源一块 VkDeviceMemory"的朴素分配器（`core/vulkan/VKMemoryAllocator.h`），因为**只有亲手撞上碎片化，才懂 VMA 在解决什么问题**。教学上这叫"先把烂的做法做出来"。

**shaderc 运行时编译 vs 离线 SPIR-V**。本项目用 shaderc 在引擎启动时把 GLSL 编译成 SPIR-V。真实引擎通常用离线工具（glslangValidator）把 `.vert/.frag` 预编译成 `.spv` 文件随包分发，启动时直接 `vkCreateShaderModule`，省掉 shaderc 依赖和编译时间。选择运行时编译是为了让 RHI 接口零改动（接口传的是源码字符串），并且和 GL 后端的 `compile()` 语义对齐。

**编译时 vs 运行时后端选择**。本项目用编译时 `#ifdef O5M_HAS_VULKAN`：一次编译只带一个后端。真实引擎（或 The-Forge 这类框架）会用运行时工厂 + 动态加载，同一份二进制按需加载 GL/Vulkan/Metal 后端。编译时方案简单、类型安全，代价是调试时要切换构建目录。

---

## 3. 文件列表（按依赖顺序）

```
core/rhi/ClearValue.h                     ：接口层修复：ClearValue 从 GL 头迁出，与 VkClearValue 内存布局兼容
core/vulkan/VulkanDevice.h/.cpp           ：后端心脏：VkInstance → Device → Queue → Surface → Swapchain 全链路单例
core/vulkan/VKMemoryAllocator.h/.cpp      ：手写分配器：一资源一 VkDeviceMemory，alignUp，无池化
core/vulkan/VKBuffer.h/.cpp               ：IBuffer：usage 位掩码映射 + staging 上传 + 持久映射
core/vulkan/VKTexture.h/.cpp              ：ITexture：VkImage + VkImageView + 7 格式映射 + layout 转换
core/vulkan/VKShader.h/.cpp               ：IShader：shaderc GLSL→SPIR-V + #version 自动升级，link() 为 no-op
core/vulkan/VKShaderResourceBindings.h/.cpp：IShaderResourceBindings：descriptor set 六步链 + 默认 sampler
core/vulkan/VKPipeline.h/.cpp             ：IGraphicsPipeline：state → VkGraphicsPipelineCreateInfo，20 格式映射
core/vulkan/VKFramebuffer.h/.cpp          ：IFramebuffer：render pass 合成 + VkFramebuffer
core/vulkan/VKCommandBuffer.h/.cpp        ：ICommandBuffer：记录式命令缓冲 + 视口 Y 翻转 + 懒建管线
core/vulkan/VKRhi.h/.cpp                  ：IRhi：工厂 + N 缓冲帧循环 + fence/semaphore 同步
main/rhi_verify.cpp                       ：双后端验证：Vulkan 分支含 staging buffer 像素回读
CMakeLists.txt / core/CMakeLists.txt      ：shaderc 查找 + vulkan_rhi 目标 + 条件链接
```

依赖顺序就是上表的顺序：`ClearValue.h` 最先（接口变更影响两个后端）→ `VulkanDevice` 提供所有原始句柄 → `VKMemoryAllocator` 依赖它的内存查询 → 四个资源类（Buffer/Texture/Shader/SRB）→ 管线两个类（Pipeline 依赖 Shader 的 stageFlag，Framebuffer 依赖 Texture）→ `VKCommandBuffer` 把它们全部串起来 → `VKRhi` 提供帧循环 → `rhi_verify` 验证。

---

## 4. 逐文件实现（代码 + 注释）

> 本 Phase 有 21 个实现文件，全部代码都在仓库里。这里不再重复贴代码，而是给出每个文件的**关键教学点**和阅读指引。读源码时优先读头文件顶部的注释块，它们就是本 Phase 的教学正文。

### `core/rhi/ClearValue.h`（接口层）
- 教学点：接口泄漏的修复。为什么 `(float[4], float, int)` 和 `VkClearValue` union 布局兼容，`active == false` 如何映射到 `loadOp = LOAD`。
- 阅读：`ClearValue.h` 顶部长注释 + `VKCommandBuffer::toVkClearValue()`（`VKCommandBuffer.cpp` 末尾）。

### `core/vulkan/VulkanDevice.h/.cpp`
- 教学点：GL 的 context 是隐式的，Vulkan 的 instance/device 是显式的（头文件注释）。macOS 为什么必须启用 `VK_KHR_portability_enumeration`（见 6.1）。validation layer 的探测与降级。`findMemoryType` 的 typeFilter 位掩码语义。
- 教学点：swapchain 就是"由应用管理的默认 framebuffer"：选 SRGB 格式、选 present mode（MAILBOX 优先 FIFO 兜底）、固定 800×600。
- 阅读：`createInstance()` → `pickPhysicalDevice()` → `createLogicalDevice()` → `createSwapchain()` → `createSwapchainRenderPass()` → `createDefaultSampler()` 的调用链。

### `core/vulkan/VKMemoryAllocator.h/.cpp`
- 教学点：device-local vs host-visible 的实物化。头文件注释直说"这个分配器是教学版的，真实引擎用 VMA"。`alignUp` 的位运算技巧和为什么对齐必须是 2 的幂。
- 阅读：`allocate()` 里 `vkGetPhysicalDeviceMemoryProperties` 的遍历逻辑。

### `core/vulkan/VKBuffer.h/.cpp`
- 教学点：GL 用 bind target 暗示缓冲用途（且能随时改），Vulkan 用 `VkBufferUsageFlags` 位掩码烘焙用途（不可变，且一个缓冲可以同时是顶点+存储缓冲）。`BufferDesc::usage` 本身就是位掩码，映射干净。
- 教学点：`TRANSFER_DST_BIT` 是经典陷阱，device-local 缓冲的所有上传都走 `vkCmdCopyBuffer`，目标缓冲缺这个 flag 会静默失败（见 6.4）。
- 教学点：STATIC → DEVICE_LOCAL（上传走 staging），DYNAMIC/STREAM → HOST_VISIBLE|HOST_COHERENT（持久映射，`upload()` 就是 memcpy）。`map()` 在 DEVICE_LOCAL 上直接 assert，因为 VRAM 根本不能 CPU 映射，GL 的 `glMapBuffer` 能成功只是驱动帮你拷了一份。
- 阅读：`submitImmediately()` 的单次提交辅助函数，它展示了"临时命令缓冲 + 等待空闲"这个通用模式。

### `core/vulkan/VKTexture.h/.cpp`
- 教学点：VkImage（像素存储）/ VkImageView（怎么解读）/ VkSampler（怎么采样）的三分离，对应 GPU 里三个不同的硬件单元（见 5.8）。
- 教学点：GL 的 internalFormat/format/type 三枚举，Vulkan 用一个 `VkFormat` 全编码。aspect mask 让深度和模板平面可独立访问。
- 教学点：layout transition 是 Vulkan 的"显式屏障"。`upload()` 的 `UNDEFINED → TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` 三段转换用 `vkCmdPipelineBarrier` 完成，它做的是缓存刷新和内存重排，不是搬像素。GL 把这些屏障全藏在驱动里。
- 阅读：`transitionLayout()` 与 `deriveUsage()`。

### `core/vulkan/VKShader.h/.cpp`
- 教学点：SPIR-V 是 GPU 字节码（见 1.2）。shaderc C++ API 的 RAII 用法。`link()` 为什么是 no-op（Vulkan 的"链接"发生在管线创建）。
- 教学点：`#version 410 → 450` 自动升级（见 6.2）。反射返回空 vector 是本 Phase 的范围裁剪（无 SPIRV-Cross），注释写明了未来接入点。
- 阅读：`compile()` 里的正则替换和 `CompileGlslToSpv` 调用。

### `core/vulkan/VKShaderResourceBindings.h/.cpp`
- 教学点：GL 的 `create()` 是 no-op（draw 时现场绑定），Vulkan 的 `create()` 是六步 GPU 对象链：`VkDescriptorSetLayoutBinding` 数组 → `VkDescriptorSetLayout` → `VkPipelineLayout` → `VkDescriptorPool` → `VkDescriptorSet` → `vkUpdateDescriptorSets`。这就是"接口没有撒谎，GL 偷懒了"的实物证据。
- 教学点：descriptor set 是 GPU 里的连续描述符表，一次绑定全部资源，对应 GPU 硬件。`vkUpdateDescriptorSets` ≈ 初始化时批量执行 `glBindBufferRange` + `glBindTextureUnit`。
- 阅读：`create()` 的六步注释，和空绑定情况下的降级路径。

### `core/vulkan/VKPipeline.h/.cpp`
- 教学点：PSO 是"烘焙后不可变"的（见 5.10）。GL 的 `glEnable/glDisable` 散装状态，Vulkan 一次 `VkGraphicsPipelineCreateInfo` 全打包。
- 教学点：`create(VkRenderPass, VkExtent2D)` 是唯一带参的 create，因为 Vulkan 管线必须知道渲染目标格式。`create()` 无参版本返回 false，`VKCommandBuffer` 在首次使用时懒调用带参版本。
- 教学点：20 个顶点格式映射表。`perInstance` 在 GL 里被忽略（divisor 默认 0），Vulkan 用 `VK_VERTEX_INPUT_RATE_INSTANCE` 真正实现它。
- 教学点：front face 补偿。Vulkan 的 NDC 是 Y 向下（左上原点），GL 是 Y 向上。为了让 GL 习惯的三角形绕序渲染正确，front face 显式设为 `VK_FRONT_FACE_CLOCKWISE`（见 6.3）。
- 教学点：viewport/scissor 声明为 dynamic state，命令缓冲在 `beginPass` 时才真正设置。GL 里所有状态都是隐式动态的，Vulkan 要求显式选。
- 阅读：`create()` 里五组 state 的映射代码（vertex input / rasterizer / depth / blend / stages）。

### `core/vulkan/VKFramebuffer.h/.cpp`
- 教学点：render pass 与 framebuffer 的二分（见 1.5）。`create()` 的六步合成：attachment 描述 → 引用数组 → subpass → subpass dependency → `vkCreateRenderPass` → `vkCreateFramebuffer`。
- 教学点：`VkSubpassDependency`（EXTERNAL → 0）是 GL 隐式 per-draw 屏障的显式化，烘焙进 render pass 让驱动可以围绕它优化。
- 教学点：所有附件尺寸必须一致，用 assert 强制（GL 的"framebuffer completeness"靠 `glCheckFramebufferStatus` 运行时查，Vulkan 让你自己创建时断言）。
- 阅读：`create()` 的附件收集与 dependency 构造。

### `core/vulkan/VKCommandBuffer.h/.cpp`
- 教学点：立即模式 vs 记录模式的完整对照（头文件顶部长注释，见 1.3）。`vkEndCommandBuffer` 之后命令缓冲不可变，直到 reset。
- 教学点：viewport 负高度翻转（见 6.3）。scissor 是绝对像素坐标，不需要翻转。
- 教学点：`beginPass(nullptr)` 约定 = 渲染到 swapchain。VKRhi 每帧把当帧的 swapchain framebuffer/render pass 通过 `beginRecording()` 注入。
- 教学点：`setGraphicPipeline` 的懒建管线策略（见 2.2 第 5 点）。
- 教学点：Vulkan 没有 VAO，`vkCmdBindVertexBuffers` 直接传 `VkBuffer[]`，比 GL 的每 draw 建 VAO 更简单。
- 阅读：`beginPass()`、`setViewport()`、`draw()`。

### `core/vulkan/VKRhi.h/.cpp`
- 教学点：`beginFrame()` 的五步：`vkWaitForFences` → `vkResetFences` → `vkAcquireNextImageKHR` → `vkResetCommandBuffer` → `vkBeginCommandBuffer`。`endFrame()` 的三步：`endRecording` → `vkQueueSubmit` → `vkQueuePresentKHR`。
- 教学点：fence/semaphore 的创建与语义（见 1.4）。`VK_FENCE_CREATE_SIGNALED_BIT` 初始状态的原因（见 6.5）。
- 教学点：`glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)` 必须在创建 surface 之前调用，否则 GLFW 可能先建一个 GL context 干扰 Vulkan surface。
- 教学点：N 缓冲环形队列 `m_currentFrame = (m_currentFrame + 1) % N`，注释解释了为什么 ring 不会踩到 GPU 正在用的资源（fence 等待保证）。
- 阅读：`init()` 和 `beginFrame()/endFrame()`。

### `main/rhi_verify.cpp`
- 教学点：同一个程序用 `#ifdef O5M_HAS_VULKAN` 跑两个后端，GL 路径零改动，证明 RHI 抽象成立。
- 教学点：Vulkan 路径每帧渲染两次：离屏 FBO（验证 `VKFramebuffer` 合成）+ swapchain（`beginPass(nullptr)`），同一个 PSO、同一个 VBO。
- 教学点：第 3 帧的 swapchain 回读。`vkDeviceWaitIdle` → staging buffer（HOST_VISIBLE）→ 临时命令缓冲 → `PRESENT_SRC → TRANSFER_SRC` 布局转换 → `vkCmdCopyImageToBuffer` → 转回 → `vkQueueSubmit` + `vkQueueWaitIdle` → `vkMapMemory` 读中心像素。这就是 GPU→CPU 回读的完整同步链（见 5.6）。
- 教学点：BGRA vs RGBA 字节序处理。swapchain 通常是 `B8G8R8A8`，中心像素的"蓝色"在 byte[0] 而不是 byte[2]。

---

## 5. 关键问题回答

### Q1: 为什么 Vulkan 需要显式的 instance/device，而 GL 不需要？

GL 的 context 由窗口系统隐式创建：`glfwCreateWindow` + `glfwMakeContextCurrent` 之后你就"有了"一个 GL。你拿到的 GPU 是窗口系统分配的，不能挑，甚至不知道是谁。driver 把所有细节包成一个黑盒，你的代码和具体 GPU 之间隔着一层翻译。

Vulkan 里一切显式：`vkCreateInstance` 建立与驱动的连接（`VkInstance`）→ `vkEnumeratePhysicalDevices` 列出系统里**所有** GPU（`VkPhysicalDevice`）→ 挑选后 `vkCreateDevice` 创建逻辑设备（`VkDevice`）→ 在设备上申请队列、分配内存、创建资源。`VulkanDevice::pickPhysicalDevice()` 甚至实现了评分算法：离散 GPU 100 分，核显 50 分，选最高的。

好处：多 GPU 环境（eGPU、异构渲染）在 Vulkan 里是天然支持的，因为枚举和选择都摆在明面上。GL 做不到，因为 driver 不让你碰选择过程。代价：启动代码多了几十行，而且每一步都可能失败，必须检查 `VkResult`。

### Q2: Queue family 是什么？为什么 graphics + present 必须同一族？

GPU 的执行单元不是一个大水桶，而是分成若干**队列族**（queue family），每个族有独立的执行能力：有的只做图形、有的只做计算、有的只做传输（DMA）。`VkQueueFamilyProperties::queueFlags` 告诉你每个族能干什么。本项目只找第一个带 `VK_QUEUE_GRAPHICS_BIT` 的族，把渲染、上传、呈现全塞进去。

为什么 graphics 和 present 要同一族？因为交换链图像的呈现（`vkQueuePresentKHR`）和渲染（`vkQueueSubmit`）需要按顺序在**同一条队列**上执行，否则要用队列族间共享 + 额外同步。`VulkanDevice::createLogicalDevice()` 里只申请了一个族，注释写明"真实引擎会用独立 transfer/compute 队列做异步上传"，那是后续优化，不是本 Phase 范围。

### Q3: Swapchain 和 GL 的默认 framebuffer 是什么关系？

swapchain 就是 Vulkan 版的默认 framebuffer。GL 里 `glfwSwapBuffers` 背后是窗口系统管理的一组后备缓冲，你从不直接碰它们，也不知道有几张、什么格式。Vulkan 把这一切**交还给你**：`VkSwapchainKHR` 是一组 `VkImage` 的环形缓冲，你在 `createSwapchain()` 里自己选格式（SRGB 优先）、图像数（min+1 起）、present mode（FIFO = 垂直同步，MAILBOX = 三缓冲无撕裂）。`vkAcquireNextImageKHR` 告诉你下一张能画的图像索引，`vkQueuePresentKHR` 把画完的图像交还给显示引擎。

更关键的区别：GL 的默认 framebuffer 格式由窗口系统定死，你只能用 `glViewport` 适配；Vulkan 里 swapchain 的 `VkRenderPass` 和逐图像 `VkFramebuffer` 都是你自己建的（`createSwapchainRenderPass()`），渲染目标和呈现格式**由应用声明**。这给了你 vsync、HDR、延迟的完全控制权，GL 的 swap interval 只是个粗糙的开关。

### Q4: 为什么 Vulkan 需要 VkRenderPass 而 GL 没有？

因为 tile-based GPU 需要知道附件的**使用意图**来优化片上内存调度（见 1.5）。GL 的驱动只能从 `glDraw*` 序列里猜：这个附件下次会不会被读？渲染完要不要写回？猜错就是性能损失，而猜的成本由所有 GL 应用平摊。

Vulkan 把 `VkRenderPass` 变成一等对象：`loadOp/storeOp`、layout 转换、subpass 依赖全部声明式描述。驱动拿到它就知道：这张附件渲染期间可以留在 tile memory，`storeOp = DONT_CARE` 的深度缓冲甚至永远不用写回显存。这就是为什么同为移动 GPU 的后端，Vulkan 能比 GLES 更省带宽。本项目里 `VKFramebuffer::create()` 合成 render pass，`VKCommandBuffer::beginPass()` 提交它，swapchain 渲染用的默认 render pass 在 `createSwapchainRenderPass()` 里定义。

### Q5: 手写内存分配器教会了什么？（device-local vs host-visible）

`VKMemoryAllocator` 故意做得朴素：一个资源一块 `VkDeviceMemory`。这逼你直面 Vulkan 的内存模型：`vkGetPhysicalDeviceMemoryProperties` 返回若干 heap（显存堆、系统内存堆）和若干 memory type（heap 内带属性组合的变体）。选型要同时满足两件事：资源自己的 `VkMemoryRequirements::memoryTypeBits`（哪些 type 物理上允许）和你要的属性：

- `DEVICE_LOCAL`：GPU 独享显存，最快，但 CPU 不能映射。静态几何放这里，上传走 staging 中转。
- `HOST_VISIBLE | HOST_COHERENT`：CPU 可映射，coherent 表示免手动 flush。动态数据（每帧改的 UBO）放这里，持久映射，`upload()` 就是 memcpy。
- `HOST_VISIBLE`（无 coherent）：映射后要手动 `vkFlushMappedMemoryRanges`，`unmap()` 在 coherent 下是 no-op 就是这个原因。

GL 的 `STATIC/DYNAMIC/STREAM` 只是 driver 的**建议**，放哪里 driver 说了算；Vulkan 把决策权交给你，代价是你必须懂这两类内存。等以后遇到"为什么动态缓冲上传很慢"，答案就在这一节。

### Q6: 为什么回读需要 staging buffer + fence？（GPU→CPU 同步链）

CPU 读 GPU 渲染结果，是一条完整的同步链（`main/rhi_verify.cpp` 第 3 帧）。GPU 的结果在显存里，CPU 的指针够不着显存，所以要一块 HOST_VISIBLE 的 staging buffer 当中转。但显存内容进不了 staging，得靠 GPU 自己拷：`vkCmdCopyImageToBuffer`。

光拷还不够，时序问题更大。CPU 不能知道 GPU 什么时候拷完，所以：

1. `vkDeviceWaitIdle`：暴力等所有 GPU 工作完成（教学场景可以，真实引擎要精确到资源级别）。
2. 布局转换：swapchain 图像现在的 layout 是 `PRESENT_SRC_KHR`（呈现专用），要转成 `TRANSFER_SRC_OPTIMAL` 才能被拷贝，用 `VkImageMemoryBarrier` 声明依赖。
3. 提交拷贝命令后 `vkQueueWaitIdle`：等拷贝队列清空。
4. 只有这时 `vkMapMemory` 才是安全的，读到的字节才是渲染结果。

每一步都在回答同一个问题：**"你怎么知道上一步完成了？"** GL 的 `glReadPixels` 把这条链藏在 driver 里，Vulkan 让你亲手搭。这就是"回读需要 staging buffer + fence"的原因：staging 解决"显存够不着"，同步解决"时序不知道"。

### Q7: GL 4.1 vs 4.6 的 DSA 问题，为什么在 Vulkan 里不存在？

AGENTS.md 强制 DSA（`glCreateTextures`、`glNamedBufferData`），但 macOS 的 GL 上限是 4.1，而 DSA 是 4.5 才有的功能。GL 后端在 macOS 上可能跑不起来：要么用老的 bind-then-modify 风格，要么干脆无法编译。这是 GL 生态的版本碎片化问题：同样的 API 在不同平台能力不同。

Vulkan 从一开始就没有 bind-state 模型，也就不存在"DSA vs 非 DSA"的区分。`vkCreateTexture` 时代的一切都是"对象直建直改"，`VKTexture`、`VKBuffer` 的所有函数都直接对句柄操作，天然就是 DSA 风格。Vulkan 的版本是 **能力协商**而不是特性碎片：`vkCreateInstance` 时声明 API 版本（本项目用 1.2），功能不足就用扩展查询发现，不存在"同一个调用在某平台不支持"的隐性差异。这就是选择 Vulkan 后端绕开 GL 平台陷阱的根本动机之一，代码里的注释也写明了这一点。

### Q8: VkImage / VkImageView / VkSampler 的分离为什么合理？

GL 的一个"纹理"把三件事绑死在一个 `GLuint` 里：像素存储、采样状态、mip 链。`VKTexture.h` 头注释讲得很清楚，GPU 硬件层面这三件事本来就是三个独立单元：

- **VkImage**：裸像素存储，只管分辨率、格式、层数。对应 `glTexStorage2D`。
- **VkImageView**：怎么解读这张图，选哪个 aspect（颜色还是深度）、哪个 mip 范围、哪一层。shader 描述符和 framebuffer 附件都绑 view 而不是 image。对应 GL 的 target + swizzle 的组合，但更精细。
- **VkSampler**：怎么采样，滤波、环绕、各向异性。因为**同一张图可以在不同 pass 里被不同 filter 采样**：深度图在阴影 pass 里要 `COMPARE_OP_LESS`，在调试 pass 里要线性滤波，GL 里你得复制两份纹理或者接受状态切换。

这个分离正好是 GPU 硬件的真实结构：采样器单元和纹理读取单元是分开的电路。Vulkan 选择暴露现实，GL 选择隐藏。本项目里 `VKTexture` 管理 image + view，`VulkanDevice::defaultSampler()` 提供一个全局默认 sampler，接口层无感知。

### Q9: 命令缓冲记录-提交模型 vs GL 立即模式，差异在哪？

差异不在"能不能画"，在**谁掌握执行时机**。GL 里每调一个 `glDraw*`，driver 立刻验证、立刻提交，CPU 和 GPU 被绑死在同一个节奏上，driver 没有机会做批量优化。Vulkan 里你先 `vkBeginCommandBuffer` 记录几百个 `vkCmd*`，`vkEndCommandBuffer` 固化，再一次性 `vkQueueSubmit`，GPU 想什么时候跑就什么时候跑。

收益：多线程并行记录（每个线程一个 pool）、静态命令缓冲复用、驱动只验证一次。代价：你要自己管 N 缓冲环和 fence，还要接受"我调了 `vkCmdDraw` 但它可能几帧后才执行"的心智模型。`VKCommandBuffer` 头注释把四点收益列全了，`VKRhi::beginFrame` 里 `vkResetCommandBuffer` 的注释解释了为什么每帧要重置（ONE_TIME_SUBMIT 允许驱动不做缓存优化）。

### Q10: 为什么 Vulkan PSO 是"烘焙后不可变"的？

因为可变的散装状态无法预编译。GL 的 `glEnable(GL_DEPTH_TEST)` + `glDepthFunc` + `glBlendFunc` + `glCullFace` 都是运行时改全局状态，driver 只能在 draw 时把所有当前状态临时组合、临时编译。改一个开关就破坏一次缓存，所以 GL 应用都学"状态排序"来减少切换。

Vulkan 的 `VkPipeline` 是一个 PSO：创建时把所有状态（shader 模块、顶点格式、光栅化、深度模板、混合、render pass、dynamic state 声明）全部烘焙进 `VkGraphicsPipelineCreateInfo`，driver 一次性编译成 GPU 原生 blob。之后**任何状态都不能改**，换 FILL 到 LINE 都要新建一条管线。这就是为什么 `VKPipeline::setRasterizerState()` 等 setter 只是存副本，真正的魔法发生在 `create()`。好处是 draw 时一次绑定就绪，坏处是状态组合爆炸，真实引擎要用管线缓存 + 状态哈希复用。本项目用 `isValid()` + 懒创建来规避"接口创建时不知道 render pass"的问题，注释里写明了这个 trade-off。

---

## 6. 踩坑预警（来自本 Phase 的真实实现）

### 6.1 MoltenVK portability subset 缺失 → 0 个物理设备

**症状**：`vkEnumeratePhysicalDevices` 返回 0 个 GPU，程序断言失败退出。

**原因**：macOS 上的 Vulkan 是 MoltenVK（Vulkan over Metal 的翻译层）提供的，而 Metal 的 API 表面和 Vulkan 不完全重合。Khronos 用 `VK_KHR_portability_subset` 扩展标注"这些功能翻译层没实现"。如果 instance 创建时没有同时开 `VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME` + `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`，**MoltenVK 直接不枚举任何设备**，因为那些设备"不完整"。

**修复**：`VulkanDevice::createInstance()` 里在 `__APPLE__` 下把 portability enumeration 扩展加进 `glfwGetRequiredInstanceExtensions` 的列表，并设置 instance flag；设备端再启用 `VK_KHR_portability_subset`。`pickPhysicalDevice()` 里还要检查每个设备有没有这个扩展，没有就跳过。

**教训**：这是"Vulkan 跨平台"的第一课。桌面 Vulkan 是"所有设备功能一致"的幻想，Mac 一上来就打破它。写 Vulkan 代码第一步永远是查扩展，不能假设。

### 6.2 shaderc 编译 `#version 410 core` 直接报错

**症状**：GL 后端正常编译的 shader，`VKShader::compile()` 返回失败，`compileLog` 是 glslang 的一堆 `ERROR: ... version '410' is not supported`。

**原因**：Vulkan 的 GLSL→SPIR-V 编译器（glslang）要求 **GLSL 450 语义**（显式 location 等），`#version 410` 根本进不了 Vulkan 目标。本项目 shader 源码是 Phase 1 写的 `#version 410 core`，GL 驱动能编译，Vulkan 不能。

**修复**：`VKShader::compile()` 在编译前用 `std::regex` 扫描 `#version XXX`，如果版本号小于 450 就原地替换成 `#version 450`。`layout(location=...)` 语法在 450 下完全兼容，所以替换后源码不动一行。这个技巧让同一份 shader 源码同时服务两个后端。

**教训**：别把 GLSL 版本号当摆设。Vulkan 目标是 450 起步，你给用户的接口传的是源码字符串，就要在编译入口处理版本差异，而不是要求用户改 shader。

### 6.3 viewport 坐标约定：GL 左下原点 vs Vulkan 左上原点

**症状**：三角形渲染出来了，但是**上下颠倒**，或者开启背面剔除后整个物体消失。

**原因**：GL 的 NDC 是 Y 向上，viewport 原点在左下；Vulkan 的 NDC 是 Y 向下，viewport 原点在左上。为 GL 写的 shader 输出 `gl_Position` 时假设 Y+1 是屏幕顶部，到了 Vulkan 里 Y+1 被映射到屏幕底部。连带效应：绕序（winding）对调，BACK 面剔除会剔除掉原本正面朝外的三角形。

**修复**：`VKCommandBuffer::setViewport()` 用**负高度技巧**：

```cpp
VkViewport viewport{};
viewport.x      = x;
viewport.y      = y + h;    // 偏移到 GL 的"顶部"
viewport.height = -h;       // 负高度翻转 Y 方向
```

把 viewport 的 Y 翻转后，GL 习惯的 clip 空间不用动矩阵就渲染正确。配套地，`VKPipeline` 把 front face 设成 `VK_FRONT_FACE_CLOCKWISE` 补偿绕序差异。scissor 是绝对像素坐标，不需要翻转，这是最容易漏的地方。

**教训**：坐标系约定不是"渲染细节"，是管线级决定。要么翻转 viewport（本项目），要么翻转投影矩阵，但后者会连锁改动绕序、剔除、模板，工程上几乎总是选前者。先想清楚再动手，别靠试错。

### 6.4 fence 初始状态必须是 SIGNALED

**症状**：程序启动后第一帧就卡死在 `vkWaitForFences`，窗口永远不出现，CPU 占用 100%。

**原因**：fence 默认是 **unsignaled**。`beginFrame()` 第一帧就 `vkWaitForFences` 等待一个从未被提交过的 fence，它永远不会变成 signaled，于是无限阻塞。

**修复**：创建 fence 时带 `VK_FENCE_CREATE_SIGNALED_BIT`，让它"初始就是完成态"，第一帧的等待立即通过。`VKRhi::init()` 里有明确的注释。

**教训**：Vulkan 的同步对象都有初始状态语义，fence 的默认态是"未完成"，和直觉相反。读 `VkFenceCreateInfo` 的文档时第一件事就是确认默认态。这类 bug 的表现是"卡死"，定位时先怀疑同步初始化。

### 6.5 VkPipeline 需要 VkRenderPass（管线与 framebuffer 耦合）

**症状**：`vkCreateGraphicsPipelines` 返回 `VK_ERROR_INCOMPATIBLE_RENDER_PASS` 或者干脆 `VK_NULL_HANDLE`，GL 后端同样的代码却正常。

**原因**：Vulkan 管线创建必须带 `VkRenderPass`，因为管线的输出格式要和 render pass 的 attachment 格式匹配。GL 的 `glProgram` 不知道 framebuffer 长什么样，写哪都行；Vulkan 里同一个 shader 集针对不同 render pass 是不同的管线。而 RHI 接口的 `IGraphicsPipeline::create()` 恰好没有参数。

**修复**：`VKPipeline::create()` 无参版本返回 false，真正的实现是 `create(VkRenderPass, VkExtent2D)`。`VKCommandBuffer::setGraphicPipeline()` 在 `isValid() == false` 时用**当前 render pass + extent** 懒创建并缓存（注释写明：真实引擎在资源加载时预烘焙）。`beginPass(nullptr)` 约定下，swapchain 的默认 render pass 来自 `VulkanDevice::swapchainRenderPass()`。

**教训**：接口设计为 GL 形状，Vulkan 后端就要在**接口缝**里做延迟决策。这里的选择是"延迟到第一次绑定管线时"，让上层代码完全无感。将来引入多 pass 时，这里要升级成 render pass 兼容性缓存（key = 附件格式组合）。

### 6.6 validation layers 在 macOS 可能不可用

**症状**：`vkCreateInstance` 返回 `VK_ERROR_LAYER_NOT_PRESENT`，或者程序干脆没输出任何 debug 消息。

**原因**：`VK_LAYER_KHRONOS_validation` 由 Vulkan SDK 提供，但 macOS 上 SDK 的层不一定完整安装。直接用裸 SDK 装，层可能就找不到。

**修复**：`VulkanDevice::createInstance()` 先 `vkEnumerateInstanceLayerProperties` 探测层是否存在，存在才启用；不存在就 `spdlog::warn` 跳过，不阻塞启动。debug messenger 通过 `vkGetInstanceProcAddr` 动态加载，只在层启用时创建。

**教训**：validation layer 是调试利器（Vulkan 版 `glDebugMessageCallback`，但强大得多，能抓资源泄漏和同步错误），但**不能假设它有**。凡是"可选能力"都要探测 + 降级，这和 6.1 的扩展探测是同一个心智模型：Vulkan 的世界里没有想当然。

---

## 附：验证方式

```bash
# GL 后端（默认）
cmake -S . -B build-gl && cmake --build build-gl -j && ./build-gl/rhi_verify

# Vulkan 后端
cmake -S . -B build-vk -DO5M_VULKAN=ON && cmake --build build-vk -j && ./build-vk/rhi_verify
```

Vulkan 版运行 3 帧后打印 `[RHI_VERIFY] PASS: triangle rendered to swapchain`，退出码 0。stderr 里出现 `[Vulkan]` 开头的 validation 警告属于可诊断项，出现 `assert` 即失败。
