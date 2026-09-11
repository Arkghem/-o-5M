# o5m-render

一个以学习为目的、从零搭建的 Vulkan 渲染器。不追求工业级完备，而是把现代渲染引擎的每一层（RHI、Render Graph、资源管理、ECS 场景组织）亲手实现一遍，最终目标是跑通一套 PBR 渲染管线。

> 项目从 OpenGL 起步，后整体迁移到 Vulkan RAII（`vulkan_raii.hpp`），macOS 下经 MoltenVK 验证可跑。

## Features

### RHI 层

- **设备层集中立法**：`vkCreateBuffer / vkCreateImage / vkAllocateMemory` 只允许出现在 `O5MDevice.cpp` 一个文件里，全引擎 GPU 对象创建与内存分配的唯一出口
- **内存分配账本**：追踪 `maxMemoryAllocationCount`（多数驱动约 4096）天花板，为将来迁移 VMA sub-allocation 留验收标准
- **Dynamic Rendering**：使用 Vulkan 1.3 动态渲染（1.2 走 `VK_KHR_dynamic_rendering` 扩展），不使用 legacy RenderPass
- **Runtime Shader 编译**：GLSL 源码经 shaderc 在运行时编译为 SPIR-V，无离线编译步骤

### Render Graph

- 声明式资源：`addResource` / `addBufferResource` 描述 Image / Buffer，不绑定具体 Vulkan 对象
- 声明式 Pass：每个 Pass 只声明 `inputs` / `outputs` 与录制 lambda
- `compile()` 依据读写依赖推导执行顺序，pass 间用 semaphore 做 signal/wait 同步
- `execute()` 支持 fence 等待，是 frames-in-flight 的雏形
- HOST_VISIBLE（UBO 直写）与 DEVICE_LOCAL（staging 上传）两种 buffer 策略

### 资源系统

- 句柄式引用（`O5MResourceHandle<T>`）+ 引用计数 + generation 槽位复用，杜绝悬垂指针
- 单例 `O5MResourceManager` 统一管理生命周期
- glTF 模型加载（tinygltf）、纹理加载（stb_image）、Shader 资源三类资源已接入

### 场景与 ECS

- 轻量 ECS：`O5MEntity` + `O5MComponent` + 编译期 ComponentTypeID 系统
- 内置 `TransformComponent` / `CameraComponent` / `MeshComponent`
- 视锥剔除系统 `O5MCullingSystem`
- 键盘 / 鼠标事件系统

## Architecture

```text
+---------------------------------------------------+
|  main/                                            |
|    o5m          -- 主程序（窗口 / 主循环）          |
|    rhi_verify   -- 无窗口离屏验证程序               |
+---------------------------------------------------+
|  core/                                            |
|    O5MRendergraph    -- 渲染图：资源声明/依赖排序    |
|    O5MDevice         -- GPU 对象创建唯一出口        |
|    O5MPipeline       -- 图形管线封装               |
|    O5MRenderer       -- 渲染器入口                 |
|    O5MResource*      -- 句柄式资源系统              |
|    O5MEntity/...     -- ECS 场景组织               |
|    O5MCullingSystem  -- 视锥剔除                   |
+---------------------------------------------------+
|  third_party/  glad / stb                          |
|  FetchContent   tinygltf    |  system: glm glfw    |
|  Vulkan SDK    vulkan + shaderc                    |
+---------------------------------------------------+
```

## Build

依赖：

- CMake >= 3.21，C++20 编译器
- [Vulkan SDK](https://vulkan.lunarg.com/)（含 shaderc）
- GLFW3、glm

```bash
# macOS
brew install glfw glm

git clone https://github.com/Arkghem/-o-5M.git
cd -o-5M
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Verify

`rhi_verify` 是无窗口的离屏全链路验证程序，不依赖显示环境，可在 CI 中跑：

- Vulkan instance / device / queue 最小 bootstrap（macOS MoltenVK 可跑）
- RenderGraph `addResource -> addPass -> compile -> execute -> wait` 全流程
- 两个 pass 的依赖关系（render 写 rt -> composite 读 rt）能正确排序执行
- fence 同步：submit 后等待完成再退出

```bash
./build/rhi_verify   # 退出码 0 = 全链路通过
```

## Roadmap

- [x] Vulkan bootstrap + 设备层立法
- [x] RenderGraph 依赖排序与同步
- [x] 句柄式资源管理系统
- [ ] UBO 资源绑定
- [ ] frames-in-flight
- [ ] PBR 材质系统（IBL / BRDF）
- [ ] 阴影与后处理 pass

## Third-party

| 库 | 用途 | 引入方式 |
| --- | --- | --- |
| vulkan-hpp (RAII) | Vulkan C++ 绑定 | Vulkan SDK |
| shaderc | 运行时 GLSL -> SPIR-V | Vulkan SDK |
| tinygltf | glTF 模型加载 | FetchContent v2.9.4 |
| glm | 数学库 | 系统包 |
| GLFW | 窗口与输入 | 系统包 |
| stb | 纹理加载 | vendored |
| glad | OpenGL loader（历史遗留） | vendored |

## License

[MIT](LICENSE)
