# Phase 0 练习清单（你写，我审）

结构迁移已完成（我做的部分）：core 按 7 模块分目录、apps/ 替代 main/、
rhi_verify 迁移后回归通过。以下三个练习按建议顺序做，每完成一个单独 commit。

行号基于迁移后的当前代码。

---

## 练习 1：ResourceManager 全生命周期修复

> 学习点：句柄式资源池（slot + generation 防悬垂）、free-list、单一职责。

### Bug 清单

| # | 位置 | 问题 |
|---|------|------|
| 1 | `core/Src/Resources/O5MResourceManager.cpp:30-39` | `freeHead == 0xFFFF`（无空闲槽）时 push 新槽后**没有把 freeHead 指向它**，接着无条件 `resources.at(freeHead)` → 必抛 `std::out_of_range`。首次 create 就崩 |
| 2 | 同上 :40 | 新槽路径也执行 `generation++`，但 push 时 generation 已设 1，再 ++ 变 2——想想 generation 的语义到底是什么再写 |
| 3 | 全文件 | `idToIndex` 从不写入 → `hasResource`/`acquire`/`release` 全是死代码。create 时应记录 `resourceId → slot index` |
| 4 | `core/Src/Resources/O5MResourceManager.cpp:19` vs `core/Inc/Resources/O5MResource.h:66` | 失败句柄 `index=0xFFFF` 与 `isValid(){index!=0}` 冲突：失败句柄"有效"，第 0 号槽的句柄反而"无效"。统一约定（建议：0 无效，槽从 1 开始） |
| 5 | `release()` :60-71 | 只 unload 不清 `slot.resource`、不删 `idToIndex` 条目 → 之后 `hasResource` 仍 true，acquire 拿到已卸载的资源 |
| 6 | `core/Inc/Resources/O5MResourceManager.h:29-32` | 死线程成员（你自己的注释都说了），删 |
| 7 | `core/Src/Resources/O5MResource.cpp:7-14` | `O5MResourceHandle<T>::get()` 是模板却定义在 .cpp → 其他编译单元链接不过，与 `O5MEntity.cpp` 同病。挪进头文件 |

### 任务
1. 修 create / acquire / release 全周期，generation 语义自洽（悬垂句柄被 get() 拒绝）。
2. 删除死成员；handle 的 get() 挪到 `O5MResource.h`。
3. 写 `test/resmgr_cycle_test.cpp`（新 CMake 目标或临时 main）：用一个最小
   `FakeResource : O5MResource`（doLoad 返回 true 即可）跑
   `create → acquire → release → 再 create`，断言：
   - 每步不崩，句柄 isValid 语义正确
   - release 后旧句柄失效（generation 不匹配）
   - 同名/同 id 语义（如果你打算支持按名查找，顺便设计）

### 参考
句柄设计读："Generational Indices / slot map"（搜索 Ella Hoskins 的 Implementing a
Slot Map 或 Bitsquid 的 Building a Data-Oriented Entity System 前半）。

---

## 练习 2：TextureResource 上传/采样链路修复

> 学习点：staging 上传、HOST_VISIBLE vs HOST_COHERENT 与 flush、图像布局状态机、
> image usage 与采样权限。

### Bug 清单

| # | 位置 | 问题 |
|---|------|------|
| 1 | `core/Src/Resources/O5MTextureResource.cpp:33,46` | `stbi_image_free(pixels)` 调用**两次** → double free |
| 2 | 同上 :42 | image usage 只有 `TransferDst\|TransferSrc`，缺 `eSampled` → 创建出来的图无法被采样 |
| 3 | 同上 :28-32 | staging 内存 `eHostVisible` 但**非 coherent** 且 memcpy 后不 `flushMappedMemoryRanges` → 写入可能对 GPU 不可见 |
| 4 | `core/Src/RHI/O5MDevice.cpp:153-187` | `copyBufferToImage` **不做任何布局转换**：copy 前应 `eUndefined → eTransferDstOptimal`，copy 后应 `→ eShaderReadOnlyOptimal`。修复位置你定：给 device 函数加 before/after 布局参数，还是在 TextureResource 里自己录？想清楚职责归属再动手（写进 commit message） |
| 5 | `core/Inc/Resources/O5MTextureResource.h:17,36` | `getImageView()` 被注释掉：load() 时创建 view（color aspect、`eR8G8B8A8Srgb`、subresourceRange 覆盖全部 mip） |
| 6 | 思考 | :25 `mipLevels = floor(log2(64)) = 6`，完整链到 1x1 应为 7。现在没生成 mip 不炸，Phase 4 IBL 回来修，代码里留 TODO |
| 7 | 思考 | 你自己的注释（h:10）："ImageView/Sampler 该不该放资源里？" 建议：view 与 image 同生命周期，归资源；sampler 是采样策略，归使用方（材质）。本次只做 view，结论写进头文件注释 |

### 验收（TDD 靶场已备好）
`apps/tex_verify.cpp` 已写好（契约见该文件头注释），它内嵌一张 187 字节棋盘 PNG：
PNG → stbi → staging → device image → NEAREST 采样 → sRGB 附件 → 回读逐像素比对。

修复完成后：
1. 根 CMakeLists.txt 取消 `tex_verify` 目标的两行注释
2. `cmake --build build --target tex_verify && ./build/tex_verify`
3. 看到 `[PASS] tex_verify` 即通过（开着 validation layer 跑，有布局错会直接报）

### 参考
`.cache/vulkan-tut/repo/attachments/`：`20_staging_buffer.cpp`、`24_texture_image.cpp`
（含完整布局转换写法）、`25_sampler.cpp`、`26_texture_mapping.cpp`。

---

## 练习 3：MeshResource 顶点数据补全

> 学习点：glTF 属性语义、顶点布局设计（为 PBR normal mapping 备好切线）。

### Bug 清单

| # | 位置 | 问题 |
|---|------|------|
| 1 | `core/Src/Resources/O5MMeshResource.cpp:104-109` | 顶点只有 pos/color/texCoord，**无法线**，颜色硬编码白。加 `normal(vec3)` 与 `tangent(vec3)`（glTF `NORMAL`/`TANGENT` accessor，无 tangent 时填 (1,0,0) 占位即可） |
| 2 | 同上 :86-89 | 16 位 / 8 位索引直接 throw。支持：按 `componentType` 分支读 `uint16_t`/`uint32_t`（8 位索引场景极罕见，可继续拒但别用 throw 当控制流） |

### 验收
- Phase 3 之前没有顶点输入管线，视觉验收推迟；
- 本次验收 = 我的代码审查 + 用一个 16 位索引的 GLB 调 `load()` 不抛异常、
  索引值域正确（临时打印或断言即可）。
- 提示：DamagedHelmet 等 glTF 样例是 32 位索引为主，16 位的典型是低模；
  tinygltf 的 `bufferView.byteStride` 别忽略（交错存取时）。

---

## 完成标志

- [ ] 练习 1：resmgr_cycle_test 通过
- [ ] 练习 2：tex_verify [PASS]（CMake 注释已取消）
- [ ] 练习 3：代码审查通过 + 16 位索引 GLB 加载成功
- [ ] rhi_verify 依旧 [PASS]（每步收尾跑一次）

> 提交建议：结构迁移（我做的部分）你先单独 commit 一次（纯 move，git 会识别
> rename），之后每个练习一个 commit，message 里写清 bug 成因和你的取舍。
